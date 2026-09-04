#pragma once

#include <functional>
#include <string>
#include <vector>

#include <map>

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

    // Version ordering is a property of the ecosystem a component
    // came from, not of the machine doing the comparing.
    //
    // dpkg, rpm and pacman order versions by different rules, and
    // pacman follows rpm in comparing a constraint only as precisely
    // as it was written: "= 2.42.3" matches 2.42.3-1. Running Arch
    // packages through Debian's comparator declared every one of them
    // unsatisfiable on a Debian machine, which looked like the
    // packages being broken.
    //
    // Set one per source; anything without its own falls back to the
    // comparator this was constructed with.
    void setComparatorFor(Source source, VersionComparator comparator);

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
    // requesterArchitecture may be empty, meaning the caller is not
    // tracking architecture; the check then ignores it entirely.
    bool matches(
        const Component& component,
        const Constraint& constraint,
        const std::string& requesterArchitecture = {}
    ) const;

    bool hasComparator() const;

private:
    VersionComparator comparator_;
    std::map<Source, VersionComparator> bySource_;

    const VersionComparator& comparatorFor(Source source) const;
};

}
