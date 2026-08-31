#pragma once

#include <iosfwd>
#include <string>
#include <vector>

#include <nexus/alias.hpp>

namespace nexus::system {

struct AliasParseResult {
    AliasTable table;
    std::vector<std::string> problems;
};

// Alias files use the same control format as everything else here:
//
//     Capability: web-browser
//     Resolves-To: firefox | chromium | epiphany-browser
//
//     Capability: vulkan-driver
//     Resolves-To: mesa-vulkan-drivers | nvidia-driver-libs
//
// Resolves-To uses the dependency grammar, so version conditions work
// there too.
AliasParseResult parseAliasStream(std::istream& input);

AliasParseResult parseAliasFile(const std::string& path);

}
