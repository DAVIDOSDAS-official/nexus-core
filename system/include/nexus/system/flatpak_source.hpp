#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <nexus/component.hpp>

namespace nexus::system {

struct FlatpakSourceResult {
    std::vector<Component> components;

    std::size_t remoteApps = 0;
    std::size_t installedApps = 0;

    std::string error;
};

// Reads what Flatpak offers, by asking flatpak.
//
// Flatpak components carry no dependencies. That is not a gap in the
// reading: a Flatpak bundles what it needs and runs against a runtime
// outside the package graph, which is exactly why it can offer a
// current application on an old base. The cost is size rather than a
// dependency count, so size is what gets reported.
//
// Application ids are reverse-DNS -- org.mozilla.firefox -- so the
// alias table does the naming, the same way it does for dpkg and rpm.
// Flatpak is another vocabulary, not another mechanism.
class FlatpakSource {
public:
    FlatpakSourceResult load() const;

    static bool available();

    // Exposed for testing against captured output.
    static FlatpakSourceResult parse(
        const std::string& remote,
        const std::string& installed
    );
};

// "196.9 MB" and "532.0 kB" as flatpak prints them, in bytes.
// Returns 0 when the text cannot be read as a size.
std::uint64_t parseHumanSize(const std::string& text);

std::string formatSize(std::uint64_t bytes);

}
