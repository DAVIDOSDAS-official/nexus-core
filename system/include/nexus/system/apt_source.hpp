#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <nexus/component.hpp>
#include <nexus/conflict_detector.hpp>

namespace nexus::system {

// Why a particular index file could not be read.
struct SkippedIndex {
    std::string path;
    std::string reason;
};

struct AptSourceResult {
    std::vector<Component> components;

    std::vector<std::string> filesRead;
    std::vector<SkippedIndex> filesSkipped;

    std::size_t stanzasRead = 0;

    // Where several versions of the same package exist across
    // suites, only the highest is kept. The rest are counted so the
    // number is visible rather than assumed.
    std::size_t versionsSuperseded = 0;
};

// Reads the package indexes apt has already downloaded.
//
// This is what lets Nexus answer questions about packages that are
// not installed: what steam would pull in, whether a profile could be
// satisfied, what a choice would cost. It is strictly read-only and
// never contacts the network -- it reads files apt put there.
//
// Index files may be stored compressed (.lz4, .gz, .xz, .bz2). Those
// are read through the matching decompressor. If a decompressor is
// missing, the file is skipped and reported; it is never silently
// ignored.
class AptSource {
public:
    explicit AptSource(
        std::string listsDirectory = "/var/lib/apt/lists"
    );

    AptSourceResult load() const;

    const std::string& listsDirectory() const;

private:
    std::string listsDirectory_;
};

// Combine an installed set with an available set into the universe
// the resolver plans against.
//
// The newer version wins, not the installed one.
//
// Installed-wins is right for describing this machine and wrong for
// planning: an upgrade that exists in the archive is a thing you can
// have. Shadowing it made firefox unsatisfiable on a system where the
// nss it needed was one dnf update away, and the fallback quietly
// chose a different browser instead.
//
// The installed set itself is untouched, so anything reporting what
// is actually here still reads that rather than this.
std::vector<Component> mergeAvailable(
    const std::vector<Component>& installed,
    const std::vector<Component>& available,
    const VersionComparator& comparator = {}
);

// The command needed to read a compressed index, or an empty string
// when the file can be opened directly.
std::string decompressorFor(const std::string& path);

}
