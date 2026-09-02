#pragma once

#include <string>
#include <vector>
#include <vector>

namespace nexus::system {

enum class ApplyOutcome {
    Applied,
    NeedsRoot,
    Refused,
    Failed,
    Unavailable
};

struct ApplyResult {
    ApplyOutcome outcome = ApplyOutcome::Unavailable;

    int exitCode = 0;

    std::vector<std::string> output;
};

// Carry out an installation by asking apt to do it.
//
// Nexus does not move files, run maintainer scripts, or write to the
// package database. Those belong to the tool that owns them, and
// reimplementing them here would be building something that already
// exists and getting it wrong on the first unusual package -- the
// same rule that says not to write a package manager.
//
// What Nexus contributes is the decision and the explanation: what to
// install, why, what it costs, and whether the plan is one the system
// will actually accept. The mechanics stay with apt, including its
// ordering, its failure handling, and its own record of what changed.
//
// Only the requested package is passed. Handing apt the whole
// computed set would override its judgement with ours, and the point
// of verifying first was to establish that the two agree.
ApplyResult applyWithApt(const std::string& requested);

// Remove a package by asking apt to do it.
//
// --autoremove is deliberately not passed. Nexus already worked out
// what becomes unused and showed it; letting apt decide again would
// mean the list on screen and the list removed could differ, and the
// person agreed to the one on screen.
ApplyResult removeWithApt(
    const std::vector<std::string>& packages
);

ApplyResult applyWithDnf(const std::string& requested);

ApplyResult removeWithDnf(
    const std::vector<std::string>& packages
);

bool haveRootPrivileges();

std::string toString(ApplyOutcome outcome);

}
