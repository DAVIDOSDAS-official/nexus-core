#pragma once

#include <string>
#include <vector>

#include <nexus/component.hpp>
#include <nexus/conflict_detector.hpp>

namespace nexus {

// One thing to do, and what it waited for.
struct TransactionStep {
    std::string component;

    // Components that had to be in place first, at most a few, for
    // reporting rather than for execution.
    std::vector<std::string> after;

    // True when this step is only orderable because a cycle was
    // broken around it -- see below.
    bool inCycle = false;
};

// A group of components that depend on each other in a loop.
//
// Debian's archive genuinely contains these. dpkg handles them by
// unpacking every member first and configuring them afterwards, which
// works because unpacking has weaker requirements than configuring.
// A cycle that contains a pre-dependency cannot be broken that way,
// because a pre-dependency must be configured before its dependent is
// even unpacked.
struct TransactionCycle {
    std::vector<std::string> members;
    bool containsPreDependency = false;
};

enum class TransactionStatus {
    Ready,
    Blocked
};

struct TransactionPlan {
    TransactionStatus status = TransactionStatus::Blocked;

    // Configure order. Everything a step depends on appears earlier,
    // except where a cycle was broken.
    std::vector<TransactionStep> steps;

    std::vector<TransactionCycle> cycles;

    std::string reason;

    bool ready() const {
        return status == TransactionStatus::Ready;
    }
};

// Put a resolved set of components into an order they can be applied
// in.
//
// Resolution says what; ordering says when. They are separate
// problems: a set can be perfectly resolvable and still have no valid
// order, and the failure needs to be reported as an ordering failure
// rather than disguised as a resolution one.
//
// Nothing here executes anything.
TransactionPlan planTransaction(
    const std::vector<std::string>& selected,
    const std::vector<Component>& universe,
    const ConflictDetector& detector
);

std::string toString(TransactionStatus status);

}
