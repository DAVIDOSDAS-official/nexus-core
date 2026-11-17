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

// ---- The isolated toolbox (0.2.3, `nexus cyber ... --isolated`)
//
// Distrobox is no wall: it mounts the person's home folder and the
// host's whole file system (/run/host), and its --home option changes
// only where $HOME points -- its own manual says it "will NOT prevent
// the mount of the host's home directory". A tool that turned hostile
// could write ~/.bashrc and run as the person at the next login.
//
// The isolated box is a plain rootless podman container instead. It
// sees one folder of its own and nothing else of the machine: no home
// folder, no /run/host, no desktop or D-Bus sockets, no devices, its
// own network namespace (pasta: it reaches the network, not this
// machine's local-only services or abstract sockets). Its root is the
// person's own account, mapped. The cost: terminal tools only, no
// `export`, and scans that need raw sockets on the real network
// (nmap -sS, monitor mode) are not possible from it.

inline constexpr const char* kCyberIsolatedContainer = "nexus-cyber-isolated";

// <home>/.local/share/nexus-cyber-isolated/work: the one folder shared,
// seen as /work inside.
std::string isolatedWorkFolder(const std::string& home);

// podman create ..., for the image and that folder.
std::vector<std::string> isolatedCreateArgs(const std::string& image,
                                            const std::string& workFolder);

// podman exec into it, in /work; -t only when there is a terminal.
std::vector<std::string> isolatedExecArgs(const std::vector<std::string>& command,
                                          bool terminal);

// `podman container inspect --format {{.State.Status}}`: its answer
// and exit code -> the box's state.
BoxState parseIsolatedInspect(int exitCode, const std::string& text);

}  // namespace nexus::system
