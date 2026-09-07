#pragma once

#include <string>

namespace nexus {

// Where a component comes from.
//
// The same capability can be met from several places, and they are
// not interchangeable: a base-repository package is integrated and
// small but tied to the distribution's release cadence; a Flatpak is
// current and sandboxed but large; a container gives you another
// distribution's packages at the price of a boundary.
//
// Nexus's job is to say which one it picked and what that costs,
// rather than to pretend they are the same thing.
enum class Source {
    Base,        // the distribution's own repositories
    Flatpak,
    Snap,
    Container,   // distrobox, toolbx: another distribution, isolated
    Nix,
    AppImage,
    Detected     // not installed from anywhere: hardware, the system itself
};

std::string toString(Source source);

// A short description of what choosing this source means, for output
// that has to explain a trade-off rather than state a fact.
std::string describe(Source source);

// Whether components from this source share a root filesystem with
// the base system. The ones that do not are why "packages from
// several distributions" is achievable at all.
bool isIsolated(Source source);

}
