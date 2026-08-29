#pragma once

#include <iosfwd>
#include <string>
#include <vector>

#include <nexus/profile.hpp>

namespace nexus::system {

// Profiles are written in the same control-file format the rest of
// the system layer already reads, so there is one format to learn and
// one parser to trust.
//
//     Profile: gaming
//     Description: Steam, drivers and 32-bit support
//     Architecture: amd64
//     Enables-Architectures: i386
//     Requires: steam, mesa-vulkan-drivers | nvidia-driver-libs
//     Prefers: audio=pipewire, display-server=wayland
//     Prefers-When: gpu-vendor-nvidia -> nvidia-driver-libs
//     Requires-Exactly: java=openjdk-21-jre
//
// Parsing never throws on a malformed field; problems are collected
// so the caller can report all of them at once.
struct ProfileParseResult {
    std::vector<Profile> profiles;
    std::vector<std::string> problems;
};

ProfileParseResult parseProfileStream(std::istream& input);

ProfileParseResult parseProfileFile(const std::string& path);

// Every *.profile file in a directory, sorted by filename.
ProfileParseResult parseProfileDirectory(const std::string& path);

}
