// nexus gamecheck.
//
// Part of the `nexus` tool. Until 0.1.96 this was in cli/src/main.cpp,
// which had grown past 11,000 lines; it was moved here unchanged.

#include "cli.hpp"

namespace nexus_cli {

namespace {

// ---------------------------------------------------------------------
// nexus gamecheck <game>: will it run here, and what can it see.
//
// Reads public data with curl (ProtonDB, AreWeAntiCheatYet, the Steam
// store search) and the Steam Flatpak's own permissions. Reports only:
// it changes nothing, and a game whose anti-cheat blocks Linux is said
// to be blocked, with no way around it offered.
std::string fetchUrl(const std::string& url) {
    if (!nexus::system::commandExists("curl")) {
        return {};
    }
    // The URL is built here from fixed parts and urlEncode()d input,
    // so it contains no quote; checked anyway. https only, redirects
    // included: a network that answers with a redirect to http:// must
    // not get to write the verdict (0.1.86, outside review P6). Nothing
    // asked for here is more than a few megabytes; an answer that says
    // it is over 32 is refused rather than read into memory (0.1.87).
    if (url.find('\'') != std::string::npos) {
        return {};
    }
    const auto result = nexus::system::runCommand(
        "curl -fsSL --proto =https --proto-redir =https --max-time 20 --retry 2 "
        "--max-filesize 33554432 " +
            shellQuote(url) + " 2>/dev/null",
        false);
    return result.ok ? result.text : std::string{};
}

// AreWeAntiCheatYet's list is a few megabytes and changes slowly:
// kept for a day, and an older copy is used (and said to be used) when
// the network is not there.
std::string antiCheatList(std::string& note) {
    std::string dir;
    if (const char* cache = std::getenv("XDG_CACHE_HOME"); cache && *cache) {
        dir = std::string(cache) + "/nexus";
    } else if (const char* home = std::getenv("HOME"); home && *home) {
        dir = std::string(home) + "/.cache/nexus";
    }
    const std::string path =
        dir.empty() ? "" : dir + "/areweanticheatyet-games.json";

    std::error_code ec;
    bool fresh = false;
    if (!path.empty() && std::filesystem::exists(path, ec)) {
        const auto age = std::filesystem::file_time_type::clock::now() -
                         std::filesystem::last_write_time(path, ec);
        fresh = !ec && age < std::chrono::hours(24);
    }

    auto readFile = [](const std::string& file) {
        std::ifstream in(file);
        std::stringstream buffer;
        buffer << in.rdbuf();
        return buffer.str();
    };

    if (fresh) {
        return readFile(path);
    }

    const std::string text = fetchUrl(
        "https://raw.githubusercontent.com/AreWeAntiCheatYet/"
        "AreWeAntiCheatYet/master/games.json");
    if (!text.empty() && nexus::system::parseJson(text).error.empty()) {
        if (!path.empty()) {
            std::filesystem::create_directories(dir, ec);
            std::ofstream out(path);
            out << text;
        }
        return text;
    }
    if (!path.empty() && std::filesystem::exists(path, ec)) {
        note = "(anti-cheat list from an older copy; the network did not answer)";
        return readFile(path);
    }
    return {};
}

// What an ordinary program could read on this machine, by file
// permissions only: "others" may read it. Run under sudo, a program
// can read everything, which is not what a game gets, so the
// permission bits are the answer rather than a test read. No value is
// read or printed (the screen's information is only measured).
std::vector<nexus::system::IdentifierProbe> probeIdentifiers() {
    namespace fs = std::filesystem;
    auto worldReadable = [](const fs::path& p) {
        std::error_code ec;
        const auto st = fs::status(p, ec);
        if (ec) return false;
        return (st.permissions() & fs::perms::others_read) != fs::perms::none;
    };
    auto exists = [](const fs::path& p) {
        std::error_code ec;
        return fs::exists(p, ec);
    };
    std::vector<nexus::system::IdentifierProbe> out;
    std::error_code ec;

    {   // Network cards other than loopback and virtual ones.
        nexus::system::IdentifierProbe p{"mac"};
        for (const auto& e : fs::directory_iterator("/sys/class/net", ec)) {
            const std::string name = e.path().filename().string();
            if (name == "lo" || !exists(e.path() / "device")) continue;
            p.present = true;
            p.readable = p.readable || worldReadable(e.path() / "address");
        }
        out.push_back(p);
    }
    {   // Disk names with serials: /dev/disk/by-id, and NVMe's own file.
        nexus::system::IdentifierProbe p{"disk"};
        const fs::path byId = "/dev/disk/by-id";
        for (const auto& e : fs::directory_iterator(byId, ec)) {
            const std::string name = e.path().filename().string();
            if (name.rfind("ata-", 0) == 0 || name.rfind("nvme-", 0) == 0 ||
                name.rfind("scsi-", 0) == 0 || name.rfind("wwn-", 0) == 0) {
                p.present = true;
                p.readable = p.readable || worldReadable(byId);
            }
        }
        for (const auto& e : fs::directory_iterator("/sys/class/nvme", ec)) {
            if (exists(e.path() / "serial")) {
                p.present = true;
                p.readable = p.readable || worldReadable(e.path() / "serial");
            }
        }
        out.push_back(p);
    }
    {
        nexus::system::IdentifierProbe p{"machine-id"};
        p.present = exists("/etc/machine-id");
        p.readable = worldReadable("/etc/machine-id");
        out.push_back(p);
    }
    {   // A connected screen has a non-empty EDID.
        nexus::system::IdentifierProbe p{"screen"};
        for (const auto& e : fs::directory_iterator("/sys/class/drm", ec)) {
            const fs::path edid = e.path() / "edid";
            if (!exists(edid)) continue;
            std::ifstream in(edid, std::ios::binary);
            if (in.peek() == std::ifstream::traits_type::eof()) continue;
            p.present = true;
            p.readable = p.readable || worldReadable(edid);
        }
        out.push_back(p);
    }
    {
        nexus::system::IdentifierProbe p{"cpu"};
        p.present = true;
        p.readable = true;
        out.push_back(p);
    }
    {
        nexus::system::IdentifierProbe p{"board"};
        for (const char* f : {"/sys/class/dmi/id/product_serial",
                              "/sys/class/dmi/id/board_serial",
                              "/sys/class/dmi/id/product_uuid"}) {
            if (!exists(f)) continue;
            p.present = true;
            p.readable = p.readable || worldReadable(f);
        }
        out.push_back(p);
    }
    {
        nexus::system::IdentifierProbe p{"tpm"};
        for (const char* f : {"/dev/tpm0", "/dev/tpmrm0"}) {
            if (!exists(f)) continue;
            p.present = true;
            p.readable = p.readable || worldReadable(f);
        }
        out.push_back(p);
    }
    return out;
}

// The section itself. `permissions` is the launcher's Flatpak
// permissions, or empty when there is no launcher to ask.
void printIdentifiers(const std::string& launcher,
                      const std::string& permissions) {
    std::cout << "\n  What a game here can recognise this computer by\n";
    for (const auto& [what, answer] :
         nexus::system::describeIdentifiers(probeIdentifiers())) {
        std::string label = what;
        while (label.size() < 21) label += ' ';
        std::cout << "    " << label << answer << "\n";
    }
    if (!permissions.empty()) {
        const bool net =
            nexus::system::sandboxAllows(permissions, "shared", "network");
        const bool dev =
            nexus::system::sandboxAllows(permissions, "devices", "all");
        std::cout << "    Inside " << launcher << "'s sandbox: "
                  << (net ? "it shares the network, so it sees the network"
                            " card;"
                          : "no network, so not the network card;")
                  << "\n      "
                  << (dev ? "it has all devices, so it sees the disks;"
                          : "no devices, so not the disks;")
                  << " Flatpak gives every app the machine ID.\n";
    }
    std::cout << "    Nexus only reports these. It does not hide or change"
                 " them: what an\n    anti-cheat may see is between the game"
                 " and its player.\n";
}

}  // namespace

int commandGamecheck(const std::vector<std::string>& words) {
    std::string typed;
    for (const std::string& word : words) {
        if (!typed.empty()) typed += " ";
        typed += word;
    }
    if (typed.empty()) {
        std::cerr << "Usage: nexus gamecheck <game name or Steam id>\n"
                     "       nexus gamecheck --identifiers\n";
        return 2;
    }
    // The machine on its own, no game and no network.
    if (typed == "--identifiers" || typed == "identifiers") {
        const auto perms = nexus::system::runCommand(
            "flatpak info --show-permissions com.valvesoftware.Steam"
            " 2>/dev/null", false);
        printIdentifiers("Steam",
            perms.ok && perms.text.find("[Context]") != std::string::npos
                ? perms.text : std::string());
        std::cout << "\nNothing has been changed.\n";
        return 0;
    }
    if (!nexus::system::commandExists("curl")) {
        std::cerr << "gamecheck needs curl to ask ProtonDB and"
                     " AreWeAntiCheatYet.\n";
        return 1;
    }

    // Which game. A number is taken as a Steam id.
    std::string steamId;
    std::string name = typed;
    std::vector<nexus::system::SteamMatch> others;
    // The store answers with something for almost anything typed:
    // "minecraft" gave Minecraft Dungeons II (Asus, 8 October), and
    // Minecraft itself is not on Steam. Say so when the name differs.
    bool closestOnly = false;
    auto lowerCopy = [](std::string t) {
        for (char& c : t) {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return t;
    };

    if (std::all_of(typed.begin(), typed.end(),
                    [](unsigned char c) { return std::isdigit(c); })) {
        steamId = typed;
    } else {
        const auto matches = nexus::system::parseSteamSearch(fetchUrl(
            "https://store.steampowered.com/api/storesearch/?term=" +
            nexus::system::urlEncode(typed) + "&l=english&cc=US"));
        const auto pick = nexus::system::pickSteamMatch(matches, typed);
        if (!pick.id.empty()) {
            steamId = pick.id;
            name = pick.name;
            closestOnly = lowerCopy(pick.name) != lowerCopy(typed);
            // Only names that contain what was typed: the store's
            // search also answers with things like "Pure Farming 2018 -
            // Gomselmash Palesse CS-200" for "counter-strike 2".
            auto lowered = [](std::string t) {
                for (char& c : t) {
                    c = static_cast<char>(
                        std::tolower(static_cast<unsigned char>(c)));
                }
                return t;
            };
            const std::string want = lowered(typed);
            for (const auto& m : matches) {
                if (m.id != pick.id && others.size() < 4 &&
                    lowered(m.name).find(want) != std::string::npos) {
                    others.push_back(m);
                }
            }
        }
    }

    std::string listNote;
    const std::string games = antiCheatList(listNote);
    const auto antiCheat =
        nexus::system::findAntiCheat(games, steamId, name);
    if (steamId.empty() && antiCheat.found) {
        // The store search failed or found nothing, but the anti-cheat
        // list knows the game, often with its Steam id.
        name = antiCheat.name;
        steamId = antiCheat.steamId;
    } else if (name == steamId && antiCheat.found) {
        // Asked by number: the list knows its name ("1245620" read
        // "1245620  (Steam 1245620)" on the Asus, 2 October).
        name = antiCheat.name;
    }

    nexus::system::ProtonSummary proton;
    bool protonAnswered = false;
    if (!steamId.empty()) {
        const std::string answer = fetchUrl(
            "https://www.protondb.com/api/v1/reports/summaries/" + steamId +
            ".json");
        protonAnswered = !answer.empty();
        proton = nexus::system::parseProtonSummary(answer);
    }

    if (steamId.empty() && !antiCheat.found) {
        std::cout << "No game called \"" << typed << "\" was found on Steam"
                     " or in the anti-cheat list.\n"
                     "It may be a phone or console game, or sold only by its"
                     " maker.\n"
                     "(Or the network did not answer. Try the exact name, or"
                     " the Steam id\nfrom the game's store address.)\n";
        return 1;
    }

    std::cout << name;
    if (!steamId.empty()) std::cout << "  (Steam " << steamId << ")";
    std::cout << "\n";
    if (closestOnly && !antiCheat.found) {
        std::cout << "  The closest name on Steam to \"" << typed
                  << "\"; no game there has exactly that name.\n"
                     "  Some games are sold only by their makers, or only"
                     " on phones or consoles.\n";
    }
    std::cout << "\n";

    std::cout << "  Runs on Linux   ";
    if (proton.found) {
        std::string tier = proton.tier;
        if (!tier.empty()) tier[0] = static_cast<char>(std::toupper(
                                         static_cast<unsigned char>(tier[0])));
        std::cout << tier << " on ProtonDB: "
                  << nexus::system::describeTier(proton.tier);
        if (proton.reports > 0) {
            std::cout << " (" << proton.reports << " reports)";
        }
        std::cout << "\n";
    } else if (!steamId.empty() && !protonAnswered) {
        std::cout << "unknown: ProtonDB did not answer (network?)\n";
    } else if (!steamId.empty()) {
        std::cout << "no ProtonDB reports found\n";
    } else {
        std::cout << "not on Steam, so no ProtonDB reports\n";
    }

    std::cout << "  Anti-cheat      ";
    if (antiCheat.found) {
        std::string which;
        for (const auto& a : antiCheat.anticheats) {
            if (!which.empty()) which += ", ";
            which += a;
        }
        std::cout << (which.empty() ? "yes" : which) << ": "
                  << nexus::system::describeAntiCheatStatus(antiCheat.status)
                  << "\n";
        for (std::size_t i = 0; i < antiCheat.notes.size() && i < 2; ++i) {
            std::cout << "                  note: " << antiCheat.notes[i]
                      << "\n";
        }
    } else if (!games.empty()) {
        std::cout << "none listed (AreWeAntiCheatYet tracks games that"
                     " have one)\n";
    } else {
        std::cout << "unknown (the list could not be fetched)\n";
    }
    if (!listNote.empty()) {
        std::cout << "                  " << listNote << "\n";
    }

    std::string verdict = nexus::system::gameVerdict(proton, antiCheat);
    if (!proton.found && !steamId.empty() && !protonAnswered &&
        verdict.rfind("Unknown", 0) == 0) {
        verdict = "Unknown: ProtonDB could not be reached; try again later.";
    }
    std::cout << "  Verdict         " << verdict << "\n";

    // What it can see: the real permissions of the launcher it would
    // run under. Skipped when it will not run at all, and not shown as
    // Steam's when the game is not a Steam game (Fortnite showed
    // "What Steam can see" on the Asus, 2 October).
    if (antiCheat.found && antiCheat.status == "Denied") {
        std::cout << "\n  Nothing to check on this machine: it will not"
                     " run here.\n";
    } else {
        const bool steamGame = !steamId.empty();
        const std::string app = steamGame ? "com.valvesoftware.Steam"
                                          : "com.heroicgameslauncher.hgl";
        const std::string launcher = steamGame ? "Steam" : "Heroic";

        if (!steamGame) {
            std::cout << "\n  Not a Steam game. Epic and GOG games run"
                         " through Heroic (gaming add-on).\n";
        }
        std::cout << "\n  What " << launcher << " can see on this machine\n";
        const auto perms = nexus::system::runCommand(
            "flatpak info --show-permissions " + shellQuote(app) + " 2>/dev/null",
            false);
        if (!perms.ok ||
            perms.text.find("[Context]") == std::string::npos) {
            std::cout << "    " << launcher << " is not installed here, so"
                         " there is nothing to report.\n";
            if (osReleaseValue("VARIANT_ID") == "minimal") {
                std::cout << "    (The minimal edition has no gaming"
                             " add-on; the KDE edition does.)\n";
            } else {
                std::cout << "    To install it: sudo nexus setup gaming"
                             " --apply\n";
            }
        } else {
            for (const auto& [what, answer] :
                 nexus::system::describeSandbox(perms.text)) {
                std::string label = what;
                while (label.size() < 14) label += ' ';
                std::cout << "    " << label << answer << "\n";
            }
        }
        const bool haveLauncher =
            perms.ok && perms.text.find("[Context]") != std::string::npos;
        printIdentifiers(launcher, haveLauncher ? perms.text : std::string());
    }

    if (!others.empty()) {
        std::cout << "\n  Not the one? Also on Steam:";
        for (const auto& m : others) {
            std::cout << "\n    " << m.name << "  (nexus gamecheck " << m.id
                      << ")";
        }
        std::cout << "\n";
    }

    std::cout << "\nSources: ProtonDB, AreWeAntiCheatYet, Steam store,"
                 " and this machine's Flatpak\nsettings and file permissions.\n"
                 "Nothing has been changed.\n";
    return 0;
}

}  // namespace nexus_cli
