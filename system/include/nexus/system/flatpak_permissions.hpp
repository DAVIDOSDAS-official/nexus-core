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

}  // namespace nexus::system
