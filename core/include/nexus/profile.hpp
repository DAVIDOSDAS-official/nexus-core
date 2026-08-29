#pragma once

#include <map>
#include <string>
#include <vector>

#include <nexus/requirement.hpp>

namespace nexus {

// A named intent: "I want this machine for gaming."
//
// A profile is not a package list. It is a set of capability
// requirements plus preferences, resolved against whatever is
// actually available. That is what lets the same profile work on
// different bases without being rewritten.
struct Profile {
    std::string name;
    std::string description;

    // Architecture the profile is resolved for. Empty means native.
    std::string architecture;

    // Additional architectures the profile needs enabled, such as
    // i386 for 32-bit game libraries.
    std::vector<std::string> additionalArchitectures;

    std::vector<Requirement> requirements;

    // capability -> component id. A preference breaks a tie; it never
    // forces an outcome.
    std::map<std::string, std::string> preferred;

    // capability -> component id. Absolute: the solve fails rather
    // than substituting something else.
    std::map<std::string, std::string> required;

    bool empty() const {
        return requirements.empty();
    }
};

}
