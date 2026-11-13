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

// Everything a package manager says it would do, by kind. Read from
// its own dry run (0.1.91, round-4 audit M2): until then only the rows
// under Installing/Upgrading/Reinstalling were read, so a plan that
// also removed, downgraded or replaced packages looked like one that
// only installed, and dnf's "replacing" lines were taken for a package
// called "replacing".
struct PackageChanges {
    std::set<std::string> install;     // new, and their dependencies
    std::set<std::string> upgrade;
    std::set<std::string> reinstall;
    std::set<std::string> downgrade;
    std::set<std::string> remove;      // removed, dependents, unused
    std::set<std::string> replaced;    // obsoleted by something new

    // What a person approving "install X" would not expect.
    bool beyondInstalling() const {
        return !downgrade.empty() || !remove.empty() || !replaced.empty();
    }
    // One line per change other than installing, e.g. "remove foo".
    std::vector<std::string> describeBeyondInstalling() const;
};

// dnf 4 and dnf 5 transaction tables, in the C locale.
PackageChanges parseDnfTransaction(const std::string& text);

// `apt-get -s` lines: "Inst name ...", "Remv name ...".
PackageChanges parseAptSimulation(const std::string& text);

// `rpm-ostree install --dry-run` lists, e.g.
//     Installing 193 packages:
//       vlc-1:3.0.24-1.fc44.x86_64 (updates)
PackageChanges parseRpmOstreeDryRun(const std::string& text);

// "vlc-1:3.0.24-1.fc44.x86_64" -> "vlc"; "bar.x86_64" -> "bar".
std::string packageNameFromNevra(const std::string& nevra);

struct PlanCheck {
    PlanAgreement agreement = PlanAgreement::Unavailable;

    std::set<std::string> theirs;

    // The whole of what the tool said it would do.
    PackageChanges changes;

    // rpm-ostree only: packages it lists that the running system
    // already has (layered earlier, recomputed and possibly moved to a
    // newer version). Not new to the machine.
    std::set<std::string> refreshed;

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

// What rpm-ostree would layer for `requested`, against the packages
// approved (0.1.91, round-4 audit M3). On the image, dnf only checks
// the plan; rpm-ostree carries it out, with its own resolver, and it
// takes weak dependencies dnf's check leaves out. Its list, less what
// the running system already has, is what will be new. theirs = that
// list; onlyTheirs = new packages nobody approved.
PlanCheck checkPlanWithRpmOstree(
    const std::string& requested,
    const std::set<std::string>& approved
);

// What dnf / apt would remove for `names`, asked before anything is
// removed (0.1.91, round-4 audit M1). `dnf remove` also takes every
// package that depends on a target, and the unused dependencies left
// behind; Nexus's own plan cannot see rpm's rich dependencies. The
// tool's own answer is the list a person confirms.
PlanCheck checkRemovalWithDnf(
    const std::vector<std::string>& names,
    const std::set<std::string>& planned
);
PlanCheck checkRemovalWithApt(
    const std::vector<std::string>& names,
    const std::set<std::string>& planned
);

// The rule for going ahead, apart from the screen and the prompt, so it
// can be tested on its own.
struct ApplyDecision {
    bool allowed = false;
    // The list that will really be installed or removed: the tool's.
    std::set<std::string> willChange;
    // Changes beyond the obvious ("remove foo", "downgrade bar"); they
    // need a typed "yes", and are refused under -y.
    std::vector<std::string> alsoChanges;
    bool typedConfirmation = false;
    std::string reason;   // when not allowed
};

// install: `manager` is dnf's or apt's check; `layer` is rpm-ostree's,
// or nullptr where nothing is layered.
ApplyDecision decideInstall(
    const std::string& requested,
    const std::set<std::string>& expected,
    const PlanCheck& manager,
    const PlanCheck* layer,
    bool assumeYes
);

ApplyDecision decideRemoval(
    const std::set<std::string>& planned,
    const PlanCheck& check,
    bool assumeYes
);

}
