#pragma once

// What a Flatpak app asks to reach outside its sandbox (0.1.97,
// round-4 audit L4).
//
// `flatpak install --noninteractive` grants every permission the app's
// metadata asks for, and neither `nexus app install` nor the Shop said
// what they were: an app could ask for the whole home folder, every
// device, or a way to run commands outside its sandbox, and be
// installed with one click. The metadata is read first now
// (`flatpak remote-info --show-metadata`), said in plain words, and
// what reaches past the usual needs a person's yes.
//
// Pure functions over the metadata text, tested without flatpak.

#include <string>
#include <vector>

namespace nexus::system {

struct AppPermission {
    std::string text;   // plain words: "your whole home folder"
    // Reaches past what an ordinary desktop app needs: all files, all
    // devices, the session or system bus, running commands outside the
    // sandbox, saved passwords, SSH or GPG keys.
    bool broad = false;
};

struct AppPermissions {
    bool read = false;   // the metadata was there and had a [Context] or app section
    std::vector<AppPermission> items;

    bool anyBroad() const;
};

// The text `flatpak remote-info --show-metadata` (or `flatpak info
// --show-metadata`) prints: an ini-style key file.
AppPermissions parseFlatpakPermissions(const std::string& metadata);

// What an app's update would add (0.2.6, outside audit NX-02). Until
// then `flatpak update -y` took whatever a new version asked for, while
// the Shop's update dialog said nothing unseen would change; with NX-01
// that was the audit's Chain A: install clean, escape on an update.
struct PermissionGain {
    std::vector<AppPermission> gained;  // in the new version, not the installed one
    bool held = false;                  // not updated without a yes
    std::string why;                    // one line, for the person and the Shop
};

// `installed` and `offered` are the two metadata texts. An offered one
// that could not be read holds the update: what it would get is not
// known. Only a gained permission that needs a yes holds it; one that
// does not is listed and let through.
PermissionGain comparePermissions(const std::string& installed, const std::string& offered,
                                  bool offeredRead);

}  // namespace nexus::system
