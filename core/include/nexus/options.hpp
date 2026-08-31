#pragma once

#include <string>
#include <vector>

#include <nexus/alias.hpp>
#include <nexus/component.hpp>
#include <nexus/solver.hpp>

namespace nexus {

// One way of satisfying a capability, and what taking it would mean.
struct Option {
    std::string component;
    std::string version;
    std::string architecture;

    // Already on this machine.
    bool installed = false;

    // Whether it can be resolved at all right now.
    bool workable = false;

    // How many components it would bring in total, itself included.
    // The number people actually want when choosing between two
    // things that do the same job.
    std::size_t componentCount = 0;

    // Components it would add that are not already installed.
    std::size_t wouldAdd = 0;

    // Why it cannot be used, when it cannot.
    std::string blockedOn;

    // Components already installed that this collides with.
    std::vector<std::string> conflictsWith;
};

struct OptionsReport {
    std::string capability;
    std::vector<Option> options;

    bool anyInstalled() const;
};

// Find every component that could satisfy a capability, and cost each
// one.
//
// The solver answers "does this work". This answers "what are my
// choices, and what does each cost me" -- which is a different
// question, and the one somebody actually has when they are deciding
// rather than checking.
//
// Nothing is chosen and nothing is changed.
OptionsReport findOptions(
    const std::string& capability,
    const std::vector<Component>& universe,
    const std::vector<Component>& installed,
    const Solver& solver,
    const ConflictDetector& detector,
    const std::string& architecture = {},
    const AliasTable& aliases = AliasTable{}
);

}
