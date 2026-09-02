#pragma once

#include <set>
#include <string>
#include <vector>

namespace nexus::system {

enum class PlanAgreement {
    Agrees,        // the package manager would do the same thing
    Differs,       // it would do something else
    Refused,       // it will not do it at all, and said why
    Unavailable    // it could not be asked
};

struct PlanCheck {
    PlanAgreement agreement = PlanAgreement::Unavailable;

    std::set<std::string> theirs;

    std::vector<std::string> onlyOurs;
    std::vector<std::string> onlyTheirs;

    // What the package manager said when it refused. Kept in full:
    // the message is the entire value of asking.
    std::vector<std::string> refusal;

    bool safeToApply() const {
        return agreement == PlanAgreement::Agrees;
    }
};

// Ask the package manager what it would do, without doing it.
//
// This is an interlock, not a debugging aid. Nexus reasons from
// metadata, and metadata does not describe repository architecture
// restrictions, holds, pins or phased updates. apt knows those.
// Modelling every one of them faithfully is a race that gets lost
// slowly; asking the tool that would carry out the work is a race
// that is not run.
//
// A plan Nexus considers perfect can be impossible. That has already
// happened: a thirteen-component Steam plan, internally consistent,
// that apt refused because the only 32-bit build of a Multi-Arch:
// same library was a different version from the installed 64-bit one.
PlanCheck checkPlanWithApt(
    const std::string& requested,
    const std::set<std::string>& expected
);

// The same question, asked of dnf.
//
// dnf's dry run reports what it would install in a table rather than
// as "Inst" lines, so the parsing differs; the interlock does not.
PlanCheck checkPlanWithDnf(
    const std::string& requested,
    const std::set<std::string>& expected
);

}
