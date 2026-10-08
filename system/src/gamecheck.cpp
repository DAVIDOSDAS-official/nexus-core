#include <nexus/system/gamecheck.hpp>
#include <nexus/system/json.hpp>

#include <algorithm>
#include <cctype>
#include <sstream>

namespace nexus::system {

namespace {

std::string lower(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return text;
}

std::vector<std::string> splitList(const std::string& text) {
    std::vector<std::string> out;
    std::string item;
    for (char c : text) {
        if (c == ';') {
            if (!item.empty()) out.push_back(item);
            item.clear();
        } else {
            item += c;
        }
    }
    if (!item.empty()) out.push_back(item);
    return out;
}

bool has(const std::vector<std::string>& list, const std::string& value) {
    return std::find(list.begin(), list.end(), value) != list.end();
}

}  // namespace

std::vector<SteamMatch> parseSteamSearch(const std::string& json) {
    std::vector<SteamMatch> out;
    const auto parsed = parseJson(json);
    if (!parsed.error.empty()) {
        return out;
    }
    for (const JsonValue& item : parsed.value["items"].items) {
        const std::string type = item["type"].asString();
        if (!type.empty() && type != "app") {
            continue;
        }
        SteamMatch match;
        match.id = item["id"].asString();
        match.name = item["name"].asString();
        if (!match.id.empty()) {
            out.push_back(std::move(match));
        }
    }
    return out;
}

SteamMatch pickSteamMatch(const std::vector<SteamMatch>& matches,
                          const std::string& typed) {
    const std::string want = lower(typed);
    for (const SteamMatch& match : matches) {
        if (lower(match.name) == want) {
            return match;
        }
    }
    return matches.empty() ? SteamMatch{} : matches.front();
}

ProtonSummary parseProtonSummary(const std::string& json) {
    ProtonSummary summary;
    const auto parsed = parseJson(json);
    if (!parsed.error.empty() || !parsed.value.isObject()) {
        return summary;
    }
    summary.tier = parsed.value["tier"].asString();
    summary.trending = parsed.value["trendingTier"].asString();
    summary.confidence = parsed.value["confidence"].asString();
    summary.reports = static_cast<long>(parsed.value["total"].number);
    summary.found = !summary.tier.empty();
    return summary;
}

std::string describeTier(const std::string& tier) {
    const std::string t = lower(tier);
    if (t == "platinum") return "runs perfectly, nothing to change";
    if (t == "gold") return "runs perfectly after small tweaks";
    if (t == "silver") return "runs, with minor problems";
    if (t == "bronze") return "runs, but often crashes or has problems";
    if (t == "borked") return "does not run";
    if (t == "native") return "has a Linux version of its own";
    if (t == "pending") return "not enough reports yet";
    return t.empty() ? "no reports" : t;
}

AntiCheatEntry findAntiCheat(const std::string& gamesJson,
                             const std::string& steamId,
                             const std::string& name) {
    AntiCheatEntry entry;
    const auto parsed = parseJson(gamesJson);
    if (!parsed.error.empty() || !parsed.value.isArray()) {
        return entry;
    }

    const JsonValue* hit = nullptr;
    const std::string wantName = lower(name);

    for (const JsonValue& game : parsed.value.items) {
        if (!steamId.empty() &&
            game["storeIds"]["steam"].asString() == steamId) {
            hit = &game;
            break;
        }
    }
    if (hit == nullptr && !wantName.empty()) {
        for (const JsonValue& game : parsed.value.items) {
            if (lower(game["name"].asString()) == wantName) {
                hit = &game;
                break;
            }
        }
    }
    if (hit == nullptr) {
        return entry;
    }

    entry.found = true;
    entry.name = (*hit)["name"].asString();
    entry.steamId = (*hit)["storeIds"]["steam"].asString();
    entry.status = (*hit)["status"].asString();
    entry.native = (*hit)["native"].boolean;
    for (const JsonValue& a : (*hit)["anticheats"].items) {
        entry.anticheats.push_back(a.asString());
    }
    for (const JsonValue& note : (*hit)["notes"].items) {
        // [text, link]; the text is what a person reads.
        if (note.isArray() && !note.items.empty()) {
            entry.notes.push_back(note.items.front().asString());
        }
    }
    return entry;
}

std::string describeAntiCheatStatus(const std::string& status) {
    if (status == "Supported") {
        return "the developer turned on Linux support; online play works";
    }
    if (status == "Running") {
        return "runs, though the developer has not said it is supported";
    }
    if (status == "Planned") {
        return "the developer plans Linux support; it does not work yet";
    }
    if (status == "Broken") {
        return "should work but is broken at the moment";
    }
    if (status == "Denied") {
        return "the developer blocks Linux on purpose. It will not work,"
               " and Nexus will not get around that";
    }
    return status;
}

std::string gameVerdict(const ProtonSummary& proton,
                        const AntiCheatEntry& antiCheat) {
    if (antiCheat.found) {
        if (antiCheat.status == "Denied") {
            return "Will not work: the anti-cheat blocks Linux, by the"
                   " developer's choice.";
        }
        if (antiCheat.status == "Broken") {
            return "Not right now: its anti-cheat is broken on Linux.";
        }
        if (antiCheat.status == "Planned") {
            return "Not yet: Linux support is only planned.";
        }
    }
    if (antiCheat.found && antiCheat.native) {
        return "Works: it has a Linux version.";
    }

    const std::string t = lower(proton.tier);
    const bool online = antiCheat.found &&
        (antiCheat.status == "Supported" || antiCheat.status == "Running");
    const std::string extra = online ? ", online included" : "";

    if (t == "native") return "Works: it has a Linux version.";
    if (t == "platinum" || t == "gold") {
        return "Should work, through Steam's Proton" + extra + ".";
    }
    if (t == "silver") return "Likely works, with some problems" + extra + ".";
    if (t == "bronze") return "Might work; expect problems.";
    if (t == "borked") return "Unlikely to work.";
    return "Unknown: nobody has reported on it yet.";
}

std::vector<std::pair<std::string, std::string>>
describeSandbox(const std::string& permissions) {
    std::vector<std::string> shared, sockets, devices, filesystems;
    std::vector<std::string> systemTalk;
    std::string section;

    std::istringstream in(permissions);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.front() == '[') {
            section = line;
            continue;
        }
        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string key = line.substr(0, eq);
        const std::string value = line.substr(eq + 1);
        if (section == "[Context]") {
            if (key == "shared") shared = splitList(value);
            if (key == "sockets") sockets = splitList(value);
            if (key == "devices") devices = splitList(value);
            if (key == "filesystems") filesystems = splitList(value);
        } else if (section == "[System Bus Policy]" ||
                   section == "[Session Bus Policy]") {
            systemTalk.push_back(key);
        }
    }

    std::vector<std::pair<std::string, std::string>> out;

    // Files.
    std::string files;
    const struct { const char* id; const char* name; } folders[] = {
        {"xdg-download", "Downloads"}, {"xdg-documents", "Documents"},
        {"xdg-music", "Music"},        {"xdg-pictures", "Pictures"},
        {"xdg-videos", "Videos"},      {"xdg-desktop", "Desktop"},
    };
    bool everything = false;
    for (const std::string& fs : filesystems) {
        const std::string base = fs.substr(0, fs.find(':'));
        if (base == "home" || base == "host" || base == "~") {
            everything = true;
        }
    }
    if (everything) {
        files = "all your files";
    } else {
        for (const auto& folder : folders) {
            for (const std::string& fs : filesystems) {
                const std::string base = fs.substr(0, fs.find(':'));
                if (base == folder.id) {
                    if (!files.empty()) files += ", ";
                    files += folder.name;
                    if (fs.find(":ro") != std::string::npos) {
                        files += " (read only)";
                    }
                }
            }
        }
        files = files.empty() ? "only its own folder"
                              : "its own folder, plus " + files;
    }
    out.emplace_back("Your files", files);

    out.emplace_back("Network",
                     has(shared, "network") ? "yes (needed to play online)"
                                            : "no");

    if (has(sockets, "pulseaudio")) {
        out.emplace_back("Microphone",
                         "yes, through the sound system, without asking"
                         " (voice chat uses it)");
    } else {
        out.emplace_back("Microphone", "only if it asks and you allow it");
    }

    if (has(devices, "all")) {
        out.emplace_back("Devices",
                         "all of them: game controllers need it, and that"
                         " includes a webcam");
    } else if (has(devices, "input")) {
        out.emplace_back("Devices", "game controllers only");
    } else {
        out.emplace_back("Devices", "none");
    }

    bool geo = false;
    for (const std::string& name : systemTalk) {
        if (name.find("GeoClue") != std::string::npos) geo = true;
    }
    out.emplace_back("Location",
                     geo ? "yes" : "only if it asks and you allow it");

    return out;
}

std::vector<std::pair<std::string, std::string>>
describeIdentifiers(const std::vector<IdentifierProbe>& probes) {
    struct Wording {
        const char* key;
        const char* what;
        const char* readable;   // when an ordinary program can read it
        const char* protectedText;
    };
    // In the order a person would think of them.
    const Wording words[] = {
        {"mac", "Network card",
         "readable: its hardware address, unique to the card",
         "protected"},
        {"disk", "Disks",
         "readable: model and serial number of each disk",
         "protected"},
        {"machine-id", "Machine ID",
         "readable: made at install, new after a reinstall",
         "protected"},
        {"screen", "Screen",
         "readable: the screen's own information, with a serial if it has one",
         "protected"},
        {"cpu", "Processor",
         "model only: x86 processors have no serial a program can read",
         "protected"},
        {"board", "Motherboard serials",
         "readable",
         "protected: root only, and no game here runs as root"},
        {"tpm", "TPM security chip",
         "readable",
         "protected: root only. Windows kernel anti-cheats ban by it;"
         " those games do not run on Linux"},
    };

    std::vector<std::pair<std::string, std::string>> out;
    for (const Wording& w : words) {
        for (const IdentifierProbe& p : probes) {
            if (p.key != w.key) continue;
            if (!p.present) {
                out.emplace_back(w.what, "none on this computer");
            } else {
                out.emplace_back(w.what,
                                 p.readable ? w.readable : w.protectedText);
            }
            break;
        }
    }
    return out;
}

bool sandboxAllows(const std::string& permissions, const std::string& key,
                   const std::string& value) {
    std::string section;
    std::istringstream in(permissions);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.front() == '[') {
            section = line;
            continue;
        }
        if (section != "[Context]") continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos || line.substr(0, eq) != key) continue;
        for (const std::string& item : splitList(line.substr(eq + 1))) {
            if (item == value) return true;
        }
    }
    return false;
}

std::string urlEncode(const std::string& text) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : text) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += static_cast<char>(c);
        } else {
            out += '%';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

}  // namespace nexus::system
