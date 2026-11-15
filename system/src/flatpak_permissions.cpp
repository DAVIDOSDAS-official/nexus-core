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
    // A path the app names: a folder in home ("~/Games"), or anywhere.
    const bool inHome = place.rfind("~/", 0) == 0 || place.rfind("home/", 0) == 0;
    const bool absolute = !place.empty() && place.front() == '/';
    // Places that hold what starts by itself, or configuration of the
    // whole desktop, are not ordinary folders.
    const bool startsThings = place.find("autostart") != std::string::npos ||
                              place.find("systemd") != std::string::npos ||
                              place.find(".bashrc") != std::string::npos ||
                              place.find(".profile") != std::string::npos;
    if (startsThings) {
        return {"files that make programs start by itself (" + place + ")" + suffix, true};
    }
    // A folder of its own in home is ordinary; anywhere else on the
    // system, written to, is not.
    const std::string where = place.rfind("xdg-", 0) == 0 ? "the place " : "the folder ";
    return {where + place + suffix, absolute && !readOnly && !inHome};
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
                    else if (item == "input") add({"input devices directly", false});
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
        } else if (section == "Session Bus Policy" && value == "talk") {
            if (key == "org.freedesktop.Flatpak")
                add({"running commands outside its sandbox (org.freedesktop.Flatpak)", true});
            else if (key == "org.freedesktop.secrets")
                add({"your saved passwords (the keyring)", true});
            else if (key == "org.freedesktop.impl.portal.PermissionStore")
                add({"the permissions given to other apps", true});
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
