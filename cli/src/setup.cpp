// nexus setup: add-ons.
//
// Part of the `nexus` tool. Until 0.1.96 this was in cli/src/main.cpp,
// which had grown past 11,000 lines; it was moved here unchanged.

#include "cli.hpp"

namespace nexus_cli {

// The add-ons named after "setup", as one comma-separated list.
// Every word after "setup" is part of it, and commas, spaces and empty
// entries are all the same separator.
std::string setupChoice(const std::vector<std::string>& positional) {
    std::string chosen;
    for (std::size_t word = 1; word < positional.size(); ++word) {
        std::string entry;
        const std::string text = positional[word] + ",";
        for (const char c : text) {
            if (c == ',' || c == ' ' || c == '\t') {
                if (!entry.empty()) {
                    if (!chosen.empty()) {
                        chosen += ",";
                    }
                    chosen += entry;
                    entry.clear();
                }
            } else {
                entry += c;
            }
        }
    }
    return chosen;
}

// Requirements a profile fills from Flathub (Profile::flatpak) are met
// by the installed Flatpak, not by a package. checkProfile only reads
// the package database, so the list in `nexus setup` and `nexus
// profile check` called them missing on a machine that had them: Asus,
// 21 October -- gaming "4 of 11 missing" (Steam, Heroic, Lutris,
// ProtonUp-Qt) and media "1 of 2" (VLC), while choosing them again
// said there was nothing to install. One reading of Flatpak, here, for
// every place that counts.
std::set<std::string> installedFlatpakApps() {
    std::set<std::string> apps;

    if (!nexus::system::commandExists("flatpak")) {
        return apps;
    }

    const auto listed = nexus::system::runCommand(
        nexus::system::inPlainLocale(
            "flatpak list --app --columns=application"), false);

    for (const std::string& line : listed.lines) {
        const auto start = line.find_first_not_of(" \t");
        const auto end = line.find_last_not_of(" \t\r");
        if (start != std::string::npos) {
            apps.insert(line.substr(start, end - start + 1));
        }
    }

    return apps;
}

void countInstalledFlatpaks(const nexus::Profile& profile,
                            nexus::ProfileReport& report,
                            const std::set<std::string>& apps) {
    for (std::size_t index = 0;
         index < profile.requirements.size() && index < report.items.size();
         ++index) {
        auto& item = report.items[index];
        const auto& alternatives = profile.requirements[index].alternatives;

        if (item.satisfied || alternatives.size() != 1) {
            continue;
        }

        const auto found = profile.flatpak.find(alternatives[0].capability);

        if (found != profile.flatpak.end() && apps.count(found->second)) {
            item.satisfied = true;
            item.provided = {found->second + " (Flatpak)"};
            item.blockedOn.clear();
            report.satisfied += 1;
        }
    }
}

namespace {

// The add-on's wallpaper, for the first chosen name that has one.
// First boot did this itself, so a choice made later -- `nexus setup
// ... --apply` or the reminder's "Choose add-ons" -- installed the
// add-on and left the wallpaper as it was (Asus, 21 October). Applied
// at the next login by /usr/libexec/nexus/apply-wallpaper.
void rememberWallpaper(const std::string& chosen) {
    std::string name;
    std::stringstream names(chosen);

    while (std::getline(names, name, ',')) {
        std::string clean;
        for (char c : name) {
            const char lowered = static_cast<char>(
                std::tolower(static_cast<unsigned char>(c)));
            if (std::isalnum(static_cast<unsigned char>(lowered)) ||
                lowered == '-') {
                clean += lowered;
            }
        }

        if (!clean.empty() &&
            std::filesystem::is_directory(
                "/usr/share/wallpapers/nexus-" + clean)) {
            std::error_code ignored;
            std::filesystem::create_directories("/etc/nexus", ignored);
            std::ofstream("/etc/nexus/wallpaper", std::ios::trunc)
                << clean << "\n";
            return;
        }
    }
}

}  // namespace

int commandSetup(
    const nexus::AliasTable& aliases,
    const std::vector<Component>& installed,
    const std::vector<Component>& universe,
    const nexus::hardware::HardwareInfo& hardware,
    const std::string& directory,
    const std::string& architecture,
    const std::string& chosenAsTyped,
    bool apply,
    bool assumeYes,
    bool showCommands,
    const std::string& invocation
) {
    // "security" is "cybersecurity" since 0.1.67, everywhere below
    // (the plan, the history record, the wallpaper).
    std::string chosen;
    {
        std::string item;
        std::stringstream in(chosenAsTyped);
        while (std::getline(in, item, ',')) {
            if (item == "security") item = "cybersecurity";
            chosen += (chosen.empty() ? "" : ",") + item;
        }
    }
    const auto loaded =
        nexus::system::parseProfileDirectory(directory);

    if (loaded.profiles.empty()) {
        std::cerr << "No profiles found in " << directory << ".\n";
        return 1;
    }

    // The name is checked before anything is described. An error
    // that arrives after a paragraph of context nobody asked for
    // reads as though the context mattered.
    nexus::Composition composition;

    if (!chosen.empty()) {
        if (!findComposition(loaded.profiles, chosen, composition)) {
            return 1;
        }

        if (composition.refused) {
            std::cerr << composition.refusal << "\n";
            return 1;
        }
    }

    std::cout << "This machine\n\n";

    for (const std::string& capability : hardware.capabilities()) {
        std::cout << "    " << capability << "\n";
    }

    if (!hardware.unreadable.empty()) {
        for (const std::string& problem : hardware.unreadable) {
            std::cout << "    (" << problem << ")\n";
        }
    }

    std::cout << "\n";

    // What each profile would mean here, before anything is chosen.
    if (chosen.empty()) {
        std::cout << "What would you like this machine to be?\n\n";

        const std::set<std::string> flatpakApps = installedFlatpakApps();

        for (const nexus::Profile& profile : loaded.profiles) {
            // base is not a choice. It is composed into every image
            // (see commandImage), so it is always "already satisfied"
            // and picking it adds nothing -- yet it was listed like
            // every other profile, and read as one of twelve things
            // this machine could be.
            if (profile.name == "base") {
                continue;
            }

            nexus::Profile local = profile;

            if (!architecture.empty()) {
                local.architecture = architecture;
            }

            // Against what is installed, not against what could be.
            // Checking the whole universe reports every profile as
            // satisfied the moment its packages exist in an archive,
            // which is every profile.
            auto report = nexus::checkProfile(
                local, buildSolver(installed), aliases);
            countInstalledFlatpaks(local, report, flatpakApps);

            const std::size_t missing =
                report.items.size() - report.satisfied;

            std::cout
                << "  " << profile.name << "\n"
                << "      " << profile.description << "\n"
                << "      ";

            if (missing == 0) {
                std::cout << "already satisfied";
            } else {
                std::cout
                    << missing << " of " << report.items.size()
                    << " requirement(s) missing";
            }

            if (profile.exclusive) {
                std::cout << "; cannot be combined";
            }

            std::cout << "\n\n";
        }

        std::cout
            << "Choose one, or several separated by commas:\n"
            << "    " << invocation << " <name>[,<name>...]\n"
            << "\nNothing has been changed.\n";

        return 0;
    }

    nexus::Profile profile = composition.profile;

    if (!architecture.empty()) {
        profile.architecture = architecture;
    }

    reportComposition(composition);

    // Two phases, as elsewhere: what is here decides what is
    // satisfied, and only then does the archive say what would fix
    // the rest.
    const auto here = nexus::checkProfile(
        profile, buildSolver(installed), aliases);

    const auto possible = nexus::checkProfile(
        profile, buildSolver(universe), aliases);

    // Requirements this profile fills from Flathub rather than the
    // distribution (Profile::flatpak), by position. Empty for the rest.
    std::vector<std::string> flatpakFor(here.items.size());

    for (std::size_t index = 0;
         index < profile.requirements.size() && index < flatpakFor.size();
         ++index) {
        const auto& alternatives = profile.requirements[index].alternatives;

        if (alternatives.size() != 1) {
            continue;
        }

        const auto found = profile.flatpak.find(alternatives[0].capability);

        if (found != profile.flatpak.end()) {
            flatpakFor[index] = found->second;
        }
    }

    const auto flatpakInstalled = [](const std::string& application) {
        return nexus::system::commandExists("flatpak") &&
               nexus::system::runCommand(
                   "flatpak info --system " + shellQuote(application) +
                   " > /dev/null 2>&1", false).ok;
    };

    std::size_t satisfied = here.satisfied;

    for (std::size_t index = 0; index < here.items.size(); ++index) {
        if (!here.items[index].satisfied && !flatpakFor[index].empty() &&
            flatpakInstalled(flatpakFor[index])) {
            satisfied += 1;
        }
    }

    std::cout
        << "Chosen:      " << chosen << "\n"
        << "Satisfied:   " << satisfied << " of "
        << here.items.size() << "\n\n";

    std::vector<std::string> wanted;
    std::vector<std::string> wantedFlatpaks;
    std::size_t unavailable = 0;

    for (std::size_t index = 0; index < here.items.size(); ++index) {
        const auto& item = here.items[index];

        if (item.satisfied) {
            continue;
        }

        if (!flatpakFor[index].empty()) {
            if (flatpakInstalled(flatpakFor[index])) {
                continue;
            }

            std::cout
                << "  [install] " << item.requirement
                << "\n            " << flatpakFor[index]
                << "  (Flatpak, from Flathub)\n";

            wantedFlatpaks.push_back(flatpakFor[index]);
            continue;
        }

        const bool available =
            index < possible.items.size() &&
            possible.items[index].satisfied &&
            !possible.items[index].provided.empty();

        if (!available) {
            unavailable += 1;

            std::cout
                << "  [missing] " << item.requirement
                << "\n            nothing available provides this\n";
            continue;
        }

        const std::string& selected =
            possible.items[index].provided.front();

        // The package name, not the internal id. Ids carry an
        // architecture suffix so multilib builds stay distinct in the
        // resolver, and "gamemode:amd64" is not something apt will
        // accept.
        std::string name = selected;

        for (const Component& component : universe) {
            if (component.id() == selected) {
                name = component.name();
                break;
            }
        }

        std::cout
            << "  [install] " << item.requirement
            << "\n            " << name << "\n";

        wanted.push_back(name);
    }

    const std::size_t total = wanted.size() + wantedFlatpaks.size();

    if (total == 0) {
        // Nothing to install is two different situations, and calling
        // both of them success told somebody their machine was
        // already what they asked for while six requirements went
        // unmet.
        if (unavailable > 0) {
            std::cout
                << "\n" << unavailable
                << " requirement(s) cannot be met from what is "
                << "available.\nNothing has been changed.\n";

            return 1;
        }

        std::cout
            << "\nThis machine already is what you asked for.\n";

        // Chosen and already here is still a choice: the reminder has
        // nothing left to say, and the add-on's wallpaper follows.
        if (apply && nexus::system::haveRootPrivileges()) {
            std::error_code ignored;
            std::filesystem::remove("/var/lib/nexus/setup-not-chosen",
                                    ignored);
            rememberWallpaper(chosen);
        }

        return 0;
    }

    if (showCommands) {
        std::cout
            << "\nWhat this would run:\n\n";

        for (const std::string& name : wanted) {
            std::cout
                << "    sudo "
                << (gUseRpmVersions ? "dnf" : "apt-get")
                << " install "
                << (gUseRpmVersions
                        ? "--setopt=install_weak_deps=False "
                        : "--no-install-recommends ")
                << name << "\n";
        }

        for (const std::string& application : wantedFlatpaks) {
            std::cout
                << "    sudo flatpak install --system flathub "
                << application << "\n";
        }

        std::cout
            << "\nOne at a time on purpose: if the fourth fails the "
            << "first three\nstill happened, and you can see which.\n";

        return 0;
    }

    if (!apply) {
        std::cout
            << "\n" << total
            << " thing(s) would be installed.\n"
            << "Use --apply to do it, or --commands to see what it "
            << "would run.\n";

        return 0;
    }

    if (!nexus::system::haveRootPrivileges()) {
        // The invocation already carries the profile and the flags.
        // Appending them again produced a suggestion that named the
        // profile twice and --apply twice.
        std::cout
            << "\nInstalling needs root. Re-run with sudo:\n"
            << "    sudo " << invocation
            << "\n\nNothing has been changed.\n";

        return 1;
    }

    if (!assumeYes) {
        std::cout
            << "\nInstall " << total
            << " thing(s)? Each is shown in full and asked again before"
               " it is installed. [y/N] ";

        std::string answer;

        std::getline(std::cin, answer);

        if (answer != "y" && answer != "Y" && answer != "yes") {
            std::cout << "Nothing has been changed.\n";
            return 1;
        }
    }

    // One at a time, each verified before it is applied. A setup that
    // half-succeeds should leave the parts that worked, and say which
    // ones did not.
    std::size_t done = 0;
    std::vector<std::string> failed;

    for (const std::string& name : wanted) {
        std::cout << "\n=== " << name << " ===\n";

        // Each item asks on its own, with its full list (0.1.91,
        // round-4 audit M4): "Install 5 things?" approves five names,
        // not the packages each brings. -y given to setup is given to
        // each item too.
        const int status = commandInstall(
            aliases, universe, installed, name, architecture,
            gUseRpmVersions, true, assumeYes, false, invocation,
            std::string{}, false);

        if (status == 0) {
            done += 1;
        } else {
            failed.push_back(name);
        }
    }

    // Flatpaks after the packages: a system-wide Flatpak goes into
    // /var, not into the deployment, so it is usable straight away
    // rather than after the reboot the packages wait for.
    //
    // Flathub is added from the copy the image carries
    // (/usr/share/nexus/flathub.flatpakrepo, fetched when the image is
    // built), so the key it is verified with came with the system
    // rather than from whatever answered on the network today.
    for (const std::string& application : wantedFlatpaks) {
        std::cout << "\n=== " << application << " (Flatpak) ===\n";

        if (!nexus::system::commandExists("flatpak")) {
            std::cout << "flatpak is not installed on this system.\n";
            failed.push_back(application);
            continue;
        }

        const std::string remote =
            std::filesystem::exists("/usr/share/nexus/flathub.flatpakrepo")
                ? "/usr/share/nexus/flathub.flatpakrepo"
                : "https://dl.flathub.org/repo/flathub.flatpakrepo";

        nexus::system::runCommand(
            "flatpak remote-add --system --if-not-exists flathub " +
            shellQuote(remote));

        // Not captured: the first Flatpak brings its runtime, a
        // download of a gigabyte or more, and minutes with nothing on
        // screen look like a hang.
        std::cout << std::flush;

        const int status = std::system(nexus::system::inPlainLocale(
            "flatpak install --system --noninteractive flathub " +
            shellQuote(application)).c_str());

        if (status == 0) {
            done += 1;
        } else {
            std::cout << "\nflatpak did not complete.\n";
            failed.push_back(application);
        }
    }

    // Something was chosen, so the one-time "add-ons were not chosen"
    // reminder (image/first-boot/nexus-setup-reminder) has nothing left
    // to say. First boot and the reminder's own button remove it; a
    // `sudo nexus setup x --apply` typed by hand did not (Asus, 20
    // October: gaming and media installed, reminder still armed).
    if (done > 0) {
        std::error_code ignored;
        std::filesystem::remove("/var/lib/nexus/setup-not-chosen", ignored);
    }

    rememberWallpaper(chosen);

    std::cout
        << "\n" << done << " of " << total
        << " installed.\n";

    if (!failed.empty()) {
        std::cout << "\nDid not install:\n";

        for (const std::string& name : failed) {
            std::cout << "    " << name << "\n";
        }

        std::cout
            << "\nThe rest of the system is unchanged and usable.\n";

        return 1;
    }

    return 0;
}

}  // namespace nexus_cli
