#pragma once

#include <set>
#include <string>
#include <vector>

#include <nexus/component.hpp>
#include <nexus/conflict_detector.hpp>

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

// Everything reachable from a set of roots by following requirements.
//
// This is a closure, not a search. The system being analysed is
// already installed and already consistent, so there is nothing to
// choose and nothing to backtrack out of: a requirement is held up by
// every installed component that satisfies it.
//
// Recommendations count as reasons to keep something. Debian's
// Recommends means "you almost certainly want this", and apt keeps
// such packages rather than removing them. Following only hard
// requirements made 381 deliberately-kept packages look abandoned on
// a machine where apt considered 4 removable.
//
// Where more than one installed component satisfies a requirement,
// all of them are kept. That is the conservative direction: it can
// leave something installed that could have gone, but it will never
// propose deleting something that is still in use.
std::set<std::string> reachableFrom(
    const std::vector<Component>& installed,
    const std::set<std::string>& roots,
    const ConflictDetector& detector
);

// Work out what removing one component would take with it.
//
// Two closures: what the roots hold up now, and what they hold up
// without this one. The difference is what the target -- and only the
// target -- was keeping alive.
//
// An earlier version re-solved the whole system instead. That is the
// wrong tool: the solver exists to choose between options, and on a
// desktop with a thousand roots it explored itself into its own
// decision limit and could answer nothing at all.
RemovalPlan planRemoval(
    const std::string& target,
    const std::vector<Component>& installed,
    const std::set<std::string>& roots,
    const ConflictDetector& detector,
    const std::string& architecture = {}
);

}
