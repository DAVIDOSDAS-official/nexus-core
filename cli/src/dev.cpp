// nexus dev.
//
// Part of the `nexus` tool. Until 0.1.96 this was in cli/src/main.cpp,
// which had grown past 11,000 lines; it was moved here unchanged.

#include "cli.hpp"

namespace nexus_cli {

// ---------------------------------------------------------------------
// nexus dev (0.1.69): programming languages, each on request, in one
// Distrobox container ("nexus-dev", Fedora's toolbox image for this
// release). Commands are exported to ~/.local/bin, so they work in any
// terminal; the system image is never touched.
int commandDev(
    const std::vector<std::string>& raw,
    bool apply,
    const std::string& invocation
) {
    using namespace nexus::system;
    namespace fs = std::filesystem;

    std::vector<std::string> words;
    for (const auto& w : raw) {
        // "cpp,python,js" and "cpp python js" both work
        std::stringstream in(w);
        std::string part;
        while (std::getline(in, part, ',')) if (!part.empty()) words.push_back(part);
    }
    const std::string action = words.empty() ? "" : words[0];
    const std::string box = kDevContainer;
    const std::string home = std::getenv("HOME") ? std::getenv("HOME") : "";

    if (action == "help") {
        const std::string topic = words.size() > 1 ? words[1] : "";
        const std::string text = topic.empty() ? "" : devHelp(topic);
        if (text.empty()) {
            if (!topic.empty()) std::cout << "No help topic called " << topic << ".\n\n";
            std::cout << "Topics:";
            for (const auto& t : devHelpTopics()) std::cout << " " << t;
            std::cout << "\n\n    nexus dev help git\n";
            return topic.empty() ? 0 : 2;
        }
        std::cout << text;
        return 0;
    }

    auto haveDistrobox = [] { return commandExists("distrobox") && commandExists("podman"); };
    auto boxState = [&] {
        return parseDistroboxList(runCommand("distrobox list --no-color 2>/dev/null", false).text, box);
    };
    // A command in ~/.local/bin that distrobox-export wrote.
    auto exported = [&](const std::string& cmd) {
        std::ifstream in(home + "/.local/bin/" + cmd);
        std::stringstream b; b << in.rdbuf();
        return b.str().find("distrobox") != std::string::npos &&
               b.str().find(box) != std::string::npos;
    };
    // On the system itself (not one of our exports).
    auto onSystem = [&](const std::string& cmd) {
        std::error_code ec;
        return fs::exists("/usr/bin/" + cmd, ec) || fs::exists("/usr/local/bin/" + cmd, ec);
    };

    if (::geteuid() == 0 && action != "" && action != "status") {
        std::cout << "Run this without sudo: the dev box belongs to you, not to root.\n";
        return 1;
    }

    if (action.empty() || action == "status") {
        std::cout << "Programming languages -- each on request, in a container, kept out of the system.\n\n";
        for (const auto& l : devLanguages()) {
            const std::string& main = l.commands.front();
            std::string where = "-";
            if (exported(main)) where = "in " + box;
            else if (onSystem(main)) where = "on the system";
            std::string name = l.display;
            while (name.size() < 22) name.push_back(' ');
            std::cout << "  " << name << where << "\n";
        }
        if (!haveDistrobox()) {
            std::cout << "\nDistrobox is not installed; it comes with the development add-on:\n"
                         "    sudo nexus setup development --apply\n";
        }
        std::cout << "\nCommands:\n"
                     "  nexus dev add rust,go,js --apply   add languages (cpp python js java rust go ruby)\n"
                     "  nexus dev remove go --apply        take one away again\n"
                     "  nexus dev remove all --apply       delete the whole box\n"
                     "  nexus dev shell                    a shell inside the box (dnf works there)\n"
                     "  nexus dev help <topic>             the commands worth knowing\n";
        return 0;
    }

    if (!haveDistrobox()) {
        std::cout << "Languages go into a Distrobox container, and Distrobox is not installed.\n"
                     "It comes with the development add-on:\n\n"
                     "    sudo nexus setup development --apply\n";
        return 1;
    }

    if (action == "shell") {
        if (!boxState().exists) {
            std::cout << "No dev box yet. Add a language first:\n\n    nexus dev add python --apply\n";
            return 1;
        }
        return std::system(("distrobox enter " + box).c_str()) == 0 ? 0 : 1;
    }

    if (action != "add" && action != "remove") {
        std::cout << "Unknown: nexus dev " << action << "\n    nexus dev   (shows every command)\n";
        return 2;
    }

    // remove all: the box itself
    if (action == "remove" && words.size() == 2 && words[1] == "all") {
        if (!boxState().exists) { std::cout << "There is no dev box.\n"; return 0; }
        std::cout << "Removes " << box << ", every language in it and the commands exported from it.\n"
                     "Your projects (your home folder) are not touched.\n";
        if (!apply) {
            std::cout << "\nNothing has been changed. To do it:\n    " << invocation << " --apply\n";
            return 0;
        }
        TransactionRecord record;
        record.when = currentTimestamp();
        record.request = "dev remove all";
        record.kind = TransactionKind::Remove;
        record.resolved = "distrobox " + box;
        record.packages.insert(box);
        const std::string log = defaultTransactionLog();
        const std::string marker = beginTransaction(log, record);
        const bool ok = std::system(("distrobox rm --force " + box).c_str()) == 0;
        record.outcome = ok ? "removed" : "failed";
        record.succeeded = ok; record.exitCode = ok ? 0 : 1;
        finishTransaction(log, marker, record);
        return ok ? 0 : 1;
    }

    std::vector<DevLanguage> chosen;
    for (std::size_t i = 1; i < words.size(); ++i) {
        const auto l = findDevLanguage(words[i]);
        if (!l) {
            std::cout << "Not a language Nexus knows: " << words[i]
                      << "\nKnown: cpp python js java rust go ruby\n"
                         "(anything else: nexus dev shell, then sudo dnf install ...)\n";
            return 2;
        }
        if (std::none_of(chosen.begin(), chosen.end(), [&](const DevLanguage& c) { return c.key == l->key; }))
            chosen.push_back(*l);
    }
    if (chosen.empty()) {
        std::cout << "Which languages? For example:\n\n    nexus dev " << action << " rust,go --apply\n";
        return 2;
    }

    if (action == "add") {
        std::vector<DevLanguage> todo;
        std::set<std::string> packages;
        for (const auto& l : chosen) {
            // On the system already: a copy in the box would only
            // shadow it in ~/.local/bin.
            if (onSystem(l.commands.front())) {
                std::cout << l.display << ": already on the system"
                          << (l.onSystemWithAddon ? " (development add-on)" : "")
                          << "; nothing to add.\n";
                continue;
            }
            if (exported(l.commands.front())) {
                std::cout << l.display << ": already in " << box << ".\n";
                continue;
            }
            todo.push_back(l);
            packages.insert(l.packages.begin(), l.packages.end());
        }
        if (todo.empty()) return 0;

        std::ifstream osr("/etc/os-release");
        std::stringstream ob; ob << osr.rdbuf();
        const std::string image = devImageFor(ob.str());
        const bool exists = boxState().exists;

        std::cout << "\nInto " << box << (exists ? "" : " (created from " + image + ")") << ":\n";
        for (const auto& l : todo) {
            std::cout << "  " << l.display << "  -> commands:";
            for (const auto& c : l.commands) std::cout << " " << c;
            std::cout << "\n";
        }
        std::cout << "System:  untouched. The commands work in any terminal (~/.local/bin).\n";
        if (!apply) {
            std::cout << "\nNothing has been changed. To do it:\n    " << invocation << " --apply\n";
            return 0;
        }

        TransactionRecord record;
        record.when = currentTimestamp();
        record.request = invocation.find("dev ") == std::string::npos
                         ? invocation : invocation.substr(invocation.find("dev "));
        record.kind = TransactionKind::Install;
        record.resolved = "distrobox " + box;
        for (const auto& l : todo) record.packages.insert(l.key);
        const std::string log = defaultTransactionLog();
        const std::string marker = beginTransaction(log, record);

        bool ok = true;
        if (!exists) {
            std::cout << "\nCreating the dev box (downloads Fedora's toolbox image once).\n" << std::flush;
            ok = std::system(("distrobox create --yes --name " + box + " --image " + shellQuote(pinnedImage(image))).c_str()) == 0;
        }
        if (ok) {
            std::string pk;
            for (const auto& p : packages) pk += " " + shellQuote(p);
            std::cout << "\nInstalling. dnf shows the download size and asks first.\n\n" << std::flush;
            ok = std::system(("distrobox enter " + box + " -- sudo dnf install" + pk).c_str()) == 0;
        }
        if (ok) {
            for (const auto& l : todo) {
                for (const auto& c : l.commands) {
                    // only what the packages really installed
                    const std::string inner =
                        "p=$(command -v " + shellQuote(c) + ") && distrobox-export --bin \"$p\" "
                        "--export-path \"$HOME/.local/bin\" >/dev/null";
                    std::system(("distrobox enter " + box + " -- sh -c " + shellQuote(inner)).c_str());
                }
            }
            std::cout << "\n";
            for (const auto& l : todo) {
                std::cout << l.display << ": " << std::flush;
                std::system(("distrobox enter " + box + " -- sh -c " +
                             shellQuote(l.versionCommand + " 2>&1 | head -1")).c_str());
            }
            std::cout << "\nReady. Open a new terminal if a command is not found yet.\n"
                         "The commands worth knowing:  nexus dev help " << todo.front().key << "\n";
        }
        record.outcome = ok ? "installed" : "failed";
        record.succeeded = ok; record.exitCode = ok ? 0 : 1;
        finishTransaction(log, marker, record);
        if (!ok) std::cout << "\nNot finished. Run the same command again; what is done is kept.\n";
        return ok ? 0 : 1;
    }

    // remove <languages>
    if (!boxState().exists) { std::cout << "There is no dev box, so nothing to remove.\n"; return 0; }
    std::set<std::string> packages;
    for (const auto& l : chosen) packages.insert(l.packages.begin(), l.packages.end());
    std::cout << "From " << box << ":";
    for (const auto& l : chosen) std::cout << " " << l.display << ";";
    std::cout << " their commands leave ~/.local/bin.\n" << std::flush;
    if (!apply) {
        std::cout << "\nNothing has been changed. To do it:\n    " << invocation << " --apply\n";
        return 0;
    }
    TransactionRecord record;
    record.when = currentTimestamp();
    record.request = invocation.find("dev ") == std::string::npos
                         ? invocation : invocation.substr(invocation.find("dev "));
    record.kind = TransactionKind::Remove;
    record.resolved = "distrobox " + box;
    for (const auto& l : chosen) record.packages.insert(l.key);
    const std::string log = defaultTransactionLog();
    const std::string marker = beginTransaction(log, record);
    for (const auto& l : chosen) {
        for (const auto& c : l.commands) {
            if (!exported(c)) continue;
            const std::string inner = "p=$(command -v " + shellQuote(c) + ") && distrobox-export --bin \"$p\" "
                                      "--export-path \"$HOME/.local/bin\" --delete >/dev/null";
            std::system(("distrobox enter " + box + " -- sh -c " + shellQuote(inner)).c_str());
        }
    }
    std::string pk;
    for (const auto& p : packages) pk += " " + shellQuote(p);
    const bool ok = std::system(("distrobox enter " + box + " -- sudo dnf remove" + pk).c_str()) == 0;
    record.outcome = ok ? "removed" : "failed";
    record.succeeded = ok; record.exitCode = ok ? 0 : 1;
    finishTransaction(log, marker, record);
    return ok ? 0 : 1;
}

}  // namespace nexus_cli
