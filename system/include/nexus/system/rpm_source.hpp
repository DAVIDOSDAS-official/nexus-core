#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include <nexus/component.hpp>

namespace nexus::system {

// Something in the metadata the model does not represent.
enum class RpmGapKind {
    BooleanDependency,   // "(a if b)", "(a or b)"
    UnknownFlag          // a comparison flag this code does not know
};

struct RpmGap {
    std::string componentId;
    RpmGapKind kind;
    std::string detail;
};

struct RpmSourceResult {
    std::vector<Component> components;
    std::vector<RpmGap> gaps;

    std::size_t packagesRead = 0;
    std::size_t sonameRequirements = 0;
    std::size_t fileRequirements = 0;

    std::string error;
};

// Reads Fedora-style repository metadata (repodata primary.xml).
//
// The shape is nothing like a Debian control file, and neither is the
// content. In Debian, "Depends: libstdc++6" names a package. Here most
// requirements name a symbol set -- "libstdc++.so.6(GLIBCXX_3.4.32)
// (64bit)" -- satisfied by whichever package provides that exact
// string. The capability model handles both, because a capability was
// never required to be a package name.
RpmSourceResult parseRepodataPrimary(const std::string& document);

// x86_64 -> amd64 and friends.
//
// The names are translated so that one architecture vocabulary runs
// through the whole resolver. Leaving both in would mean every
// comparison had to know which ecosystem it came from.
std::string normaliseRpmArchitecture(const std::string& architecture);

std::string toString(RpmGapKind kind);

}
