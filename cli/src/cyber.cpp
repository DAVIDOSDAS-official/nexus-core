// nexus cyber.
//
// Part of the `nexus` tool. Until 0.1.96 this was in cli/src/main.cpp,
// which had grown past 11,000 lines; it was moved here unchanged.

#include "cli.hpp"

namespace nexus_cli {

// ---------------------------------------------------------------------
// nexus cyber (0.1.67): the cybersecurity toolbox. Kali's tools in a
// Distrobox container -- every tool for whoever asks, nothing for
// anyone else, and gone with one command. The container belongs to the
// person (rootless), shares their home and the network, and never
// touches the signed system image.
int commandCyber(
    const std::vector<std::string>& raw,
    bool apply,
    const std::string& invocation
) {
    using namespace nexus::system;
    namespace fs = std::filesystem;

    std::vector<std::string> words;
    bool rootBox = false, asApp = false;
    for (const auto& w : raw) {
        // After `run <tool>`, every word is the tool's, even --root.
        if (words.size() >= 2 && words[0] == "run") { words.push_back(w); continue; }
        if (w == "--root") rootBox = true;
        else if (w == "--app") asApp = true;
        else words.push_back(w);
    }
    const std::string action = words.empty() ? "" : words[0];
    const std::string box = kCyberContainer;
    const std::string rootFlag = rootBox ? " --root" : "";

    const char* legal =
        "Use these tools only on systems you own or have written permission to\n"
        "test. Elsewhere, scanning and breaking in are crimes in most countries.\n";

    auto haveDistrobox = [] { return commandExists("distrobox") && commandExists("podman"); };
    auto state = [&] {
        return parseDistroboxList(
            runCommand(std::string("distrobox list --no-color") + rootFlag + " 2>/dev/null", false).text,
            box);
    };
    auto needDistrobox = [&] {
        std::cout << "The toolbox runs in Distrobox, which is not installed here.\n"
                     "It comes with the cybersecurity or development add-on:\n\n"
                     "    sudo nexus setup cybersecurity --apply\n";
        return 1;
    };
    // A rootless box belongs to the person: sudo would make it root's.
    if (::geteuid() == 0 && !rootBox && action != "" && action != "status") {
        std::cout << "Run this without sudo: the toolbox belongs to you, not to root.\n"
                     "(Wi-Fi monitor mode and a few raw-socket tools need a root box:\n"
                     " add --root, without sudo; it asks for the password itself.)\n";
        return 1;
    }

    if (action.empty() || action == "status") {
        std::cout << "Cybersecurity toolbox -- Kali's tools in a container, kept out of the system.\n"
                  << (rootBox ? "Not a sandbox: a --root box runs as root on this machine and sees your\n"
                                "home folder. Use it only for the tools that need it.\n"
                              : "Not a sandbox: it sees your home folder and runs as you.\n")
                  << "For malware samples or tools you do not trust, use a virtual machine.\n\n";
        if (!haveDistrobox()) {
            std::cout << "  Distrobox: not installed (sudo nexus setup cybersecurity --apply)\n";
        } else {
            const auto st = state();
            if (st.exists) {
                std::cout << "  Toolbox:   " << box << ", " << (st.running ? "running" : "stopped")
                          << " (" << st.status << ")\n";
                std::string tier;
                std::ifstream in(std::string(std::getenv("HOME") ? std::getenv("HOME") : "")
                                 + "/.local/state/nexus/cyber-tier");
                std::getline(in, tier);
                if (!tier.empty()) std::cout << "  Size:      " << tier << "\n";
            } else {
                std::cout << "  Toolbox:   not created\n";
            }
        }
        std::cout << "\nSizes (the exact download is shown by apt before anything is installed):\n";
        for (const auto& t : cyberTiers()) {
            std::string n = t.name;
            while (n.size() < 8) n.push_back(' ');
            std::cout << "  " << n << t.what << "\n";
        }
        std::cout << "\nCommands:\n"
                     "  nexus cyber create small|medium|full [--root] --apply\n"
                     "  nexus cyber shell                 a Kali shell; your files are there\n"
                     "  nexus cyber run <tool> [args]     one tool, e.g. nexus cyber run nmap -sV 192.168.0.1\n"
                     "  nexus cyber export <tool> [--app] --apply   put a tool in your terminal (or menu)\n"
                     "  nexus cyber remove --apply        delete the toolbox and everything in it\n\n"
                  << legal;
        return 0;
    }

    if (!haveDistrobox()) return needDistrobox();

    if (action == "create") {
        const std::string tierName = words.size() > 1 ? words[1] : "";
        const auto tier = cyberTier(tierName);
        if (!tier) {
            std::cout << "Which size? small, medium or full:\n\n"
                         "    nexus cyber create medium --apply\n\n"
                         "nexus cyber shows what each one holds.\n";
            return 2;
        }
        const auto st = state();
        std::cout << "Toolbox:  " << box << " from " << kKaliImage << (rootBox ? " (root box)" : "") << "\n"
                  << "Tools:    " << tier->metapackage << " -- " << tier->what << "\n"
                  << "System:   untouched; the tools live only in the container.\n";
        if (st.exists) {
            std::cout << "Exists:   yes, so only the tools are added to it.\n";
        }
        std::cout << "\n" << legal;
        if (!apply) {
            std::cout << "\nNothing has been changed. To do it:\n    " << invocation << " --apply\n";
            return 0;
        }

        TransactionRecord record;
        record.when = currentTimestamp();
        record.request = "cyber create " + tier->name;
        record.kind = TransactionKind::Install;
        record.resolved = "distrobox " + box + ": " + tier->metapackage;
        record.packages.insert(tier->metapackage);
        const std::string log = defaultTransactionLog();
        const std::string marker = beginTransaction(log, record);

        bool ok = true;
        if (!st.exists) {
            std::cout << "\nCreating the container (downloads Kali's base image once).\n" << std::flush;
            ok = std::system(("distrobox create" + rootFlag + " --yes --name " + box +
                              " --image " + shellQuote(pinnedImage(kKaliImage))).c_str()) == 0;
        }
        if (ok) {
            std::cout << "\nInstalling " << tier->metapackage
                      << ". apt shows the download size and asks before it starts.\n\n" << std::flush;
            ok = std::system(("distrobox enter" + rootFlag + " " + box +
                              " -- sh -c 'sudo apt-get update && sudo apt-get install " +
                              tier->metapackage + "'").c_str()) == 0;
        }
        if (ok) {
            const char* home = std::getenv("HOME");
            if (home) {
                std::error_code ec;
                fs::create_directories(std::string(home) + "/.local/state/nexus", ec);
                std::ofstream(std::string(home) + "/.local/state/nexus/cyber-tier")
                    << tier->name << " (" << tier->metapackage << ")" << (rootBox ? ", root box" : "") << "\n";
            }
        }
        record.outcome = ok ? "created" : "failed";
        record.succeeded = ok;
        record.exitCode = ok ? 0 : 1;
        finishTransaction(log, marker, record);
        if (ok) {
            std::cout << "\nReady. Start it:  nexus cyber shell\n"
                         "One tool:         nexus cyber run nmap --help\n";
        } else {
            std::cout << "\nNot finished. Run the same command again; what is done is kept.\n";
        }
        return ok ? 0 : 1;
    }

    const auto st = state();
    if (!st.exists && action != "remove") {
        std::cout << "No toolbox yet. Create one first:\n\n    nexus cyber create medium --apply\n";
        return 1;
    }

    if (action == "shell") {
        return std::system(("distrobox enter" + rootFlag + " " + box).c_str()) == 0 ? 0 : 1;
    }

    if (action == "run") {
        if (words.size() < 2 || !safeToolName(words[1])) {
            std::cout << "Usage: nexus cyber run <tool> [arguments]\n";
            return 2;
        }
        std::string cmd = "distrobox enter" + rootFlag + " " + box + " --";
        for (std::size_t i = 1; i < words.size(); ++i) cmd += " " + shellQuote(words[i]);
        return std::system(cmd.c_str()) == 0 ? 0 : 1;
    }

    if (action == "export") {
        if (words.size() < 2 || !safeToolName(words[1])) {
            std::cout << "Usage: nexus cyber export <tool> [--app] --apply\n";
            return 2;
        }
        const std::string tool = words[1];
        std::cout << (asApp ? "Menu entry: " : "Command:    ") << tool
                  << (asApp ? " (runs in the toolbox)\n" : " in ~/.local/bin, running inside the toolbox\n");
        if (!apply) {
            std::cout << "\nNothing has been changed. To do it:\n    " << invocation << " --apply\n";
            return 0;
        }
        const std::string inner = asApp
            ? "distrobox-export --app " + shellQuote(tool)
            : "distrobox-export --bin \"$(command -v " + shellQuote(tool) + ")\" --export-path \"$HOME/.local/bin\"";
        TransactionRecord record;
        record.when = currentTimestamp();
        record.request = "cyber export " + tool;
        record.kind = TransactionKind::Install;
        record.resolved = std::string(asApp ? "menu entry" : "~/.local/bin/") + (asApp ? " " : "") + tool;
        record.packages.insert(tool);
        const std::string log = defaultTransactionLog();
        const std::string marker = beginTransaction(log, record);
        const bool ok = std::system(("distrobox enter" + rootFlag + " " + box + " -- sh -c " +
                                     shellQuote(inner)).c_str()) == 0;
        record.outcome = ok ? "exported" : "failed";
        record.succeeded = ok;
        record.exitCode = ok ? 0 : 1;
        finishTransaction(log, marker, record);
        return ok ? 0 : 1;
    }

    if (action == "remove") {
        if (!st.exists) {
            std::cout << "There is no toolbox to remove.\n";
            return 0;
        }
        std::cout << "Removes " << box << " and every tool and file inside it.\n"
                     "Your own files (your home folder) are not touched.\n"
                     "Commands and menu entries exported with `nexus cyber export` go\n"
                     "with it.\n";
        if (!apply) {
            std::cout << "\nNothing has been changed. To do it:\n    " << invocation << " --apply\n";
            return 0;
        }
        TransactionRecord record;
        record.when = currentTimestamp();
        record.request = "cyber remove";
        record.kind = TransactionKind::Remove;
        record.resolved = "distrobox " + box;
        record.packages.insert(box);
        const std::string log = defaultTransactionLog();
        const std::string marker = beginTransaction(log, record);
        const bool ok = std::system(("distrobox rm" + rootFlag + " --force " + box).c_str()) == 0;
        if (ok) {
            if (const char* home = std::getenv("HOME")) {
                std::error_code ec;
                fs::remove(std::string(home) + "/.local/state/nexus/cyber-tier", ec);
            }
        }
        record.outcome = ok ? "removed" : "failed";
        record.succeeded = ok;
        record.exitCode = ok ? 0 : 1;
        finishTransaction(log, marker, record);
        return ok ? 0 : 1;
    }

    std::cout << "Unknown: nexus cyber " << action << "\n"
                 "    nexus cyber   (shows every command)\n";
    return 2;
}

}  // namespace nexus_cli
