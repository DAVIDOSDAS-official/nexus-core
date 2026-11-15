// nexus app: Flatpak apps, for Nexus Shop.
//
// Part of the `nexus` tool. Until 0.1.96 this was in cli/src/main.cpp,
// which had grown past 11,000 lines; it was moved here unchanged.

#include "cli.hpp"

#include <nexus/system/flatpak_permissions.hpp>

namespace nexus_cli {

// What the app asks to reach, from Flathub's own metadata for it
// (0.1.97, round-4 audit L4). Read-only: remote-info downloads the
// metadata and changes nothing.
nexus::system::AppPermissions readAppPermissions(const std::string& id) {
    const auto shown = nexus::system::runCommand(
        "flatpak remote-info --system --show-metadata flathub " + shellQuote(id) +
            " 2>/dev/null",
        false);
    if (!shown.ok) return {};
    return nexus::system::parseFlatpakPermissions(shown.text);
}

void printAppPermissions(const nexus::system::AppPermissions& permissions,
                         const std::string& indent) {
    std::vector<std::string> usual;
    std::vector<std::string> broad;
    for (const auto& item : permissions.items) {
        (item.broad ? broad : usual).push_back(item.text);
    }
    if (usual.empty() && broad.empty()) {
        std::cout << indent << "Can reach  nothing outside its sandbox\n";
        return;
    }
    if (!usual.empty()) {
        std::cout << indent << "Can reach  ";
        for (std::size_t i = 0; i < usual.size(); ++i) {
            std::cout << (i ? "\n" + indent + "           " : "") << usual[i];
        }
        std::cout << "\n";
    }
    if (!broad.empty()) {
        std::cout << indent << "Also asks  for more than an app usually needs:\n";
        for (const auto& text : broad) std::cout << indent << "           ! " << text << "\n";
    }
}

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
    bool apply,
    bool assumeYes
) {
    if (words.size() != 2 ||
        (words[0] != "install" && words[0] != "remove" && words[0] != "permissions")) {
        std::cerr << "Use: nexus app install <id> [--apply]\n"
                     "     nexus app remove <id> [--apply]\n"
                     "     nexus app permissions <id>   (what it asks to reach; for the Shop)\n"
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

    // Lines for the Shop, which shows them before its Install button:
    //   perm<TAB>broad|usual<TAB>text      one per permission
    //   state<TAB>read|unread
    if (words[0] == "permissions") {
        const auto permissions = readAppPermissions(id);
        std::cout << "state\t" << (permissions.read ? "read" : "unread") << "\n";
        for (const auto& item : permissions.items) {
            std::cout << "perm\t" << (item.broad ? "broad" : "usual") << "\t" << item.text << "\n";
        }
        return permissions.read ? 0 : 1;
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

    nexus::system::AppPermissions permissions;
    if (install) {
        std::cout << "Install " << id << " from Flathub\n"
                  << "  Kind       Flatpak app, system-wide\n"
                  << "  Restart    not needed\n"
                  << "  System     the image is not changed\n"
                  << "  Also       the runtime it needs, if not here yet "
                     "(shared with other apps)\n";
        // Flatpak grants every permission the app asks for; they are
        // said before anything is installed (0.1.97, round-4 audit L4).
        permissions = readAppPermissions(id);
        if (permissions.read) {
            printAppPermissions(permissions);
        } else {
            std::cout << "  Can reach  could not be read from Flathub"
                      << (apply ? "" : " (with --apply Flathub is set up first and it is read then)")
                      << "\n";
        }
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
        const std::string addRemote =
            "flatpak remote-add --system --if-not-exists flathub " + shellQuote(remote);

        // Without Flathub set up, its metadata cannot be read: set it up
        // first, then read.
        bool remoteAdded = false;
        if (!permissions.read) {
            nexus::system::runCommand(addRemote);
            remoteAdded = true;
            permissions = readAppPermissions(id);
            if (permissions.read) printAppPermissions(permissions);
        }
        // Not installed on permissions nobody saw (0.1.97).
        if (!permissions.read) {
            std::cout << "\nWhat " << id << " would be allowed to reach could not be "
                         "read from Flathub,\nso it is not installed."
                      << " Nothing else has been changed"
                      << (remoteAdded ? " (Flathub is set up as a source, if it was not)" : "")
                      << ".\n";
            return 1;
        }
        // Beyond the usual: a person says yes to it. In a terminal, a
        // typed yes; elsewhere (the Shop, which shows the same list in
        // its own dialog first) --yes. Asked before anything is changed.
        if (permissions.anyBroad() && !assumeYes) {
            if (!::isatty(STDIN_FILENO)) {
                std::cout << "\n" << id << " asks for more than an app usually needs "
                             "(marked ! above).\nNot installed without a yes. Read the "
                             "list; to install it anyway:\n    nexus app install " << id
                          << " --apply --yes\nNothing has been changed.\n";
                return 1;
            }
            std::cout << "\n" << id << " asks for more than an app usually needs "
                         "(marked ! above).\nType 'yes' to install it anyway: ";
            std::string answer;
            std::getline(std::cin, answer);
            if (answer != "yes") {
                std::cout << "Nothing has been changed.\n";
                return 1;
            }
        }
        if (!remoteAdded) nexus::system::runCommand(addRemote);
    }

    nexus::system::TransactionRecord record;
    record.kind = install ? nexus::system::TransactionKind::Install
                          : nexus::system::TransactionKind::Remove;
    record.when = nexus::system::currentTimestamp();
    record.request = std::string("app ") + words[0] + " " + id;
    record.resolved = "Flathub: " + id + " (Flatpak app)";
    record.packages = {id};
    // What it was allowed beyond the usual, in the history too (0.1.97).
    if (install && permissions.anyBroad()) {
        std::string allowed;
        for (const auto& item : permissions.items) {
            if (item.broad) allowed += (allowed.empty() ? "" : "; ") + item.text;
        }
        record.resolved += " -- allowed: " + allowed;
    }
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
