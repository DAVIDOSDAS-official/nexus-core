#pragma once

#include <optional>
#include <string>

namespace nexus {

// How a version is compared against a required version.
//
// This lives in core rather than the system layer because every
// package ecosystem has version relations; only the comparison
// algorithm is distribution-specific.
enum class VersionRelation {
    Earlier,
    EarlierOrEqual,
    Exactly,
    LaterOrEqual,
    Later
};

struct VersionConstraint {
    VersionRelation relation;
    std::string version;
};

// A capability name plus an optional version condition.
//
//     graphics
//     libc6 (>= 2.38)
//
// A constraint with no version condition applies to any version.
struct Constraint {
    std::string capability;
    std::optional<VersionConstraint> version;

    // An explicit ":amd64" or ":any" written on the dependency.
    // Absent means "whatever architecture the requester is".
    std::optional<std::string> architecture;

    Constraint() = default;

    explicit Constraint(std::string capabilityName)
        : capability(std::move(capabilityName)) {
    }

    Constraint(std::string capabilityName, VersionConstraint condition)
        : capability(std::move(capabilityName)),
          version(condition) {
    }

    bool isUnversioned() const {
        return !version.has_value();
    }
};

std::string toString(VersionRelation relation);
std::string toString(const VersionConstraint& constraint);
std::string toString(const Constraint& constraint);

}
