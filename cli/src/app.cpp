// nexus app: Flatpak apps, for Nexus Shop.
//
// Part of the `nexus` tool. Until 0.1.96 this was in cli/src/main.cpp,
// which had grown past 11,000 lines; it was moved here unchanged.

#include "cli.hpp"

namespace nexus_cli {

// nexus app install|remove <id>: one Flathub app, for Nexus Shop
// and for anyone at a terminal.
//
// Flatpak, system-wide, from Flathub. It needs no sudo: Flatpak's
// own helper lets the person at the machine install and remove apps
// (Asus, 9 October: no password asked). Written in history like
// every other change, which is why the Shop goes through here rather
// than calling flatpak itself.
int commandApp(
    const std::vector<std::string>& words,
    bool apply
) {
    if (words.size() != 2 ||
        (words[0] != "install" && words[0] != "remove")) {
        std::cerr << "Use: nexus app install <id> [--apply]\n"
                     "     nexus app remove <id> [--apply]\n"
                     "The id is Flathub's, like com.obsproject.Studio.\n";
        return 2;
    }
    const bool install = words[0] == "install";
    const std::string& id = words[1];

    if (!nexus::system::isFlatpakId(id)) {
        std::cerr << "\"" << id << "\" is not a Flathub app id "
                     "(they look like com.obsproject.Studio).\n";
        return 2;
    }
    if (!nexus::system::commandExists("flatpak")) {
        std::cout << "flatpak is not installed on this system.\n";
        return 1;
    }

    const bool present = nexus::system::runCommand(
        "flatpak info --system " + shellQuote(id) + " >/dev/null 2>&1", false)
        .exitCode == 0;

    if (install && present) {
        std::cout << id << " is already installed.\n";
        return 0;
    }
    if (!install && !present) {
        std::cout << id << " is not installed (system-wide).\n";
        return 0;
    }

    if (install) {
        std::cout << "Install " << id << " from Flathub\n"
                  << "  Kind       Flatpak app, system-wide\n"
                  << "  Restart    not needed\n"
                  << "  System     the image is not changed\n"
                  << "  Also       the runtime it needs, if not here yet "
                     "(shared with other apps)\n";
    } else {
        std::cout << "Remove " << id << "\n"
                  << "  Your files in ~/.var/app/" << id
                  << " are kept.\n";
    }

    const std::string command = install
        ? "flatpak install --system --noninteractive flathub " + shellQuote(id)
        : "flatpak uninstall --system --noninteractive " + shellQuote(id);

    if (!apply) {
        std::cout << "\nWhat it would run:\n    " << command
                  << "\n\nNothing has been changed. Add --apply to do it.\n";
        return 0;
    }

    if (install) {
        const std::string remote =
            std::filesystem::exists("/usr/share/nexus/flathub.flatpakrepo")
                ? "/usr/share/nexus/flathub.flatpakrepo"
                : "https://dl.flathub.org/repo/flathub.flatpakrepo";
        nexus::system::runCommand(
            "flatpak remote-add --system --if-not-exists flathub " +
            shellQuote(remote));
    }

    nexus::system::TransactionRecord record;
    record.kind = install ? nexus::system::TransactionKind::Install
                          : nexus::system::TransactionKind::Remove;
    record.when = nexus::system::currentTimestamp();
    record.request = std::string("app ") + words[0] + " " + id;
    record.resolved = "Flathub: " + id + " (Flatpak app)";
    record.packages = {id};
    const std::string log = nexus::system::defaultTransactionLog();
    const std::string marker = nexus::system::beginTransaction(log, record);

    std::cout << "\nHanding over to flatpak.\n\n" << std::flush;
    // Not captured: Flatpak prints its progress, and the first app can
    // bring a runtime of a gigabyte. Silence for minutes looks like a
    // hang, in a terminal and in the Shop.
    const int status =
        std::system(nexus::system::inPlainLocale(command).c_str());
    const bool ok = status == 0;

    record.outcome = ok ? (install ? "installed" : "removed") : "failed";
    record.succeeded = ok;
    record.exitCode = status;
    nexus::system::finishTransaction(log, marker, record);

    std::cout << (ok ? (install ? "\nInstalled. Nothing to restart.\n"
                                : "\nRemoved.\n")
                     : "\nflatpak did not complete; its messages are "
                       "above. Nothing else was changed.\n");
    return ok ? 0 : 1;
}

}  // namespace nexus_cli
