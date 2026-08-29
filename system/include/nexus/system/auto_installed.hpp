#pragma once

#include <set>
#include <string>

namespace nexus::system {

// Package names apt recorded as pulled in automatically, read from
// its extended_states file.
//
// This is what separates the components someone asked for from the
// ones that merely came along. Without it, "what can be removed" is
// unanswerable: every installed package looks equally wanted.
//
// Entries are keyed by "name:architecture", matching how components
// are identified elsewhere. Returns an empty set when the file is
// absent -- which is honest, not silent: with no record of what was
// automatic, nothing can be assumed automatic.
std::set<std::string> readAutoInstalled(
    const std::string& path = "/var/lib/apt/extended_states"
);

}
