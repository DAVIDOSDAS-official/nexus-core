// The `nexus` tool: reading the command line, and handing each command
// to its own file (0.1.96; see cli.hpp for what they share and why the
// namespace is called what it is).

#include "cli.hpp"

namespace nexus_cli {

namespace {

void printUsage() {
    std::cout
        << "nexus - a system tool that explains itself\n"
        << "\n"
        << "Usage:\n"
        << "    nexus                  this machine in a few lines\n"
        << "    nexus guide            how to work this system\n"
        << "    nexus gamecheck <game>  will it run here, and what it can see\n"
        << "    nexus gamecheck --identifiers  what a game could recognise this computer by\n"
        << "    nexus language [list | add <name> | set <name>] [--apply]\n"
        << "    nexus update [--apply]   what is new; take it\n"
        << "    nexus update notify off|on|status [--apply]  the daily update notification\n"
        << "    nexus app install|remove|permissions <flathub id> [--apply]\n"
        << "    nexus dualboot [add | remove | <number>] [--disk <path>] [--apply]  other systems on this disk\n"
        << "    nexus gaming [--apply]   what this machine does for games\n"
        << "    nexus server [add | logs | backup | remove | refresh | cockpit]  Minecraft and websites here\n"
        << "    nexus scan\n"
        << "    nexus what-provides <capability>\n"
        << "    nexus why <component>\n"
        << "    nexus inspect <component>\n"
        << "    nexus gaps [kind]\n"
        << "    nexus conflicts\n"
        << "    nexus options <capability>\n"
        << "    nexus install <capability> [--apply] [--yes]\n"
        << "    nexus source <package>       what building it would cost\n"
        << "    nexus container <package> [--from-distro d] [--apply]\n"
        << "    nexus image <profile>\n"
        << "    nexus plan <capability>\n"
        << "    nexus remove <component> [--apply] [--yes]\n"
        << "    nexus doctor\n"
        << "    nexus history\n"
        << "    nexus largest [count] [--unused]\n"
        << "    nexus vpn [check]        is a VPN carrying traffic; DNS and IPv6 leaks; kill switch\n"
        << "    nexus cyber              the cybersecurity toolbox: Kali's tools in a container\n"
        << "                             (--isolated: a box that cannot see your files)\n"
        << "    nexus dev                programming languages on request; nexus dev help <topic>\n"
        << "    nexus security           audit: firewall, updates, encryption, kernel settings...\n"
        << "    nexus security harden [off] [--apply]  stricter kernel settings, or undo them\n"
        << "    nexus rollback           go back to the previous system version (warns first)\n"
        << "    nexus rebase             switch edition, pin a build, or follow the newest again\n"
        << "    nexus vpn keys | vpn config --address A --peer-key K --endpoint H:P\n"
        << "    nexus services [--all]\n"
        << "    nexus hardware\n"
        << "    nexus specs [--plain] [--json]\n"
        << "    nexus secureboot\n"
        << "    nexus setup [<profile>[,<profile>...]] [--apply]\n"
        << "    nexus profile list\n"
        << "    nexus profile show <name>\n"
        << "    nexus profile check <name>[,<name>...]\n"
        << "    nexus solve <capability> [--arch a] [--prefer id]\n"
        << "                             [--require id]\n"
        << "\n"
        << "Options:\n"
        << "    --status <path>   dpkg status file\n"
        << "    --profiles <dir>  profile directory\n"
        << "    --arch <arch>     target architecture\n"
        << "    --with-available  also read available packages\n"
        << "    --with-flatpak    also offer Flatpak applications\n"
        << "    --with-snap       also read installed snaps\n"
        << "    --with-nix        also read the Nix profile\n"
        << "    --with-portage <dir>  also read a Gentoo md5-cache tree\n"
        << "    --with-arch <db>  also offer Arch packages, via a container\n"
        << "    --from <source>   only options from base, flatpak, container or nix\n"
        << "    --rpm | --dpkg    force a package ecosystem\n"
        << "    --rpm-root <dir>  inspect an rpm root elsewhere\n"
        << "    --commands        print the commands instead of running them\n"
        << "    --explain         reason for every component, not just choices\n"
        << "    --lists <dir>     apt lists directory\n"
        << "    --sysfs <dir>     root for hardware detection\n"
        << "    --version, -V     print version and build commit\n"
        << "\n"
        << "Nothing is changed without --apply; every other command\n"
        << "only reads and explains.\n";
}

// A source build's components/<name>, found from where this nexus is
// (build/cli/nexus -> components/), never from the current folder
// (0.1.82, outside review: under sudo, a components/ in whatever folder
// you stood in was read as Nexus's own profiles).
std::string sourceTreeDir(const std::string& name) {
    std::error_code ec;
    const auto self = std::filesystem::read_symlink("/proc/self/exe", ec);
    if (ec) return "/nonexistent/nexus-components/" + name;
    for (auto dir = self.parent_path(); !dir.empty() && dir != dir.root_path(); dir = dir.parent_path()) {
        const auto candidate = dir / "components" / name;
        if (std::filesystem::is_directory(candidate, ec)) return candidate.string();
    }
    return "/nonexistent/nexus-components/" + name;
}

}  // namespace

}  // namespace nexus_cli

// As before the split, when everything main() calls was in an unnamed
// namespace in this file: plain names, found the same way.
using namespace nexus_cli;

int main(int argc, char** argv) {
    std::vector<std::string> arguments(argv + 1, argv + argc);

    // Kept so that a suggested re-run is the command actually typed.
    std::string invocation = argv[0] == nullptr ? "nexus" : argv[0];

    for (const std::string& argument : arguments) {
        invocation += " " + argument;
    }

    std::string statusPath = "/var/lib/dpkg/status";
    std::string prefer;
    std::string require;
    std::string arch;
    // Where profiles live, most specific first: an explicit flag, the
    // environment (which the image sets), the installed location, then
    // the source tree for development.
    std::string profileDir;

    if (const char* fromEnvironment = std::getenv("NEXUS_PROFILES")) {
        profileDir = fromEnvironment;
    } else if (std::filesystem::is_directory(
                   "/usr/share/nexus/profiles")) {
        profileDir = "/usr/share/nexus/profiles";
    } else {
        profileDir = sourceTreeDir("profiles");
    }
    std::string listsDir = "/var/lib/apt/lists";
    std::string sysfsRoot = "/";
    std::string statesPath = "/var/lib/apt/extended_states";
    std::string rpmCache = "/var/cache/libdnf5";
    std::string aptConfigDir = "/etc/apt/apt.conf.d";
    std::string rpmRoot;
    std::string aliasDir;

    if (const char* fromEnvironment = std::getenv("NEXUS_ALIASES")) {
        aliasDir = fromEnvironment;
    } else if (std::filesystem::is_directory(
                   "/usr/share/nexus/aliases")) {
        aliasDir = "/usr/share/nexus/aliases";
    } else {
        aliasDir = sourceTreeDir("aliases");
    }
    bool forceRpm = false;
    bool forceDpkg = false;
    bool withAvailable = false;
    bool withFlatpak = false;
    bool withSnap = false;
    bool withNix = false;
    std::optional<nexus::Source> onlySource;
    std::vector<std::string> archDatabases;
    std::string portagePath;
    std::string fromDistribution = "arch";
    bool explain = false;
    bool apply = false;
    bool assumeYes = false;
    bool showAll = false;
    bool showUnused = false;
    bool showCommands = false;
    std::string vpnInterface = "wg0";
    std::string vpnAddress;
    std::string vpnPeerKey;
    std::string vpnEndpoint;
    std::string vpnRoutes;
    std::string vpnDns;
    std::string vpnPath;
    int vpnKeepalive = 0;
    std::vector<std::string> positional;

    // Before any flag is read (0.1.68):
    //
    // A mistyped command used to fall through to the flags, so `nexus
    // cuber run nmap --version` printed Nexus's own version (the Asus,
    // 6 November) -- --version was taken as Nexus's flag. Unknown
    // commands are now named at once, with the closest real one.
    if (!arguments.empty() && !arguments[0].empty() && arguments[0][0] != '-') {
        static const std::vector<std::string> kCommands = {
            "app", "conflicts", "container", "cyber", "cybersecurity", "dev", "doctor",
            "dualboot", "gamecheck", "gaming", "gaps", "guide", "hardware", "help",
            "history", "image", "inspect", "install", "language", "largest",
            "options", "plan", "profile", "rebase", "remove", "rollback", "scan", "secure-boot", "security",
            "secureboot", "server", "services", "setup", "solve", "source", "specs",
            "update", "vpn", "what-provides", "why"};
        const std::string& first = arguments[0];
        if (std::find(kCommands.begin(), kCommands.end(), first) == kCommands.end()) {
            std::cerr << "Unknown command: " << first << "\n";
            const auto near = nexus::system::spellingSuggestions(kCommands, first, 1);
            if (!near.empty()) {
                std::string fixed = "nexus " + near.front();
                for (std::size_t i = 1; i < arguments.size(); ++i) fixed += " " + arguments[i];
                std::cerr << "Did you mean " << near.front() << "?\n\n    " << fixed << "\n";
            } else {
                std::cerr << "\n    nexus help     every command\n";
            }
            return 2;
        }
    }

    // `nexus cyber run <tool> ...`: everything after the tool is the
    // tool's, not Nexus's -- `nexus cyber run nmap --version` is nmap's
    // version, and -h is nmap's help.
    if (arguments.size() >= 2 &&
        (arguments[0] == "cyber" || arguments[0] == "cybersecurity") &&
        arguments[1] == "run") {
        return commandCyber(
            std::vector<std::string>(arguments.begin() + 1, arguments.end()),
            false, invocation);
    }

    for (std::size_t index = 0; index < arguments.size(); ++index) {
        if (arguments[index] == "--status" && index + 1 < arguments.size()) {
            statusPath = arguments[index + 1];
            index += 1;
            continue;
        }

        static const std::vector<std::pair<std::string,
                                           std::string*>> vpnFlags{
            {"--interface", &vpnInterface},
            {"--address", &vpnAddress},
            {"--peer-key", &vpnPeerKey},
            {"--endpoint", &vpnEndpoint},
            {"--routes", &vpnRoutes},
            {"--dns", &vpnDns},
            {"--config", &vpnPath},
        };

        bool tookVpnFlag = false;

        for (const auto& [flag, target] : vpnFlags) {
            if (arguments[index] == flag &&
                index + 1 < arguments.size()) {
                *target = arguments[index + 1];
                index += 1;
                tookVpnFlag = true;
                break;
            }
        }

        if (tookVpnFlag) {
            continue;
        }

        if (arguments[index] == "--keepalive" &&
            index + 1 < arguments.size()) {
            vpnKeepalive = std::atoi(arguments[index + 1].c_str());
            index += 1;
            continue;
        }

        if (arguments[index] == "--commands") {
            showCommands = true;
            continue;
        }

        if (arguments[index] == "--unused") {
            showUnused = true;
            continue;
        }

        if (arguments[index] == "--all") {
            showAll = true;
            continue;
        }

        if (arguments[index] == "--yes") {
            assumeYes = true;
            continue;
        }

        if (arguments[index] == "--apply") {
            apply = true;
            continue;
        }

        if (arguments[index] == "--explain") {
            explain = true;
            continue;
        }

        if (arguments[index] == "--rpm") {
            forceRpm = true;
            continue;
        }

        if (arguments[index] == "--dpkg") {
            forceDpkg = true;
            continue;
        }

        if (arguments[index] == "--rpm-cache" &&
            index + 1 < arguments.size()) {
            rpmCache = arguments[index + 1];
            index += 1;
            continue;
        }

        if (arguments[index] == "--rpm-root" &&
            index + 1 < arguments.size()) {
            rpmRoot = arguments[index + 1];
            index += 1;
            continue;
        }

        if (arguments[index] == "--from" &&
            index + 1 < arguments.size()) {
            const std::string named = arguments[index + 1];

            if (named == "base") {
                onlySource = nexus::Source::Base;
            } else if (named == "flatpak") {
                onlySource = nexus::Source::Flatpak;
                withFlatpak = true;
            } else if (named == "snap") {
                onlySource = nexus::Source::Snap;
                withSnap = true;
            } else if (named == "container") {
                onlySource = nexus::Source::Container;
            } else if (named == "nix") {
                onlySource = nexus::Source::Nix;
                withNix = true;
            } else {
                std::cerr
                    << "Unknown source: " << named << "\n"
                    << "Known: base, flatpak, snap, container, nix\n";
                return 2;
            }

            index += 1;
            continue;
        }

        if (arguments[index] == "--from-distro" &&
            index + 1 < arguments.size()) {
            fromDistribution = arguments[index + 1];
            index += 1;
            continue;
        }

        if (arguments[index] == "--with-portage" &&
            index + 1 < arguments.size()) {
            portagePath = arguments[index + 1];
            index += 1;
            continue;
        }

        if (arguments[index] == "--with-arch" &&
            index + 1 < arguments.size()) {
            archDatabases.push_back(arguments[index + 1]);
            index += 1;
            continue;
        }

        if (arguments[index] == "--with-nix") {
            withNix = true;
            continue;
        }

        if (arguments[index] == "--version" ||
            arguments[index] == "-V") {

            // Defined by the build. A binary built without them says
            // so rather than printing an empty string and looking
            // like a version that exists.
            std::cout
                << "nexus " << NEXUS_VERSION
                << " (" << NEXUS_COMMIT << ")\n";

            return 0;
        }

        if (arguments[index] == "--with-snap") {
            withSnap = true;
            continue;
        }

        if (arguments[index] == "--with-flatpak") {
            withFlatpak = true;
            continue;
        }

        if (arguments[index] == "--with-available") {
            withAvailable = true;
            continue;
        }

        if (arguments[index] == "--states" && index + 1 < arguments.size()) {
            statesPath = arguments[index + 1];
            index += 1;
            continue;
        }

        if (arguments[index] == "--sysfs" && index + 1 < arguments.size()) {
            sysfsRoot = arguments[index + 1];
            index += 1;
            continue;
        }

        if (arguments[index] == "--lists" && index + 1 < arguments.size()) {
            listsDir = arguments[index + 1];
            withAvailable = true;
            index += 1;
            continue;
        }

        if (arguments[index] == "--profiles" &&
            index + 1 < arguments.size()) {
            profileDir = arguments[index + 1];
            index += 1;
            continue;
        }

        if (arguments[index] == "--arch" && index + 1 < arguments.size()) {
            arch = arguments[index + 1];
            index += 1;
            continue;
        }

        if (arguments[index] == "--prefer" && index + 1 < arguments.size()) {
            prefer = arguments[index + 1];
            index += 1;
            continue;
        }

        if (arguments[index] == "--require" && index + 1 < arguments.size()) {
            require = arguments[index + 1];
            index += 1;
            continue;
        }

        positional.push_back(arguments[index]);
    }

    if (positional.empty() && arguments.empty()) {
        return commandSummary();
    }

    if (positional.empty() ||
        positional[0] == "-h" ||
        positional[0] == "--help") {

        printUsage();
        return 0;
    }

    const std::string command = positional[0];

    // Needs no package database: answered before the (slow) read of
    // every installed and available package.
    if (command == "gamecheck") {
        return commandGamecheck(
            std::vector<std::string>(positional.begin() + 1,
                                     positional.end()));
    }

    if (command == "update") {
        return commandUpdate(
            std::vector<std::string>(positional.begin() + 1,
                                     positional.end()),
            apply, assumeYes, invocation);
    }

    if (command == "app") {
        return commandApp(
            std::vector<std::string>(positional.begin() + 1,
                                     positional.end()),
            apply, assumeYes);
    }

    if (command == "server") {
        return commandServer(
            std::vector<std::string>(positional.begin() + 1,
                                     positional.end()),
            apply, invocation);
    }

    if (command == "security") {
        return commandSecurity(
            std::vector<std::string>(positional.begin() + 1, positional.end()),
            apply, invocation);
    }
    if (command == "rollback") {
        return commandRollback(apply, invocation);
    }
    if (command == "rebase") {
        return commandRebase(
            std::vector<std::string>(positional.begin() + 1, positional.end()),
            apply, invocation);
    }

    if (command == "dev") {
        return commandDev(
            std::vector<std::string>(positional.begin() + 1,
                                     positional.end()),
            apply, invocation);
    }

    if (command == "cyber" || command == "cybersecurity") {
        return commandCyber(
            std::vector<std::string>(positional.begin() + 1,
                                     positional.end()),
            apply, invocation);
    }

    if (command == "gaming") {
        return commandGaming(
            std::vector<std::string>(positional.begin() + 1,
                                     positional.end()),
            apply, invocation);
    }

    if (command == "dualboot") {
        return commandDualboot(
            std::vector<std::string>(positional.begin() + 1,
                                     positional.end()),
            apply, assumeYes, invocation);
    }

    if (command == "language") {
        return commandLanguage(
            std::vector<std::string>(positional.begin() + 1,
                                     positional.end()),
            apply, assumeYes, invocation);
    }

    // A misspelt or missing add-on is answered before every package in
    // every repository is read -- that read takes a while, and its
    // "Loaded 79339 available packages" line then sat on top of an
    // error that had nothing to do with it (VM, 3 October).
    if (command == "setup") {
        const std::string chosen = setupChoice(positional);

        if (!chosen.empty()) {
            const auto loaded =
                nexus::system::parseProfileDirectory(profileDir);
            nexus::Composition composition;

            if (!loaded.profiles.empty() &&
                !findComposition(loaded.profiles, chosen, composition)) {
                return 1;
            }
        }
    }

    // These describe a machine other than this one, and none of them
    // can be answered from the installed set alone.
    //
    // setup is the one that was missed: it exists to install things
    // that are not here, so without the archive every gap reads as
    // "nothing available provides this" and the machine looks
    // unimprovable rather than unconfigured.
    //
    // Installing means installing something not here yet. Generating
    // an image list means describing a machine that does not exist
    // yet -- and resolving that against what happens to be on this
    // one produced a "minimal" profile listing a full desktop's file
    // manager, because that is what the generating machine ran.
    //
    // Making the archive optional here only ever produces a confusing
    // failure or a quietly wrong answer.
    if (command == "install" || command == "image" ||
        command == "setup" || command == "source") {
        withAvailable = true;
    }
    const std::string argument =
        positional.size() > 1 ? positional[1] : std::string{};

    try {
        // Not every system has a package database, and not every
        // command needs one. `nexus hardware` and `nexus profile list`
        // are perfectly meaningful on a machine that has never heard
        // of dpkg -- which includes the Fedora image Nexus ships in.
        //
        // So a missing database is a fact to report, not a reason to
        // refuse to start.
        // Which ecosystem this machine uses is a fact to detect, not
        // a flag to demand. dpkg wins when its database is present
        // because a system with both is a Debian one with rpm
        // installed as a tool.
        SourceSummary result;
        bool havePackages = false;

        gUseRpmVersions = forceRpm ||
            (!forceDpkg &&
             !std::filesystem::exists(statusPath) &&
             nexus::system::RpmDatabase::available());

        const bool preferRpm =
            forceRpm ||
            (!forceDpkg &&
             !std::filesystem::exists(statusPath) &&
             nexus::system::RpmDatabase::available());

        if (preferRpm) {
            const nexus::system::RpmDatabase database(rpmRoot);
            const auto loaded = database.load();

            if (loaded.error.empty()) {
                result.name = "rpm database";
                result.components = loaded.components;
                result.recordsRead = loaded.packagesRead;
                result.incomplete = loaded.incomplete;
                havePackages = true;

                // Said once, here, for every command: anything that
                // reasons about what is missing is reasoning from a
                // list that is itself missing something.
                if (!loaded.incomplete.empty()) {
                    std::cerr
                        << "Warning: the rpm database was not read "
                           "completely (" << loaded.incomplete << ").\n"
                        << "Anything reported as missing may be missing "
                           "from the read, not from the machine.\n";
                }

                for (const Component& component : result.components) {
                    const auto count = component.requirements().size();

                    result.dependencyClauses += count;
                    result.representableClauses += count;
                }

                if (loaded.fileProvides > 0) {
                    result.notes.push_back(
                        std::to_string(loaded.fileProvides) +
                        " file path(s) are provided as capabilities: "
                        "rpm lets a requirement name a file."
                    );
                }

                if (loaded.booleanRequirements > 0) {
                    result.gaps["boolean-dependency"] =
                        loaded.booleanRequirements;
                }

                if (loaded.rpmlibRequirements > 0) {
                    result.notes.push_back(
                        std::to_string(loaded.rpmlibRequirements) +
                        " rpmlib requirement(s) were skipped: they "
                        "are satisfied by rpm itself, never by a "
                        "package."
                    );
                }
            }
        } else {
            try {
                const nexus::system::DpkgSource source(statusPath);
                const auto loaded = source.load();

                result.name = "dpkg status";
                result.components = loaded.components;
                result.recordsRead = loaded.stanzasRead;
                result.recordsSkipped = loaded.stanzasSkipped;
                result.dependencyClauses = loaded.dependencyClauses;
                result.representableClauses =
                    loaded.representableClauses;

                for (const auto& gap : loaded.gaps) {
                    result.gaps[toString(gap.kind)] += 1;
                }

                havePackages = true;
            } catch (const std::exception& error) {
                result = SourceSummary{};
            }
        }

        // Profiles name intent; the alias table says what this
        // ecosystem calls it. Without one, every profile is written
        // for whichever distribution its author had.
        nexus::AliasTable aliases;

        {
            const std::string file =
                aliasDir + "/" +
                (preferRpm ? "rpm.aliases" : "dpkg.aliases");

            const auto loaded =
                nexus::system::parseAliasFile(file);

            for (const std::string& problem : loaded.problems) {
                std::cerr << "Warning: " << problem << "\n";
            }

            aliases = loaded.table;
        }

        // Each additional source brings its own vocabulary, loaded
        // alongside the ecosystem's own rather than instead of it, so
        // a capability offered from several places shows options from
        // all of them.
        //
        // Reading a source's packages without its aliases loads the
        // components and offers none of them, which looks exactly
        // like the source being empty.
        if (!archDatabases.empty()) {
            const auto loaded = nexus::system::parseAliasFile(
                aliasDir + "/arch.aliases");

            for (const std::string& problem : loaded.problems) {
                std::cerr << "Warning: " << problem << "\n";
            }

            aliases.merge(loaded.table);
        }

        if (withFlatpak) {
            const auto loaded = nexus::system::parseAliasFile(
                aliasDir + "/flatpak.aliases");

            for (const std::string& problem : loaded.problems) {
                std::cerr << "Warning: " << problem << "\n";
            }

            aliases.merge(loaded.table);
        }

        // What the distribution says must never be removed. Read
        // from its own configuration rather than encoded here, so the
        // policy stays whatever the distribution decided.
        const auto protectionRules =
            nexus::system::readProtectionRules(aptConfigDir);

        const std::string runningKernel =
            nexus::system::runningKernelRelease();

        // Commands that read the package database say so plainly when
        // there is not one, rather than failing on a file path the
        // user never mentioned.
        const auto requirePackages = [&]() {
            std::cerr
                << "No package database found.\n"
                << "Looked for dpkg at " << statusPath
                << " and for an rpm database, so \""
                << command << "\" has nothing to read.\n"
                << "Commands that work anywhere: hardware, "
                << "profile list, profile show.\n";
        };

        // Detected hardware joins the universe as a component, so a
        // profile can require "gpu-vendor:amd" and have it resolved
        // exactly like any other capability.
        const nexus::hardware::HardwareDetector detector(sysfsRoot);
        const nexus::hardware::HardwareInfo hardware = detector.detect();

        const nexus::hardware::EncryptionReport encryption =
            nexus::hardware::EncryptionDetector(sysfsRoot).detect();

        // Hardware belongs in the installed baseline too: the GPU in
        // this machine is not something you install, it is something
        // that is already here.
        std::vector<Component> installed = result.components;
        installed.push_back(nexus::hardware::asComponent(hardware));

        const std::set<std::string> protectedIds =
            nexus::system::protectedComponents(
                installed, protectionRules, runningKernel);

        std::vector<Component> universe = installed;

        // Kept separate so that image generation can resolve against
        // what exists rather than what is here.
        std::vector<Component> availableOnly;

        bool haveAvailable = false;

        // An Arch database read on a machine that is not Arch: the
        // packages are real and reachable, through a container rather
        // than by installing them here.
        for (const std::string& database : archDatabases) {
            const auto arch =
                nexus::system::readPacmanDatabase(database);

            if (!arch.error.empty()) {
                std::cerr << "Warning: " << arch.error << "\n";
                continue;
            }

            for (const Component& component : arch.components) {
                universe.push_back(component);
                availableOnly.push_back(component);
            }

            haveAvailable = true;

            std::cerr
                << "Loaded " << arch.components.size()
                << " Arch package(s) from " << database << ".\n";
        }

        // Gentoo's tree, read wherever it is. On Gentoo that is
        // /var/db/repos/gentoo/metadata/md5-cache; anywhere else it
        // is a clone, which is how this was built and tested.
        if (!portagePath.empty()) {
            const auto portage =
                nexus::system::readPortageTree(portagePath);

            if (!portage.error.empty()) {
                std::cerr << "Warning: " << portage.error << "\n";
            } else {
                for (const Component& component :
                     portage.components) {

                    universe.push_back(component);
                    availableOnly.push_back(component);
                }

                haveAvailable = true;

                std::cerr
                    << "Loaded " << portage.components.size()
                    << " portage package(s) from "
                    << portage.packagesRead << " ebuild(s); "
                    << portage.conditional
                    << " condition(s) assumed from USE defaults";

                if (portage.liveEbuilds > 0) {
                    std::cerr
                        << ", " << portage.liveEbuilds
                        << " live ebuild(s) skipped";
                }

                std::cerr << ".\n";
            }
        }

        if (withNix) {
            const auto loadedNix = nexus::system::parseAliasFile(
                aliasDir + "/nix.aliases");

            for (const std::string& problem : loadedNix.problems) {
                std::cerr << "Warning: " << problem << "\n";
            }

            aliases.merge(loadedNix.table);

            const auto nix = nexus::system::readNixProfile();

            if (!nix.error.empty()) {
                std::cerr << "Warning: " << nix.error << "\n";
            } else {
                for (const Component& component : nix.components) {
                    universe.push_back(component);
                    availableOnly.push_back(component);
                }

                if (nix.installed > 0) {
                    haveAvailable = true;
                }

                std::cerr
                    << "Loaded " << nix.installed
                    << " Nix package(s).\n";
            }
        }

        if (withSnap) {
            const auto snaps = nexus::system::readSnaps();

            if (!snaps.error.empty()) {
                std::cerr << "Warning: " << snaps.error << "\n";
            } else {
                for (const Component& component : snaps.components) {
                    universe.push_back(component);
                    availableOnly.push_back(component);
                }

                haveAvailable = true;

                std::cerr
                    << "Loaded " << snaps.installed
                    << " snap(s) and " << snaps.infrastructure
                    << " base(s).\n";
            }

            const auto loaded = nexus::system::parseAliasFile(
                aliasDir + "/snap.aliases");

            for (const std::string& problem : loaded.problems) {
                std::cerr << "Warning: " << problem << "\n";
            }

            aliases.merge(loaded.table);
        }

        if (withFlatpak) {
            const nexus::system::FlatpakSource flatpak;
            const auto offered = flatpak.load();

            if (!offered.error.empty()) {
                std::cerr
                    << "Warning: " << offered.error << "\n";
            } else {
                for (const Component& component : offered.components) {
                    universe.push_back(component);
                    availableOnly.push_back(component);
                }

                haveAvailable = true;

                std::cerr
                    << "Loaded " << offered.components.size()
                    << " Flatpak application(s).\n";
            }
        }

        if (withAvailable && preferRpm) {
            const nexus::system::RpmRepository repositories(rpmCache);
            auto available = repositories.load();

            for (const auto& skipped : available.skipped) {
                std::cerr
                    << "Warning: skipped " << skipped.path
                    << " (" << skipped.reason << ")\n";
            }

            if (!available.components.empty()) {
                universe = nexus::system::mergeAvailable(
                    universe, available.components,
                    versionComparator());

                // Moved, not copied. This is every package in every
                // repository -- the largest thing the process holds,
                // and it was held twice for no reason. The merge is
                // done with it by this point.
                // Counted before the move below, which leaves the
                // vector empty. Reading its size afterwards reported
                // zero packages for a load that had in fact worked.
                const std::size_t loadedCount =
                    available.components.size();

                availableOnly = std::move(available.components);

                haveAvailable = true;

                std::cerr
                    << "Loaded " << loadedCount
                    << " available packages from "
                    << available.repositoriesRead.size()
                    << " repository(ies).\n";
            } else {
                // Say so, the way the apt branch does.
                //
                // Reading nothing and reporting nothing is the failure
                // this project keeps meeting: every later "nothing
                // available provides this" is then confidently wrong
                // about the machine, with no hint why. On a fresh
                // install the cache really is empty, and the honest
                // answer is that Nexus cannot see any packages yet --
                // not that none exist.
                // Fetched here when it can be (0.1.63), instead of
                // telling the person to run dnf themselves (the Asus,
                // 1 November: `nexus install` gave up on a machine
                // whose lists had simply never been downloaded).
                // Fetching lists changes no part of the system -- it
                // is dnf's cache -- so it needs no --apply. Only for
                // commands that are about to offer packages, and only
                // as root: dnf writes that cache as root, and reads
                // that need no root stay that way.
                const bool offers = command == "install" ||
                                    command == "setup" ||
                                    command == "options" ||
                                    command == "source";
                bool fetched = false;
                if (offers && ::geteuid() == 0 &&
                    nexus::system::commandExists("dnf")) {
                    std::cerr << "Fetching the package lists (dnf makecache); "
                                 "this takes a moment.\n" << std::flush;
                    const int rc = std::system(nexus::system::inPlainLocale(
                        "dnf makecache -q").c_str());
                    if (rc == 0) {
                        auto again = repositories.load();
                        if (!again.components.empty()) {
                            const std::size_t loadedCount =
                                again.components.size();
                            universe = nexus::system::mergeAvailable(
                                universe, again.components,
                                versionComparator());
                            availableOnly = std::move(again.components);
                            haveAvailable = true;
                            fetched = true;
                            std::cerr << "Loaded " << loadedCount
                                      << " available packages.\n";
                        }
                    }
                }
                if (!fetched) {
                    std::cerr
                        << "No package lists here yet, so Nexus can only see "
                           "what is installed.\n";
                    if (::geteuid() != 0 && offers) {
                        std::cerr << "Run it with sudo and it fetches them "
                                     "first (nothing is installed without "
                                     "--apply).\n";
                    } else {
                        std::cerr << "Fetch them: sudo dnf makecache\n";
                    }
                }
            }
        } else if (withAvailable) {
            const nexus::system::AptSource apt(listsDir);
            auto available = apt.load();

            for (const auto& skipped : available.filesSkipped) {
                std::cerr
                    << "Warning: skipped " << skipped.path
                    << " (" << skipped.reason << ")\n";
            }

            if (!available.components.empty()) {
                universe = nexus::system::mergeAvailable(
                    universe, available.components,
                    versionComparator());

                // Moved, not copied. This is every package in every
                // repository -- the largest thing the process holds,
                // and it was held twice for no reason. The merge is
                // done with it by this point.
                // Counted before the move below, which leaves the
                // vector empty. Reading its size afterwards reported
                // zero packages for a load that had in fact worked.
                const std::size_t loadedCount =
                    available.components.size();

                availableOnly = std::move(available.components);

                haveAvailable = true;

                std::cerr
                    << "Loaded " << loadedCount
                    << " available packages from "
                    << available.filesRead.size() << " index file(s).\n";
            } else {
                std::cerr
                    << "No package indexes read from " << listsDir
                    << "; continuing with installed packages only.\n";
            }
        }

        if (command == "scan") {
            if (!havePackages) {
                requirePackages();
                return 1;
            }

            return commandScan(result);
        }

        if (command == "what-provides") {
            if (!havePackages) {
                requirePackages();
                return 1;
            }

            if (argument.empty()) {
                std::cerr << "what-provides requires a capability name.\n";
                return 2;
            }

            return commandWhatProvides(result.components, argument);
        }

        if (command == "why") {
            if (!havePackages) {
                requirePackages();
                return 1;
            }

            if (argument.empty()) {
                std::cerr << "why requires a component id.\n";
                return 2;
            }

            return commandWhy(result.components, argument);
        }

        if (command == "inspect") {
            if (!havePackages) {
                requirePackages();
                return 1;
            }

            if (argument.empty()) {
                std::cerr << "inspect requires a component id.\n";
                return 2;
            }

            return commandInspect(result.components, argument);
        }

        if (command == "image") {
            if (argument.empty()) {
                std::cerr << "image requires a profile name.\n";
                return 2;
            }

            if (availableOnly.empty()) {
                std::cerr
                    << "No package archive could be read, so a "
                    << "generated list would\ndescribe this machine "
                    << "rather than the profile.\n";

                return 1;
            }

            return commandImage(
                aliases, availableOnly, argument, profileDir, arch);
        }

        gAvailableLoaded = haveAvailable;
        if (command == "install") {
            if (argument.empty()) {
                std::cerr << "install requires a capability name.\n";
                return 2;
            }

            return commandInstall(
                aliases, universe, installed, argument, arch,
                preferRpm, apply, assumeYes, showCommands,
                invocation, profileDir, explain);
        }

        if (command == "options") {
            if (argument.empty()) {
                std::cerr << "options requires a capability name.\n";
                return 2;
            }

            return commandOptions(
                aliases, universe, installed, argument, arch,
                onlySource);
        }

        if (command == "plan") {
            if (!havePackages) {
                requirePackages();
                return 1;
            }

            if (argument.empty()) {
                std::cerr << "plan requires a capability name.\n";
                return 2;
            }

            return commandPlan(universe, argument, arch, explain);
        }

        if (command == "remove") {
            if (!havePackages) {
                requirePackages();
                return 1;
            }

            if (argument.empty()) {
                std::cerr << "remove requires a component name.\n";
                return 2;
            }

            return commandRemove(
                installed, argument, statesPath, arch, protectedIds,
                apply, assumeYes, preferRpm, showCommands,
                invocation);
        }

        if (command == "history") {
            return commandHistory(
                nexus::system::defaultTransactionLog());
        }

        if (command == "doctor") {
            std::set<std::string> roots;

            const auto automatic =
                nexus::system::readAutoInstalled(statesPath);

            for (const Component& component : installed) {
                const std::string key =
                    component.name() + ":" + component.architecture();

                if (automatic.count(key) == 0) {
                    roots.insert(component.id());
                }
            }

            if (automatic.empty()) {
                roots.clear();
            }

            return commandDoctor(
                result, installed, hardware, encryption, roots,
                havePackages, protectedIds);
        }

        if (command == "vpn") {
            nexus::system::TunnelSettings settings;

            settings.interfaceName = vpnInterface;
            settings.address = vpnAddress;
            settings.peerPublicKey = vpnPeerKey;
            settings.endpoint = vpnEndpoint;

            if (!vpnRoutes.empty()) {
                settings.allowedIps = vpnRoutes;
            }

            settings.dns = vpnDns;
            settings.keepalive = vpnKeepalive;

            const std::string path =
                vpnPath.empty()
                    ? "/etc/wireguard/" + vpnInterface + ".conf"
                    : vpnPath;

            return commandVpn(
                argument.empty() ? "check" : argument,
                settings, path, showAll, apply);
        }

        if (command == "source") {
            if (argument.empty()) {
                std::cerr << "source requires a package name.\n";
                return 2;
            }
            if (!nexus::system::isPackageName(argument)) {
                std::cerr << "Not a package name: " << argument << "\n";
                return 2;
            }

            if (!havePackages) {
                requirePackages();
                return 1;
            }

            return commandSource(
                universe, installed, argument, arch, showCommands);
        }

        if (command == "guide" || command == "help") {
            return commandGuide(preferRpm);
        }

        if (command == "largest") {
            if (!havePackages) {
                requirePackages();
                return 1;
            }

            const std::size_t howMany =
                argument.empty()
                    ? 15
                    : static_cast<std::size_t>(
                          std::strtoul(argument.c_str(), nullptr, 10));

            std::set<std::string> unusedOnly;

            if (showUnused) {
                std::set<std::string> roots;
                bool knowWhatWasWanted = false;

                if (preferRpm) {
                    // dnf records which packages a person asked for.
                    const auto asked = nexus::system::runCommand(
                        "dnf repoquery --userinstalled --qf "
                        "'%{name}' 2>/dev/null", false);

                    for (const std::string& name : asked.lines) {
                        if (name.empty()) {
                            continue;
                        }

                        for (const Component& component : installed) {
                            if (component.name() == name) {
                                roots.insert(component.id());
                            }
                        }
                    }

                    knowWhatWasWanted = !roots.empty();
                } else {
                    const auto automatic =
                        nexus::system::readAutoInstalled(statesPath);

                    knowWhatWasWanted = !automatic.empty();

                    for (const Component& component : installed) {
                        const std::string key =
                            component.name() + ":" +
                            component.architecture();

                        if (automatic.count(key) == 0) {
                            roots.insert(component.id());
                        }
                    }
                }

                // With no record of what was asked for, nothing can
                // be called unused. Treating that as "everything is
                // unused" listed all 525 components of a base image
                // as removable, which is the same mistake as reading
                // an empty result from broken input.
                if (!knowWhatWasWanted) {
                    std::cerr
                        << "No record of which components were asked "
                        << "for, so nothing\ncan be called unused. "
                        << "Showing everything by size instead.\n\n";

                    showUnused = false;
                } else {
                    const std::set<std::string> reachable =
                        nexus::reachableFrom(
                            installed, roots, buildDetector());

                    for (const Component& component : installed) {
                        if (protectedIds.count(component.id()) > 0) {
                            continue;
                        }

                        if (reachable.count(component.id()) == 0) {
                            unusedOnly.insert(component.id());
                        }
                    }
                }
            }

            return commandLargest(
                result.components, howMany == 0 ? 15 : howMany,
                showUnused, unusedOnly);
        }

        if (command == "services") {
            return commandServices(
                argument == "all" || showAll, preferRpm);
        }

        if (command == "secureboot" || command == "secure-boot") {
            return commandSecureBoot(sysfsRoot);
        }

        if (command == "specs") {
            const bool plain =
                std::find(arguments.begin(), arguments.end(), "--plain") !=
                arguments.end();
            const bool json =
                std::find(arguments.begin(), arguments.end(), "--json") !=
                arguments.end();
            return commandSpecs(sysfsRoot, plain, json);
        }

        if (command == "hardware") {
            return commandHardware(hardware);
        }


        if (command == "container") {
            if (argument.empty()) {
                std::cerr
                    << "container requires a package name.\n"
                    << "    nexus container <package> "
                    << "[--from-distro arch] [--apply]\n";
                return 2;
            }

            return commandContainerInstall(
                universe, argument, fromDistribution, apply,
                assumeYes);
        }

        if (command == "setup") {
            // "school, media" is two words to the shell: "school," and
            // "media". Only the first used to arrive, so a person who
            // typed the list the way they would write it got half of
            // it. Every word after "setup" is part of the list, and
            // commas, spaces and empty entries are all the same
            // separator.
            const std::string chosen = setupChoice(positional);

            return commandSetup(
                aliases, installed, universe, hardware, profileDir,
                arch, chosen, apply, assumeYes, showCommands,
                invocation);
        }

        if (command == "profile") {
            const std::string action =
                positional.size() > 1 ? positional[1] : "list";
            const std::string name =
                positional.size() > 2 ? positional[2] : std::string{};

            if (action == "check" && !havePackages) {
                requirePackages();
                return 1;
            }

            return commandProfile(
                aliases, installed, universe, haveAvailable,
                action, name, profileDir, arch);
        }

        if (command == "solve") {
            if (!havePackages) {
                requirePackages();
                return 1;
            }

            if (argument.empty()) {
                std::cerr << "solve requires a capability name.\n";
                return 2;
            }

            return commandSolve(
                aliases, universe, argument, prefer, require, arch,
                explain);
        }

        if (command == "conflicts") {
            if (!havePackages) {
                requirePackages();
                return 1;
            }

            return commandConflicts(result.components);
        }

        if (command == "gaps") {
            return commandGaps(result);
        }

        std::cerr << "Unknown command: " << command << "\n\n";
        printUsage();
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n";
        return 1;
    }
}
