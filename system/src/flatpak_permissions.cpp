#include <nexus/system/flatpak_permissions.hpp>

#include <nexus/system/process.hpp>

#include <algorithm>
#include <set>
#include <sstream>

namespace nexus::system {

namespace {

std::string trimmed(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string::npos) return {};
    const auto last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1);
}

std::vector<std::string> splitList(const std::string& value) {
    std::vector<std::string> out;
    std::istringstream parts(value);
    std::string part;
    while (std::getline(parts, part, ';')) {
        part = trimmed(part);
        if (!part.empty() && part.front() != '!') out.push_back(part);   // "!x" takes away
    }
    return out;
}

// One filesystem entry ("home", "xdg-download:ro", "~/Games:create").
AppPermission filesystem(const std::string& entry) {
    std::string place = entry;
    std::string mode;
    const auto colon = place.rfind(':');
    if (colon != std::string::npos) {
        mode = place.substr(colon + 1);
        place = place.substr(0, colon);
    }
    const bool readOnly = mode == "ro";
    const std::string suffix = readOnly ? " (read only)" : "";

    if (place == "host") {
        return {"all your files and the system's (host)" + suffix, true};
    }
    if (place == "host-os" || place == "host-etc") {
        return {"the system's own files (" + place + ")" + suffix, true};
    }
    if (place == "home" || place == "~") {
        // Read-only still means every document, key and browser profile in it.
        return {"your whole home folder" + suffix, true};
    }
    static const std::pair<const char*, const char*> folders[] = {
        {"xdg-download", "Downloads"}, {"xdg-documents", "Documents"},
        {"xdg-music", "Music"},        {"xdg-pictures", "Pictures"},
        {"xdg-videos", "Videos"},      {"xdg-desktop", "Desktop"},
        {"xdg-public-share", "Public"}, {"xdg-templates", "Templates"},
    };
    for (const auto& [key, name] : folders) {
        if (place == key) return {std::string("your ") + name + " folder" + suffix, false};
    }
    // Inside one of those folders ("xdg-download/Foo"): the same.
    for (const auto& [key, name] : folders) {
        const std::string prefix = std::string(key) + "/";
        if (place.rfind(prefix, 0) == 0) {
            return {std::string("a folder in your ") + name + " folder (" + place + ")" + suffix, false};
        }
    }

    // Everything else is judged by what it could reach (0.2.6, outside
    // audit NX-01). Until then only a few strings were "broad" and every
    // other place was "usual": write access to ~/.config (where autostart
    // is), ~/.local/bin (first on the PATH), ~/.ssh and KDE's login
    // scripts all showed calmly and installed without a typed yes. Now
    // the ordinary places are the short list above and a plain folder of
    // the app's own in home; anything else that is written to needs a yes.
    const bool inHome = place.rfind("~/", 0) == 0 || place.rfind("home/", 0) == 0;
    const bool absolute = !place.empty() && place.front() == '/';
    const bool xdg = place.rfind("xdg-", 0) == 0;
    const std::string rest =
        place.rfind("~/", 0) == 0 ? place.substr(2)
        : place.rfind("home/", 0) == 0 ? place.substr(5)
        : place;
    auto has = [&](const char* part) { return place.find(part) != std::string::npos; };

    // Secrets: even reading them is too much.
    if (has(".ssh") || has(".gnupg") || has(".mozilla") || has(".password-store") ||
        has("keyrings") || has(".pki") || has("kwalletd") ||
        place == "xdg-data/keyrings") {
        return {"your keys, passwords or browser profile (" + place + ")" + suffix, true};
    }
    // Whatever runs on its own, outside the sandbox: at login, on the
    // PATH, from the menu, or in every shell.
    const bool startsThings =
        has("autostart") || has("systemd") || has("environment.d") ||
        has("plasma-workspace") || has(".local/bin") || has("xdg-data/applications") ||
        has(".local/share/applications") || has("flatpak") ||
        has(".bashrc") || has(".bash_profile") || has(".bash_login") || has(".profile") ||
        has(".zshrc") || has(".zprofile") || has(".zshenv") || has(".xprofile") ||
        has(".xinitrc") || has(".xsession") || has(".pam_environment") ||
        has("xdg-run/bus");
    // dconf holds the desktop's settings, keyboard shortcuts and the
    // commands they run among them. Older GNOME apps ask for it.
    if (has("dconf") && !readOnly) {
        return {"your desktop's settings, keyboard shortcuts included (" + place + ")" + suffix, true};
    }
    if (startsThings && !readOnly) {
        return {"files that make programs start by themselves (" + place + ")" + suffix, true};
    }
    // The whole of a hidden place: every app's settings and data.
    if (place == "xdg-config" || place == "xdg-data" || place == "xdg-cache" ||
        place == "xdg-state" || place == "xdg-run") {
        return {"every app's " + place.substr(4) + " folder (" + place + ")" + suffix, true};
    }
    const std::string where = xdg ? "the place " : "the folder ";
    if (readOnly) {
        // Reading a named place is ordinary, unless it is a secret (above).
        return {where + place + suffix, false};
    }
    // Written to: a plain folder of the app's own in home ("~/Games")
    // is ordinary; a hidden one, a place in another app's settings or
    // data, or anywhere outside home is not.
    if (inHome) {
        const bool hidden = !rest.empty() && rest.front() == '.';
        return {where + place + suffix, hidden};
    }
    if (xdg) {
        // xdg-cache/X and xdg-run/X: caches and sockets, ordinary.
        const bool harmless = place.rfind("xdg-cache/", 0) == 0 || place.rfind("xdg-run/", 0) == 0;
        return {where + place + suffix, !harmless};
    }
    // Anywhere else on the system, written to (or a form not known).
    (void)absolute;
    return {where + place + suffix, true};
}

// A session-bus name an app talks to or owns: the ones that reach
// outside the sandbox, or someone's secrets, need a yes (0.2.6, NX-01:
// until then every name but four was left out of the list entirely).
AppPermission sessionBusName(const std::string& name, bool owns) {
    const bool wildcard = name.find('*') != std::string::npos;
    auto starts = [&](const char* prefix) { return name.rfind(prefix, 0) == 0; };
    if (!owns) {
        if (starts("org.freedesktop.Flatpak"))
            return {"running commands outside its sandbox (" + name + ")", true};
        if (name == "org.freedesktop.systemd1")
            return {"starting programs outside its sandbox (" + name + ")", true};
        if (name == "org.freedesktop.secrets")
            return {"your saved passwords (the keyring)", true};
        // KDE's password store (the Asus, 15 Nov: VLC asks for both
        // kwalletd and kwalletd5); reached by name, not through the
        // secrets service.
        if (starts("org.kde.kwalletd"))
            return {"your saved passwords (KWallet)", true};
        if (name == "org.freedesktop.impl.portal.PermissionStore")
            return {"the permissions given to other apps", true};
        if (starts("org.kde.plasmashell") || starts("org.kde.klauncher") ||
            starts("org.kde.KWin") || starts("org.gnome.Shell") ||
            starts("ca.desrt.dconf") || starts("org.kde.kded"))
            return {"controlling your desktop (" + name + ")", true};
        if (wildcard)
            return {"every desktop service named " + name, true};
        return {"the desktop service " + name, false};
    }
    // Owning a name is answering under it. Media-player names with a
    // wildcard are how players announce themselves (MPRIS): ordinary.
    if (starts("org.mpris.MediaPlayer2."))
        return {"announcing itself as a media player (" + name + ")", false};
    if (wildcard || starts("org.freedesktop.") || starts("org.kde.") || starts("org.gnome."))
        return {"answering as a desktop service (" + name + ")", true};
    return {"offering the service " + name, false};
}

}  // namespace

bool AppPermissions::anyBroad() const {
    for (const auto& item : items) {
        if (item.broad) return true;
    }
    return false;
}

AppPermissions parseFlatpakPermissions(const std::string& metadata) {
    AppPermissions out;
    std::istringstream lines(metadata);
    std::string line;
    std::string section;
    std::set<std::string> said;   // each sentence once

    auto add = [&](AppPermission permission) {
        permission.text = printable(permission.text);
        if (said.insert(permission.text).second) out.items.push_back(std::move(permission));
    };

    while (std::getline(lines, line)) {
        line = trimmed(line);
        if (line.empty() || line.front() == '#') continue;
        if (line.front() == '[' && line.back() == ']') {
            section = line.substr(1, line.size() - 2);
            if (section == "Application" || section == "Context") out.read = true;
            continue;
        }
        const auto equals = line.find('=');
        if (equals == std::string::npos) continue;
        const std::string key = trimmed(line.substr(0, equals));
        const std::string value = trimmed(line.substr(equals + 1));

        if (section == "Context") {
            for (const auto& item : splitList(value)) {
                if (key == "filesystems") {
                    add(filesystem(item));
                } else if (key == "devices") {
                    if (item == "all") add({"every device: cameras, USB, game pads and more", true});
                    else if (item == "dri") add({"the graphics card", false});
                    else if (item == "kvm") add({"virtual machines (kvm)", false});
                    // Raw input devices: every key pressed, in every app.
                    else if (item == "input") add({"every keyboard and mouse directly (it can read what you type)", true});
                    else if (item == "usb") add({"USB devices", false});
                    else add({"the device " + item, false});
                } else if (key == "sockets") {
                    if (item == "x11" || item == "fallback-x11")
                        add({"the X11 display (where it can see other X11 windows)", false});
                    else if (item == "wayland") add({"the screen (Wayland)", false});
                    else if (item == "pulseaudio") add({"sound, and the microphone", false});
                    else if (item == "session-bus") add({"every service of your desktop session (session bus)", true});
                    else if (item == "system-bus") add({"every system service (system bus)", true});
                    else if (item == "ssh-auth") add({"your SSH keys (the SSH agent)", true});
                    else if (item == "gpg-agent") add({"your GPG keys (the GPG agent)", true});
                    else if (item == "cups") add({"printers", false});
                    else if (item == "pcsc") add({"smart cards", false});
                    else add({"the " + item + " socket", false});
                } else if (key == "shared") {
                    if (item == "network") add({"the internet", false});
                    // ipc: shared memory with X11; not worth a line.
                }
            }
        } else if (section == "Session Bus Policy" && (value == "talk" || value == "own")) {
            add(sessionBusName(key, value == "own"));
        } else if (section == "System Bus Policy" && (value == "talk" || value == "own")) {
            add({"the system service " + key, true});
        }
    }
    // What reaches furthest first: it is what a person must not miss.
    std::stable_partition(out.items.begin(), out.items.end(),
                          [](const AppPermission& item) { return item.broad; });
    return out;
}

}  // namespace nexus::system
