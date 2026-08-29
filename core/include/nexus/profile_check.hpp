#pragma once

#include <string>
#include <vector>

#include <nexus/profile.hpp>
#include <nexus/solver.hpp>

namespace nexus {

// The state of one requirement in a profile.
struct ProfileItem {
    std::string requirement;
    bool satisfied = false;

    // What satisfies it, when it is satisfied.
    std::vector<std::string> provided;

    // What stopped it, when it is not.
    std::string blockedOn;
};

struct ProfileReport {
    std::string profile;
    std::string description;

    std::vector<ProfileItem> items;

    // Conditional preferences whose condition held on this machine,
    // and those that did not. Both are reported: a preference that
    // silently did not apply is indistinguishable from one that was
    // never written.
    std::vector<std::string> appliedPreferences;
    std::vector<std::string> inactivePreferences;

    std::size_t satisfied = 0;
    std::size_t missing = 0;

    bool complete() const {
        return missing == 0;
    }
};

// Check each requirement of a profile separately.
//
// Solving the whole profile at once answers "does all of this work
// together", which is one bit of information. Checking requirement by
// requirement answers "what exactly is missing", which is what
// somebody setting up a machine actually needs to know.
ProfileReport checkProfile(
    const Profile& profile,
    const Solver& solver
);

}
