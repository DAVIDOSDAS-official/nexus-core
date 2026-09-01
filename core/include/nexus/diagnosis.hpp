#pragma once

#include <set>
#include <string>
#include <vector>

#include <nexus/component.hpp>
#include <nexus/conflict_detector.hpp>

namespace nexus {

enum class Health {
    Ok,
    Warning,
    Problem,
    Unknown
};

// One thing that was checked, and what was found.
struct Finding {
    std::string check;
    Health health = Health::Unknown;

    std::string detail;

    // A few named examples, so the finding is actionable rather than
    // a number. Never the whole list: a diagnosis nobody reads is a
    // diagnosis that did not happen.
    std::vector<std::string> examples;

    std::size_t total = 0;

    // A command that would help, from a fixed vocabulary. Empty when
    // nothing known applies -- saying "I do not know how to fix this"
    // is better than a generated command somebody pastes as root.
    std::string suggestion;
};

struct Diagnosis {
    std::vector<Finding> findings;

    Health overall() const;
};

// Check what can be checked from package metadata alone.
//
// Every one of these is a question the resolver can already answer;
// doctor is the command that asks all of them at once instead of
// requiring somebody to know which to ask.
// protectedIds are components that must never be proposed for
// removal even when nothing needs them -- the running kernel above
// all. The graph is right that nothing requires it; acting on that
// would leave a machine that does not boot.
Diagnosis diagnose(
    const std::vector<Component>& installed,
    const std::set<std::string>& roots,
    const ConflictDetector& detector,
    const std::set<std::string>& protectedIds = {}
);

std::string toString(Health health);

}
