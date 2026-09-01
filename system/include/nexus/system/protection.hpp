#pragma once

#include <set>
#include <string>
#include <vector>

#include <nexus/component.hpp>

namespace nexus::system {

// Which components must never be proposed for removal, read from the
// distribution's own configuration rather than encoded here.
//
// apt keeps these rules in /etc/apt/apt.conf.d/01autoremove:
//
//     NeverAutoRemove { "^linux-image-[a-z0-9]*$"; ... };
//     VersionedKernelPackages { "linux-.*"; ".*-modules"; ... };
//     Never-MarkAuto-Sections { "metapackages"; ... };
//
// Reading them means the policy stays whatever the distribution
// decided, and changes with it. Writing "keep one old kernel" in C++
// would be a guess that silently goes stale.
struct ProtectionRules {
    std::vector<std::string> neverRemove;
    std::vector<std::string> kernelPatterns;
    std::vector<std::string> neverAutoSections;

    bool empty() const;
};

ProtectionRules parseProtectionRules(const std::string& text);

// Reads every *.conf-style file in an apt configuration directory.
ProtectionRules readProtectionRules(
    const std::string& directory = "/etc/apt/apt.conf.d"
);

// The release string of the kernel currently running, or empty when
// it cannot be determined.
std::string runningKernelRelease();

// Components that must not be proposed for removal.
//
// Anything matching NeverAutoRemove, plus the kernel that is running
// and the newest kernel installed. Removing the running kernel leaves
// a machine that does not boot; removing the newest leaves no fallback
// if the running one turns out to be broken.
std::set<std::string> protectedComponents(
    const std::vector<Component>& installed,
    const ProtectionRules& rules,
    const std::string& runningKernel
);

}
