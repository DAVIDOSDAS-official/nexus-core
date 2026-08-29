#pragma once

#include <set>
#include <string>
#include <vector>

#include <nexus/solver.hpp>

namespace nexus {

// What removing something would actually cost.
struct RemovalPlan {
    std::string target;

    // True when the target can go: nothing else still needs it.
    bool possible = false;

    // The target plus everything that was only there for it.
    std::vector<std::string> removed;

    // Components that would be left with nothing needing them.
    // Reported separately because they are a consequence of the
    // request rather than the request itself.
    std::vector<std::string> orphaned;

    // When removal is blocked: what still requires the target.
    std::vector<std::string> requiredBy;

    std::string reason;
};

// Work out what removing one component would take with it.
//
// The method is a re-solve rather than a graph walk: resolve the
// system again from the things that were explicitly wanted, minus
// this one, and see what no longer appears. Anything that drops out
// was only present to satisfy the thing being removed.
//
// This is exact where reference counting is approximate, and it falls
// out of already having a solver rather than needing new machinery.
RemovalPlan planRemoval(
    const std::string& target,
    const std::vector<Component>& installed,
    const std::set<std::string>& roots,
    const Solver& solver,
    const std::string& architecture = {}
);

}
