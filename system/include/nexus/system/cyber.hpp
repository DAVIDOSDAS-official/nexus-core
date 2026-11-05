#pragma once

#include <optional>
#include <string>
#include <vector>

namespace nexus::system {

// `nexus cyber` (0.1.67): the cybersecurity toolbox -- Kali's tools in
// a container, so they are all there for whoever wants them and cost
// nothing to everyone else. No bloat in the system: the container is
// created on request, holds every tool, and goes with one command.
//
// Built on Distrobox: the container shares the user's home and the
// network, so the tools act on the real machine and files, while
// their packages stay out of the signed system image.
//
// This file is the pure part (tiers, names, parsing); running
// distrobox is the CLI's.

inline constexpr const char* kCyberContainer = "nexus-cyber";
inline constexpr const char* kKaliImage = "docker.io/kalilinux/kali-rolling:latest";

struct CyberTier {
    std::string name;         // small, medium, full
    std::string metapackage;  // Kali's own bundle
    std::string what;         // in plain words
};

// small: Kali's ten most used tools; medium: Kali's default set; full:
// everything Kali packages. Sizes are not guessed here: apt inside the
// container says exactly what each one downloads, before it starts.
const std::vector<CyberTier>& cyberTiers();
std::optional<CyberTier> cyberTier(const std::string& name);

// A tool name that may go into a command line: letters, digits and
// . _ + - only, not starting with '-'.
bool safeToolName(const std::string& name);

// `distrobox list --no-color` -> whether a container with this exact
// name is listed, and its status column ("Up 2 hours", "Exited ...").
struct BoxState {
    bool exists = false;
    bool running = false;
    std::string status;
    std::string image;
};
BoxState parseDistroboxList(const std::string& text, const std::string& name);

}  // namespace nexus::system
