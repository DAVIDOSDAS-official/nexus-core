#pragma once

#include <string>
#include <vector>

#include <nexus/component.hpp>

namespace nexus::system {

struct NixSourceResult {
    std::vector<Component> components;

    std::size_t installed = 0;

    std::string error;
};

// What Nix has installed.
//
// Nix is not NixOS. Nix is a package manager that runs on any Linux
// and installs into /nix/store in isolation; NixOS is a distribution
// built entirely out of it. Nexus reads the first, which is why Nix
// belongs beside Flatpak and containers rather than beside apt.
//
// Only what is installed. The set of packages Nix could offer is
// enormous and lives behind an evaluation rather than an index file:
// asking it per capability would mean evaluating nixpkgs per
// question. So Nix explains what is on a machine, as snap does.
NixSourceResult readNixProfile();

// Exposed for testing against captured output.
NixSourceResult parseNixProfile(const std::string& text);

// A store path is /nix/store/<hash>-<name>-<version>, so a version
// can often be recovered from it. Often, not always: some derivations
// have no version at all, and inventing one would be worse than
// leaving it empty.
std::string versionFromStorePath(
    const std::string& path,
    const std::string& name
);

}
