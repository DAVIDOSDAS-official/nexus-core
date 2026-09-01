#pragma once

#include <string>
#include <vector>

#include <nexus/profile.hpp>

namespace nexus {

// Where two profiles disagree about the same capability.
//
// Not an error. "school" prefers LibreOffice and "gaming" prefers
// nothing in particular; but if two profiles name different preferred
// providers for one capability, somebody has to win, and the person
// combining them deserves to know it happened.
struct PreferenceClash {
    std::string capability;
    std::string chosen;
    std::string overridden;
    std::string chosenBy;
    std::string overriddenBy;
};

struct Composition {
    Profile profile;

    std::vector<std::string> sources;
    std::vector<PreferenceClash> clashes;

    // Requirements that more than one profile asked for. Reported
    // because "school + gaming installs 340 things" is easier to
    // accept when you can see the 40 they share.
    std::vector<std::string> shared;
};

// Combine profiles into one.
//
// The union of what they require, because somebody who picks school
// and gaming wants a spreadsheet and Steam, and telling them that is
// no longer minimalism helps nobody.
//
// Requirements are deduplicated by their written form, preferences
// merge with the first profile winning any clash, and architectures
// accumulate.
Composition compose(
    const std::vector<Profile>& profiles,
    const std::string& name = "combined"
);

}
