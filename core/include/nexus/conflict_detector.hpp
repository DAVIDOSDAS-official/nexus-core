#pragma once

#include <functional>
#include <string>
#include <vector>

#include <nexus/component.hpp>
#include <nexus/constraint.hpp>

namespace nexus {

// Compares two version strings, returning a negative value, zero, or a
// positive value.
//
// Core does not know how any particular distribution orders versions,
// so the algorithm is supplied by the system layer. This keeps the
// conflict model independent of any one package ecosystem.
using VersionComparator =
    std::function<int(const std::string&, const std::string&)>;

// One detected conflict between two components.
struct Conflict {
    std::string componentId;        // the component declaring it
    std::string conflictsWith;      // the component it collides with
    Constraint declared;            // the constraint as written
    std::string reason;
};

class ConflictDetector {
public:
    // The comparator may be left empty, in which case versioned
    // constraints are treated as applying to every version. That is
    // deliberately conservative: it can report a conflict that a real
    // comparison would rule out, but it never hides one.
    explicit ConflictDetector(VersionComparator comparator = {});

    // Every conflict among the given components.
    std::vector<Conflict> detect(
        const std::vector<Component>& components
    ) const;

    // Conflicts that adding one component to an existing set would
    // introduce, in both directions: the new component conflicting
    // with something installed, and something installed conflicting
    // with the new component.
    std::vector<Conflict> check(
        const Component& candidate,
        const std::vector<Component>& installed
    ) const;

    // Whether a component satisfies a constraint: it must provide the
    // capability, and its version must meet any version condition.
    bool matches(
        const Component& component,
        const Constraint& constraint
    ) const;

    bool hasComparator() const;

private:
    VersionComparator comparator_;
};

}
