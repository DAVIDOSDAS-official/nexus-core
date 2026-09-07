#pragma once

#include <string>
#include <vector>

#include <nexus/component.hpp>

namespace nexus::system {

struct SnapSourceResult {
    std::vector<Component> components;

    std::size_t installed = 0;

    // Bases and the daemon itself: infrastructure rather than things
    // somebody chose. Counted apart so "you have 12 snaps" does not
    // include four that came with the others.
    std::size_t infrastructure = 0;

    std::string error;
};

// What Snap has installed.
//
// Only what is installed, not what the store offers: the store is not
// available as a file to read, and asking it for every capability
// would mean a network round trip per question. Snaps therefore
// explain what is on a machine rather than offering alternatives --
// which is exactly the gap that made a snap's service look like it
// had no owner.
SnapSourceResult readSnaps();

// Exposed for testing against captured output.
SnapSourceResult parseSnapList(const std::string& text);

// snap service units are named snap.<snap>.<app>.service, so the
// owner is in the name and needs no query at all.
std::string snapOwningUnit(const std::string& unitName);

}
