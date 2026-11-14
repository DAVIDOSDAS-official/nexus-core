#pragma once

#include <string>
#include <vector>
#include <vector>

namespace nexus::system {

enum class ApplyOutcome {
    // Written to a new deployment, and not yet in effect. The
    // machine has to reboot before any of it is true.
    Staged,
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

// Layer packages on an image-based system.
//
// rpm-ostree writes a new deployment rather than changing the running
// one, so nothing takes effect until a reboot. The result says so:
// Staged rather than Applied, because a change that has not happened
// yet must not be reported as one that has.
// Every name is checked (isPackageName) and quoted; a name that is not
// a package name refuses the whole call (0.1.84, outside review: names
// from repository metadata reached a root shell unquoted).
ApplyResult layerWithRpmOstree(const std::vector<std::string>& names);

// Whether this machine is image-based: /usr read-only and rpm-ostree
// present to layer onto it.
bool isImageBased();

// The same question about the tree at `root` (tests). An ostree
// system marks itself with /run/ostree-booted; failing that, a
// read-only /usr is asked of the kernel (statvfs), not of findmnt:
// on a bootc image with composefs /usr is not a mount point of its
// own, findmnt printed nothing, and 0.1.91 took the Asus for a
// mutable system (14 Nov), so rpm-ostree's list was never asked for.
bool isImageBasedAt(const std::string& root);

ApplyResult removeWithDnf(
    const std::vector<std::string>& packages
);

bool haveRootPrivileges();

std::string toString(ApplyOutcome outcome);

}
