#pragma once

#include <optional>
#include <string>

namespace nexus {

// How a component may satisfy dependencies across architectures.
//
// This mirrors Debian's Multi-Arch field. It matters because
// libc6:amd64 and libc6:i386 are different packages that can be
// installed at the same time, and a 64-bit binary is not satisfied by
// a 32-bit library.
enum class MultiArch {
    No,        // only satisfies dependencies of its own architecture
    Same,      // co-installable across architectures; arch must match
    Foreign,   // satisfies dependencies of any architecture
    Allowed    // may be depended on with an explicit ":any" qualifier
};

// The architecture name used for components that work anywhere.
inline constexpr const char* kArchitectureAll = "all";

// The qualifier meaning "any architecture will do".
inline constexpr const char* kArchitectureAny = "any";

// Whether a provider's architecture can satisfy a dependency.
//
// requesterArchitecture may be empty, which means the caller is not
// tracking architecture. In that case the check passes: this keeps
// architecture-blind callers working exactly as before, rather than
// silently changing their answers.
//
// This implements the common cases of Debian's multi-arch rules, not
// the whole specification. The parts it does not model are recorded
// as gaps rather than assumed away.
bool architectureSatisfies(
    const std::string& providerArchitecture,
    MultiArch providerMultiArch,
    const std::optional<std::string>& qualifier,
    const std::string& requesterArchitecture
);

MultiArch parseMultiArch(const std::string& text);

std::string toString(MultiArch value);

}
