#pragma once

#include <optional>
#include <string>
#include <vector>

namespace nexus::system {

// `nexus dev` (0.1.69): programming languages, each one on request.
//
// They go into one Distrobox container, "nexus-dev", made from
// Fedora's own toolbox image for the same Fedora release as the
// system. Inside it dnf works normally and nothing waits for a
// restart; the main commands (node, cargo, go, ruby...) are exported
// to ~/.local/bin, so they work from any terminal as if installed.
// The signed system image is never touched. The development add-on
// still puts C, C++ and Python on the system itself; for those the
// container is only used when they are not there.
//
// This file is the pure part (the language table, help topics,
// os-release); running distrobox is the CLI's.

inline constexpr const char* kDevContainer = "nexus-dev";

struct DevLanguage {
    std::string key;                    // cpp, python, js, java, rust, go, ruby
    std::string display;                // "C and C++"
    std::vector<std::string> names;     // what people type
    std::vector<std::string> packages;  // Fedora packages in the box
    std::vector<std::string> commands;  // exported to ~/.local/bin
    std::string versionCommand;         // shows it works
    bool onSystemWithAddon = false;     // the development add-on has it
};

const std::vector<DevLanguage>& devLanguages();

// "c++", "C++", "cpp", "py", "javascript", "node", "golang", "rb"...
std::optional<DevLanguage> findDevLanguage(const std::string& typed);

// Fedora's toolbox image for this release (VERSION_ID of os-release),
// "latest" when the release cannot be read.
std::string devImageFor(const std::string& osRelease);

// `nexus dev help <topic>`: the commands worth knowing, in the
// terminal. Empty when there is no such topic.
std::string devHelp(const std::string& topic);
std::vector<std::string> devHelpTopics();

}  // namespace nexus::system
