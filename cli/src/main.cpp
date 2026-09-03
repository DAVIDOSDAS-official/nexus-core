#include <algorithm>
#include <exception>
#include <iostream>
#include <istream>
#include <cstdlib>
#include <fstream>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <nexus/component.hpp>
#include <nexus/conflict_detector.hpp>
#include <nexus/profile_check.hpp>
#include <nexus/alias.hpp>
#include <nexus/composition.hpp>
#include <nexus/diagnosis.hpp>
#include <nexus/options.hpp>
#include <nexus/system/alias_file.hpp>
#include <nexus/removal.hpp>
#include <nexus/transaction.hpp>
#include <nexus/system/auto_installed.hpp>
#include <nexus/solver.hpp>
#include <nexus/hardware/encryption.hpp>
#include <nexus/hardware/hardware.hpp>
#include <nexus/system/apt_source.hpp>
#include <nexus/system/flatpak_source.hpp>
#include <nexus/system/rpm_database.hpp>
#include <nexus/system/plan_apply.hpp>
#include <nexus/system/transaction_log.hpp>
#include <nexus/system/plan_check.hpp>
#include <nexus/system/protection.hpp>
#include <nexus/system/rpm_version.hpp>
#include <nexus/system/rpm_repo.hpp>
#include <nexus/system/profile_file.hpp>
#include <nexus/system/dpkg_source.hpp>
#include <nexus/system/version.hpp>

namespace {

using nexus::Capability;
using nexus::Component;
using nexus::system::DpkgSourceResult;
using nexus::system::ModelGap;

void printUsage() {
    std::cout
        << "nexus - read-only system inspection\n"
        << "\n"
        << "Usage:\n"
        << "    nexus scan\n"
        << "    nexus what-provides <capability>\n"
        << "    nexus why <component>\n"
        << "    nexus inspect <component>\n"
        << "    nexus gaps [kind]\n"
        << "    nexus conflicts\n"
        << "    nexus options <capability>\n"
        << "    nexus install <capability> [--apply] [--yes]\n"
        << "    nexus image <profile>\n"
        << "    nexus plan <capability>\n"
        << "    nexus remove <component> [--apply] [--yes]\n"
        << "    nexus doctor\n"
        << "    nexus history\n"
        << "    nexus hardware\n"
        << "    nexus profile list\n"
        << "    nexus profile show <name>\n"
        << "    nexus profile check <name>[,<name>...]\n"
        << "    nexus solve <capability> [--arch a] [--prefer id]\n"
        << "                             [--require id]\n"
        << "\n"
        << "Options:\n"
        << "    --status <path>   dpkg status file\n"
        << "    --profiles <dir>  profile directory\n"
        << "    --arch <arch>     target architecture\n"
        << "    --with-available  also read available packages\n"
        << "    --with-flatpak    also offer Flatpak applications\n"
        << "    --rpm | --dpkg    force a package ecosystem\n"
        << "    --rpm-root <dir>  inspect an rpm root elsewhere\n"
        << "    --explain         reason for every component, not just choices\n"
        << "    --lists <dir>     apt lists directory\n"
        << "    --sysfs <dir>     root for hardware detection\n"
        << "                      (default: /var/lib/dpkg/status)\n"
        << "\n"
        << "This command never modifies the system.\n";
}

const Component* find(
    const std::vector<Component>& components,
    const std::string& id
) {
    for (const Component& component : components) {
        if (component.id() == id) {
            return &component;
        }
    }

    // On a multi-arch system ids are qualified ("libc6:amd64"), so a
    // plain name still has to resolve. If it is ambiguous, say so
    // rather than picking one silently.
    std::vector<const Component*> byName;

    for (const Component& component : components) {
        if (component.name() == id) {
            byName.push_back(&component);
        }
    }

    if (byName.size() == 1) {
        return byName.front();
    }

    if (byName.size() > 1) {
        std::cout
            << id << " is installed for more than one architecture:\n";

        for (const Component* component : byName) {
            std::cout << "    " << component->id() << "\n";
        }

        std::cout << "\nName one of them explicitly.\n";

        // Reported here; the caller must not also say "unknown".
        return nullptr;
    }

    std::cout << "Unknown component: " << id << "\n";

    return nullptr;
}

bool provides(const Component& component, const std::string& capability) {
    for (const Capability& provided : component.providedCapabilities()) {
        if (provided.name() == capability) {
            return true;
        }
    }

    return false;
}

bool requires_(const Component& component, const std::string& capability) {
    for (const Capability& required : component.requiredCapabilities()) {
        if (required.name() == capability) {
            return true;
        }
    }

    return false;
}

// Which version algorithm to use is a property of the ecosystem, not
// a detail. dpkg and rpm order versions differently, and running one
// system's metadata through the other's comparator produces answers
// that look reasonable and are wrong.
bool gUseRpmVersions = false;

nexus::VersionComparator versionComparator() {
    if (gUseRpmVersions) {
        return [](const std::string& provided,
                  const std::string& required) {
            return nexus::system::compareRpmConstraint(
                provided, required);
        };
    }

    return [](const std::string& left, const std::string& right) {
        return nexus::system::compareVersions(left, right);
    };
}

nexus::Solver buildSolver(const std::vector<Component>& components) {
    return nexus::Solver(
        components,
        nexus::ConflictDetector(versionComparator())
    );
}

// What a package source reported, without saying which one it was.
//
// Nexus reasons about capabilities, so the parts of scan that are
// interesting -- how much of the metadata the model holds, what it
// could not represent -- are the same question whichever ecosystem
// answered it.
struct SourceSummary {
    std::string name;
    std::vector<Component> components;

    std::size_t recordsRead = 0;
    std::size_t recordsSkipped = 0;
    std::size_t dependencyClauses = 0;
    std::size_t representableClauses = 0;

    std::map<std::string, std::size_t> gaps;
    std::vector<std::string> notes;
};

int commandScan(const SourceSummary& result) {
    std::cout
        << "Source:      " << result.name << "\n"
        << "Records:     " << result.recordsRead << "\n"
        << "Components:  " << result.components.size() << "\n"
        << "Skipped:     " << result.recordsSkipped << "\n"
        << "\n";

    std::size_t capabilities = 0;

    for (const Component& component : result.components) {
        capabilities += component.providedCapabilities().size();
    }

    std::cout
        << "Capabilities provided: " << capabilities << "\n"
        << "Dependency clauses:    " << result.dependencyClauses << "\n";

    if (result.dependencyClauses > 0) {
        const double share =
            100.0 * static_cast<double>(result.representableClauses) /
            static_cast<double>(result.dependencyClauses);

        std::cout
            << "Representable:         "
            << result.representableClauses
            << " (" << static_cast<int>(share) << "%)\n";
    }

    std::cout << "\nModel gaps:\n";

    if (result.gaps.empty()) {
        std::cout << "    none\n";
    }

    for (const auto& [kind, count] : result.gaps) {
        std::cout << "    " << kind << ": " << count << "\n";
    }

    for (const std::string& note : result.notes) {
        std::cout << "\n" << note << "\n";
    }

    return 0;
}

int commandWhatProvides(
    const std::vector<Component>& components,
    const std::string& capability
) {
    std::vector<std::string> providers;

    for (const Component& component : components) {
        if (provides(component, capability)) {
            providers.push_back(component.id());
        }
    }

    std::cout << "Capability:\n    " << capability << "\n\n";

    if (providers.empty()) {
        std::cout << "No installed component provides this capability.\n";
        return 1;
    }

    std::cout << "Provided by:\n";

    for (const std::string& provider : providers) {
        std::cout << "    " << provider << "\n";
    }

    if (providers.size() > 1) {
        std::cout
            << "\nResolution status:\n"
            << "    Ambiguous - "
            << providers.size()
            << " providers, no preference given.\n";
    }

    return 0;
}

int commandWhy(
    const std::vector<Component>& components,
    const std::string& id
) {
    const Component* target = find(components, id);

    if (target == nullptr) {
        return 1;
    }

    std::cout << "Component:\n    " << id << "\n\n";

    std::vector<std::string> dependents;

    for (const Component& component : components) {
        if (component.id() == id) {
            continue;
        }

        bool matches = requires_(component, id);

        if (!matches) {
            for (const Capability& provided :
                 target->providedCapabilities()) {

                if (requires_(component, provided.name())) {
                    matches = true;
                    break;
                }
            }
        }

        if (matches) {
            dependents.push_back(component.id());
        }
    }

    if (dependents.empty()) {
        std::cout
            << "Nothing installed depends on it.\n"
            << "It was requested directly or is no longer needed.\n";
        return 0;
    }

    std::cout << "Required by:\n";

    for (const std::string& dependent : dependents) {
        std::cout << "    " << dependent << "\n";
    }

    std::cout
        << "\nReason:\n    "
        << dependents.size()
        << " installed component(s) declare a dependency on it.\n";

    return 0;
}

int commandInspect(
    const std::vector<Component>& components,
    const std::string& id
) {
    const Component* component = find(components, id);

    if (component == nullptr) {
        return 1;
    }

    std::cout
        << "ID:        " << component->id() << "\n"
        << "Version:   " << component->version() << "\n"
        << "Type:      " << toString(component->type()) << "\n"
        << "Arch:      " << component->architecture()
        << "  (multi-arch: "
        << nexus::toString(component->multiArch()) << ")\n";

    std::cout << "\nProvides:\n";

    for (const Capability& capability : component->providedCapabilities()) {
        std::cout << "    " << capability.name() << "\n";
    }

    std::cout << "\nRequires:\n";

    if (component->requiredCapabilities().empty()) {
        std::cout << "    (none)\n";
    }

    for (const Capability& capability : component->requiredCapabilities()) {
        std::cout << "    " << capability.name() << "\n";
    }

    return 0;
}

int commandConflicts(const std::vector<Component>& components) {
    const nexus::ConflictDetector detector(versionComparator());

    std::size_t declared = 0;

    for (const Component& component : components) {
        declared += component.conflicts().size();
    }

    std::cout
        << "Components:          " << components.size() << "\n"
        << "Declared conflicts:  " << declared << "\n\n";

    const auto conflicts = detector.detect(components);

    if (conflicts.empty()) {
        std::cout
            << "No active conflicts.\n"
            << "Every declared conflict refers to something that is\n"
            << "either not installed or at a version that is allowed.\n";
        return 0;
    }

    std::cout << "Active conflicts (" << conflicts.size() << "):\n\n";

    for (const nexus::Conflict& conflict : conflicts) {
        std::cout << "    " << conflict.reason << "\n";
    }

    return 1;
}

int commandSolve(
    const nexus::AliasTable& aliases,
    const std::vector<Component>& universe,
    const std::string& capability,
    const std::string& prefer,
    const std::string& require,
    const std::string& architecture,
    bool explain
) {
    nexus::Solver solver = buildSolver(universe);

    nexus::SolverRequest request;
    request.architecture = architecture;
    request.requirements.push_back(
        aliases.expand(nexus::Requirement(nexus::Constraint(capability)))
    );

    if (!prefer.empty()) {
        request.preferred[capability] = prefer;
    }

    if (!require.empty()) {
        request.required[capability] = require;
    }

    const auto solution = solver.solve(request);

    std::cout
        << "Request:     " << capability << "\n"
        << "Arch:        "
        << (architecture.empty() ? "(any)" : architecture) << "\n"
        << "Status:      " << toString(solution.status) << "\n"
        << "Decisions:   " << solution.decisions << "\n"
        << "Backtracks:  " << solution.backtracks << "\n\n";

    if (solution.status != nexus::SolverStatus::Success) {
        std::cout << solution.reason << "\n";

        if (!solution.blockedOn.empty()) {
            // blockedOn may be a capability nothing provides or a
            // rule that was violated. "Nothing available provides"
            // fits only the first.
            std::cout << "\nBlocked on:\n    "
                      << solution.blockedOn << "\n";
        }

        return 1;
    }

    std::size_t choices = 0;

    for (const nexus::SolverStep& step : solution.steps) {
        if (step.alternativesConsidered > 1) {
            choices += 1;
        }
    }

    std::cout
        << "Selected " << solution.selected.size()
        << " component(s); " << choices
        << " involved a real choice.\n";

    if (!explain) {
        // A few hundred lines of "only component providing X" buries
        // the handful of lines that actually say something.
        if (choices > 0) {
            std::cout << "\nWhere there was a choice:\n";

            for (const nexus::SolverStep& step : solution.steps) {
                if (step.alternativesConsidered <= 1) {
                    continue;
                }

                std::cout
                    << "    " << step.selected << "\n"
                    << "        " << step.reason << "\n";
            }
        }

        std::cout
            << "\nUse --explain for the reason behind every "
            << "component.\n";

        return 0;
    }

    std::cout << "\nWhy each one:\n";

    for (const nexus::SolverStep& step : solution.steps) {
        std::cout << "    " << step.selected;

        if (step.wasBacktrackedInto) {
            std::cout << "  [after backtracking]";
        }

        std::cout << "\n        " << step.reason << "\n";
    }

    return 0;
}


// Emit the package list a profile resolves to, annotated with the
// capability each line answers.
//
// Only the directly chosen packages, not their dependencies: dnf and
// apt resolve those themselves, and a list of five hundred names is
// not something anybody can read. The point of generating it is that
// it stays readable -- a build you cannot read is a build you cannot
// audit.
// Resolve a profile name, which may be several separated by commas.
//
// Somebody who wants a machine for school and gaming wants both, not
// a choice between them. Returns false and explains when a name does
// not exist.
bool findComposition(
    const std::vector<nexus::Profile>& available,
    const std::string& name,
    nexus::Composition& composition
) {
    std::vector<std::string> wanted;
    std::string current;

    for (char character : name) {
        if (character == ',') {
            if (!current.empty()) {
                wanted.push_back(current);
            }

            current.clear();
            continue;
        }

        current.push_back(character);
    }

    if (!current.empty()) {
        wanted.push_back(current);
    }

    std::vector<nexus::Profile> picked;

    for (const std::string& one : wanted) {
        const nexus::Profile* found = nullptr;

        for (const nexus::Profile& profile : available) {
            if (profile.name == one) {
                found = &profile;
                break;
            }
        }

        if (found == nullptr) {
            std::cerr << "Unknown profile: " << one << "\n";
            return false;
        }

        picked.push_back(*found);
    }

    if (picked.empty()) {
        std::cerr << "No profile named.\n";
        return false;
    }

    composition = nexus::compose(picked, name);

    return true;
}

void reportComposition(const nexus::Composition& composition) {
    if (composition.sources.size() < 2) {
        return;
    }

    std::cout
        << "Combined:    " << composition.sources.size()
        << " profiles, "
        << composition.profile.requirements.size()
        << " requirement(s)";

    if (!composition.shared.empty()) {
        std::cout
            << " (" << composition.shared.size()
            << " shared, asked for more than once)";
    }

    std::cout << "\n";

    for (const auto& clash : composition.clashes) {
        std::cout
            << "  note: " << clash.capability << " -> "
            << clash.chosen << " (from " << clash.chosenBy
            << "), not " << clash.overridden << " (from "
            << clash.overriddenBy << ")\n";
    }

    std::cout << "\n";
}

// Note that this resolves against the archive alone, never against
// what happens to be installed here.
//
// A generated list is a description of a machine that does not exist
// yet, so what is on this one must not decide it -- otherwise two
// people generating the same profile get different files, and a
// profile called minimal picks up whatever desktop the generating
// machine happened to run.
int commandImage(
    const nexus::AliasTable& aliases,
    const std::vector<Component>& universe,
    const std::string& profileName,
    const std::string& directory,
    const std::string& architecture
) {
    const auto loaded =
        nexus::system::parseProfileDirectory(directory);

    nexus::Composition composition;

    if (!findComposition(loaded.profiles, profileName, composition)) {
        return 1;
    }

    if (composition.refused) {
        std::cerr << composition.refusal << "\n";
        return 1;
    }

    nexus::Profile profile = composition.profile;

    if (!architecture.empty()) {
        profile.architecture = architecture;
    }

    nexus::Solver solver = buildSolver(universe);

    std::cout
        << "# Generated by: nexus image " << profile.name << "\n"
        << "# " << profile.description << "\n"
        << "#\n"
        << "# One package per capability the profile asks for. Their\n"
        << "# dependencies are left to the package manager.\n"
        << "#\n"
        << "# Regenerate with:\n"
        << "#     nexus image " << profile.name
        << " --with-available\n"
        << "\n";

    std::size_t unresolved = 0;

    for (const nexus::Requirement& requirement :
         profile.requirements) {

        const nexus::Requirement expanded =
            aliases.expand(requirement);

        nexus::SolverRequest request;

        request.architecture = profile.architecture;
        request.requirements.push_back(expanded);
        request.preferred = profile.preferred;
        request.required = profile.required;

        // A preference keyed by capability does not survive alias
        // expansion: the profile asks for "terminal-emulator", the
        // alternative that matches is "x-terminal-emulator", and the
        // lookup misses. Favouring the component by id works whichever
        // alternative it came in on.
        for (const auto& [capability, component] :
             profile.preferred) {

            request.preferredComponents.insert(component);
        }

        const auto result = solver.solve(request);

        const std::string asked = toString(requirement);

        if (result.status != nexus::SolverStatus::Success ||
            result.steps.empty()) {

            std::cout
                << "# UNRESOLVED: " << asked << "\n";

            unresolved += 1;
            continue;
        }

        // The first step is what satisfied the requirement itself;
        // everything after it is a dependency.
        const std::string selected = result.steps.front().selected;

        // Emit the package name, not the internal id. Ids carry an
        // architecture suffix so that multilib builds stay distinct
        // inside the resolver -- but "kate:amd64" is not something
        // dnf or apt will accept, and a generated list that cannot be
        // fed to a package manager is not a generated list.
        std::string package = selected;

        for (const Component& component : universe) {
            if (component.id() == selected) {
                package = component.name();
                break;
            }
        }

        // Hardware capabilities are facts about the machine, not
        // things to install.
        if (package == "system-hardware") {
            std::cout << "# (hardware) " << asked << "\n";
            continue;
        }

        std::string padded = package;

        while (padded.size() < 32) {
            padded.push_back(' ');
        }

        std::cout << padded << "# " << asked << "\n";
    }

    if (unresolved > 0) {
        std::cerr
            << unresolved
            << " requirement(s) could not be resolved; the list above "
            << "is incomplete.\n";

        return 1;
    }

    return 0;
}

// Check a plan against the package manager's own engine, without
// changing anything.
//
// Resolution being correct and installation succeeding are different
// claims. Nexus can prove the first from metadata; only the thing
// that actually installs packages can speak to the second. apt will
// say what it would do without doing it, so the plan can be checked
// against the tool that would carry it out.
//
// Every other correctness question in this project was settled by
// comparing against a native tool rather than by another assertion.
// This is that, applied to the one part metadata cannot answer.
int commandInstall(
    const nexus::AliasTable& aliases,
    const std::vector<Component>& universe,
    const std::vector<Component>& installed,
    const std::string& capability,
    const std::string& architecture,
    bool useRpm,
    bool apply,
    bool assumeYes,
    const std::string& invocation
) {
    nexus::SolverRequest request;

    request.architecture = architecture;
    request.requirements.push_back(
        aliases.expand(nexus::Requirement(nexus::Constraint(capability)))
    );

    const auto solution = buildSolver(universe).solve(request);

    if (solution.status != nexus::SolverStatus::Success) {
        std::cout
            << "Cannot be resolved: " << solution.reason << "\n";

        if (!solution.blockedOn.empty()) {
            // blockedOn may be a capability nothing provides or a
            // rule that was violated. "Nothing available provides"
            // fits only the first.
            std::cout << "\nBlocked on:\n    "
                      << solution.blockedOn << "\n";
        }

        return 1;
    }

    std::set<std::string> here;

    for (const Component& component : installed) {
        here.insert(component.name() + ":" + component.architecture());
    }

    std::set<std::string> expected;
    std::string requested;

    for (const std::string& id : solution.selected) {
        for (const Component& component : universe) {
            if (component.id() != id) {
                continue;
            }

            if (requested.empty()) {
                requested = component.name();
            }

            if (here.count(component.name() + ":" +
                           component.architecture()) == 0) {
                expected.insert(component.name());
            }

            break;
        }
    }

    std::cout
        << "Request:   " << capability << "\n"
        << "Resolves:  " << requested << "\n"
        << "New:       " << expected.size() << " component(s)\n";

    // The interlock is the same either way; only the tool asked
    // differs.
    const auto check = useRpm
        ? nexus::system::checkPlanWithDnf(requested, expected)
        : nexus::system::checkPlanWithApt(requested, expected);

    const std::string manager = useRpm ? "dnf" : "apt";

    switch (check.agreement) {
        case nexus::system::PlanAgreement::Agrees:
            std::cout
                << "Verified:  " << manager
                << " would do the same thing.\n";
            break;

        case nexus::system::PlanAgreement::Unavailable:
            std::cout
                << "\n" << manager << " could not be asked, so this "
                << "plan is unverified.\nThat is not the same as "
                << manager << " objecting to it.\n";
            break;

        case nexus::system::PlanAgreement::Refused:
            std::cout
                << "\n" << manager << " refuses this plan:\n";

            for (const std::string& message : check.refusal) {
                std::cout << "    " << message << "\n";
            }

            std::cout
                << "\nNexus resolved it from metadata, which does "
                << "not describe\nrepository restrictions, holds or "
                << "pins. " << manager << " knows those.\n";
            break;

        case nexus::system::PlanAgreement::Differs: {
            std::cout
                << manager << " would install: "
                << check.theirs.size()
                << "\n\nThe plans differ.\n";

            const auto show =
                [](const std::string& title,
                   const std::vector<std::string>& names) {
                    if (names.empty()) {
                        return;
                    }

                    std::cout << "\n" << title << ":\n";

                    std::size_t shown = 0;

                    for (const std::string& name : names) {
                        std::cout << "    " << name << "\n";

                        if (++shown >= 10) {
                            std::cout
                                << "    ... and "
                                << (names.size() - shown) << " more\n";
                            break;
                        }
                    }
                };

            show("Nexus expects, " + manager + " does not",
                 check.onlyOurs);
            show(manager + " expects, Nexus does not",
                 check.onlyTheirs);

            std::cout
                << "\nA difference is not automatically a fault: "
                << manager << " applies\npolicy Nexus does not "
                << "model.\n";
            break;
        }
    }

    if (!apply) {
        std::cout
            << "\nNothing has been changed. Use --apply to install.\n";

        return check.agreement == nexus::system::PlanAgreement::Agrees
            ? 0
            : 1;
    }

    // The interlock. A plan the package manager will not agree to is
    // not applied, whatever Nexus thinks of it.
    if (!check.safeToApply()) {
        std::cout
            << "\nRefusing to apply an unverified plan.\n"
            << "Nothing has been changed.\n";

        return 1;
    }

    if (!nexus::system::haveRootPrivileges()) {
        // The original arguments, not a reconstruction. A suggested
        // command that drops the flags which made it work is worse
        // than no suggestion.
        std::cout
            << "\nInstalling needs root. Re-run with sudo:\n"
            << "    sudo " << invocation << "\n"
            << "\nNothing has been changed.\n";

        return 1;
    }

    // The plan is shown before anything happens, and the person says
    // yes to that plan rather than to a question with no content.
    if (!assumeYes) {
        std::cout << "\nWould install:\n";

        std::size_t shown = 0;

        for (const std::string& name : expected) {
            std::cout << "    " << name << "\n";

            if (++shown >= 20) {
                std::cout
                    << "    ... and " << (expected.size() - shown)
                    << " more\n";
                break;
            }
        }

        std::cout << "\nProceed? [y/N] ";

        std::string answer;

        std::getline(std::cin, answer);

        if (answer != "y" && answer != "Y" && answer != "yes") {
            std::cout << "Nothing has been changed.\n";
            return 1;
        }
    }

    std::cout << "\nHanding the plan to " << manager << ".\n\n";

    const auto applied = useRpm
        ? nexus::system::applyWithDnf(requested)
        : nexus::system::applyWithApt(requested);

    for (const std::string& line : applied.output) {
        std::cout << line << "\n";
    }

    // A tool that changes a system owes an account of what it did.
    nexus::system::TransactionRecord record;

    record.when = nexus::system::currentTimestamp();
    record.request = capability;
    record.resolved = requested;
    record.packages = expected;
    record.outcome = toString(applied.outcome);
    record.exitCode = applied.exitCode;
    record.succeeded =
        applied.outcome == nexus::system::ApplyOutcome::Applied;

    const std::string log = nexus::system::defaultTransactionLog();

    if (!nexus::system::recordTransaction(log, record)) {
        std::cerr
            << "\nWarning: could not write the transaction record to "
            << log << ".\n";
    }

    switch (applied.outcome) {
        case nexus::system::ApplyOutcome::Applied:
            std::cout << "\nDone.\n";
            return 0;

        case nexus::system::ApplyOutcome::NeedsRoot:
            std::cout << "\nInstalling needs root.\n";
            return 1;

        case nexus::system::ApplyOutcome::Unavailable:
            std::cout
                << "\n" << manager << " is not available here.\n";
            return 1;

        default:
            std::cout
                << "\n" << manager << " did not complete (exit "
                << applied.exitCode << ").\n"
                << "It manages its own recovery; the messages above "
                << "are its own.\n";
            return 1;
    }
}

int commandOptions(
    const nexus::AliasTable& aliases,
    const std::vector<Component>& universe,
    const std::vector<Component>& installed,
    const std::string& capability,
    const std::string& architecture
) {
    const nexus::ConflictDetector detector(versionComparator());

    const auto report = nexus::findOptions(
        capability, universe, installed,
        buildSolver(universe), detector, architecture, aliases);

    std::cout << "Capability:  " << capability << "\n";

    if (report.options.empty()) {
        std::cout
            << "\nNothing available provides it.\n"
            << "Try --with-available to include packages that are "
            << "not installed.\n";
        return 1;
    }

    std::cout
        << "Options:     " << report.options.size() << "\n\n";

    for (const nexus::Option& option : report.options) {
        std::cout << "  " << option.component;

        if (!option.version.empty()) {
            std::cout << "  " << option.version;
        }

        if (option.installed) {
            std::cout << "   [installed]";
        }

        // Only worth saying when it is not the ordinary case.
        if (option.source != nexus::Source::Base) {
            std::cout << "   (" << toString(option.source) << ")";
        }

        std::cout << "\n";

        if (option.source != nexus::Source::Base) {
            std::cout
                << "      " << describe(option.source) << "\n";
        }

        if (!option.workable) {
            std::cout
                << "      cannot be used: " << option.blockedOn
                << "\n";
        } else if (option.installed) {
            std::cout
                << "      " << option.componentCount
                << " components, already here\n";
        } else {
            std::cout
                << "      " << option.componentCount
                << " components, " << option.wouldAdd
                << " of them new\n";
        }

        if (!option.conflictsWith.empty()) {
            std::cout << "      collides with ";

            for (std::size_t index = 0;
                 index < option.conflictsWith.size() && index < 3;
                 ++index) {

                if (index > 0) {
                    std::cout << ", ";
                }

                std::cout << option.conflictsWith[index];
            }

            std::cout << "\n";
        }
    }

    if (!report.anyInstalled()) {
        std::cout
            << "\nNothing providing this is installed yet.\n";
    }

    std::cout
        << "\nNothing has been changed; this command only reports.\n";

    return 0;
}

int commandPlan(
    const std::vector<Component>& universe,
    const std::string& capability,
    const std::string& architecture,
    bool explain
) {
    nexus::Solver solver = buildSolver(universe);

    nexus::SolverRequest request;

    request.architecture = architecture;
    request.requirements.push_back(
        nexus::Requirement(nexus::Constraint(capability))
    );

    const auto solution = solver.solve(request);

    std::cout
        << "Request:  " << capability << "\n"
        << "Resolve:  " << toString(solution.status) << "\n";

    if (solution.status != nexus::SolverStatus::Success) {
        std::cout << "\n" << solution.reason << "\n";

        if (!solution.blockedOn.empty()) {
            // blockedOn may be a capability nothing provides or a
            // rule that was violated. "Nothing available provides"
            // fits only the first.
            std::cout << "\nBlocked on:\n    "
                      << solution.blockedOn << "\n";
        }

        return 1;
    }

    const auto plan = nexus::planTransaction(
        solution.selected,
        universe,
        nexus::ConflictDetector(versionComparator())
    );

    std::cout
        << "Order:    " << toString(plan.status) << "\n"
        << "Steps:    " << plan.steps.size() << "\n\n"
        << plan.reason << "\n";

    if (!plan.cycles.empty()) {
        std::cout << "\nDependency cycles:\n";

        for (const nexus::TransactionCycle& cycle : plan.cycles) {
            std::cout << "    ";

            for (std::size_t i = 0; i < cycle.members.size(); ++i) {
                if (i > 0) {
                    std::cout << " <-> ";
                }

                std::cout << cycle.members[i];
            }

            if (cycle.containsPreDependency) {
                std::cout << "   [contains a pre-dependency]";
            }

            std::cout << "\n";
        }
    }

    if (!plan.ready()) {
        return 1;
    }

    if (!explain) {
        std::cout
            << "\nUse --explain for the full order.\n"
            << "Nothing has been changed; this command only "
            << "reports.\n";

        return 0;
    }

    std::cout << "\nOrder:\n";

    std::size_t number = 0;

    for (const nexus::TransactionStep& step : plan.steps) {
        std::cout
            << "    " << ++number << ". " << step.component;

        if (step.inCycle) {
            std::cout << "   [cycle: unpack first, configure after]";
        } else if (!step.after.empty()) {
            std::cout << "   after ";

            for (std::size_t i = 0; i < step.after.size(); ++i) {
                if (i > 0) {
                    std::cout << ", ";
                }

                std::cout << step.after[i];
            }
        }

        std::cout << "\n";
    }

    std::cout
        << "\nNothing has been changed; this command only reports.\n";

    return 0;
}

int commandRemove(
    const std::vector<Component>& installed,
    const std::string& target,
    const std::string& statesPath,
    const std::string& architecture,
    const std::set<std::string>& protectedIds,
    bool apply,
    bool assumeYes,
    bool useRpm,
    const std::string& invocation
) {
    const auto automatic =
        nexus::system::readAutoInstalled(statesPath);

    if (automatic.empty()) {
        std::cerr
            << "Warning: no auto-installed record found at "
            << statesPath << ".\n"
            << "Every package looks explicitly wanted, so nothing "
            << "will appear removable.\n\n";
    }

    // Roots are what was asked for: installed, minus what apt says
    // came along automatically.
    std::set<std::string> roots;

    for (const Component& component : installed) {
        const std::string key =
            component.name() + ":" + component.architecture();

        if (automatic.count(key) == 0) {
            roots.insert(component.id());
        }
    }

    const auto plan = nexus::planRemoval(
        target,
        installed,
        roots,
        nexus::ConflictDetector(versionComparator()),
        architecture,
        protectedIds);

    std::cout
        << "Target:     " << plan.target << "\n"
        << "Roots:      " << roots.size()
        << " explicitly wanted component(s)\n\n";

    if (!plan.possible) {
        std::cout << plan.reason << "\n";

        if (!plan.requiredBy.empty()) {
            std::cout << "\nStill required by:\n";

            std::size_t shown = 0;

            for (const std::string& id : plan.requiredBy) {
                std::cout << "    " << id << "\n";

                if (++shown >= 10) {
                    std::cout
                        << "    ... and "
                        << (plan.requiredBy.size() - shown)
                        << " more\n";
                    break;
                }
            }
        }

        return 1;
    }

    std::cout
        << "Would remove " << plan.removed.size()
        << " component(s):\n\n"
        << "    " << plan.target << "  (requested)\n";

    for (const std::string& id : plan.orphaned) {
        std::cout << "    " << id << "  (nothing else needs it)\n";
    }

    if (!apply) {
        std::cout
            << "\nNothing has been changed. Use --apply to remove.\n";

        return 0;
    }

    if (!nexus::system::haveRootPrivileges()) {
        std::cout
            << "\nRemoving needs root. Re-run with sudo:\n"
            << "    sudo " << invocation << "\n"
            << "\nNothing has been changed.\n";

        return 1;
    }

    // Removing is the dangerous direction, so the confirmation is
    // stricter than for installing: the count is stated and the whole
    // word must be typed. A y/N prompt is answered by reflex.
    if (!assumeYes) {
        std::cout
            << "\nThis will remove " << plan.removed.size()
            << " component(s). Type 'remove' to confirm: ";

        std::string answer;

        std::getline(std::cin, answer);

        if (answer != "remove") {
            std::cout << "Nothing has been changed.\n";
            return 1;
        }
    }

    std::cout
        << "\nHanding the plan to " << (useRpm ? "dnf" : "apt")
        << ".\n\n";

    const auto removed = useRpm
        ? nexus::system::removeWithDnf(plan.removed)
        : nexus::system::removeWithApt(plan.removed);

    for (const std::string& line : removed.output) {
        std::cout << line << "\n";
    }

    nexus::system::TransactionRecord record;

    record.kind = nexus::system::TransactionKind::Remove;
    record.when = nexus::system::currentTimestamp();
    record.request = "remove " + target;
    record.resolved = plan.target;
    record.packages = std::set<std::string>(
        plan.removed.begin(), plan.removed.end());
    record.outcome = toString(removed.outcome);
    record.exitCode = removed.exitCode;
    record.succeeded =
        removed.outcome == nexus::system::ApplyOutcome::Applied;

    const std::string log = nexus::system::defaultTransactionLog();

    if (!nexus::system::recordTransaction(log, record)) {
        std::cerr
            << "\nWarning: could not write the transaction record to "
            << log << ".\n";
    }

    if (removed.outcome == nexus::system::ApplyOutcome::Applied) {
        std::cout << "\nDone.\n";
        return 0;
    }

    std::cout
        << "\n" << (useRpm ? "dnf" : "apt")
        << " did not complete (exit " << removed.exitCode
        << ").\nIt manages its own recovery; the messages above are "
        << "its own.\n";

    return 1;
}

int commandHistory(const std::string& path) {
    const auto records = nexus::system::readTransactions(path);

    if (records.empty()) {
        std::cout
            << "No changes recorded in " << path << ".\n";
        return 0;
    }

    for (const auto& record : records) {
        std::cout
            << record.when << "  " << record.request;

        if (!record.resolved.empty() &&
            record.resolved != record.request) {
            std::cout << " -> " << record.resolved;
        }

        std::cout << "  [" << record.outcome << "]\n";

        if (!record.packages.empty()) {
            std::string verb = "attempted ";

            if (record.succeeded) {
                verb =
                    record.kind ==
                        nexus::system::TransactionKind::Remove
                        ? "removed "
                        : "added ";
            }

            std::cout
                << "    " << verb << record.packages.size() << ": ";

            std::size_t shown = 0;

            for (const std::string& name : record.packages) {
                if (shown > 0) {
                    std::cout << ", ";
                }

                std::cout << name;

                if (++shown >= 8) {
                    std::cout << ", ...";
                    break;
                }
            }

            std::cout << "\n";
        }
    }

    return 0;
}

int commandDoctor(
    const SourceSummary& source,
    const std::vector<Component>& installed,
    const nexus::hardware::HardwareInfo& hardware,
    const nexus::hardware::EncryptionReport& encryption,
    const std::set<std::string>& roots,
    bool havePackages,
    const std::set<std::string>& protectedIds
) {
    std::vector<nexus::Finding> findings;

    // Whether the system can be examined at all comes first: every
    // finding below is worthless if this one failed.
    nexus::Finding source_;

    source_.check = "Package database";

    if (havePackages) {
        source_.health = nexus::Health::Ok;
        source_.detail =
            std::to_string(source.components.size()) +
            " components read from " + source.name + ".";
    } else {
        source_.health = nexus::Health::Problem;
        source_.detail = "No package database could be read.";
        source_.suggestion = "nexus scan";
    }

    findings.push_back(std::move(source_));

    nexus::Finding metal;

    metal.check = "Hardware";

    if (hardware.unreadable.empty()) {
        metal.health = nexus::Health::Ok;
        metal.detail =
            std::to_string(hardware.capabilities().size()) +
            " capabilities detected.";
    } else {
        // Unreadable is not broken. A container has no EFI variables
        // and that is correct, not a fault.
        metal.health = nexus::Health::Warning;
        metal.detail =
            std::to_string(hardware.unreadable.size()) +
            " thing(s) could not be read.";
        metal.examples = hardware.unreadable;
        metal.total = hardware.unreadable.size();
        metal.suggestion = "nexus hardware";
    }

    findings.push_back(std::move(metal));

    // What is protected when the machine is off. Full-disk encryption
    // does nothing while it is running and nothing against root, so
    // the finding says what it covers rather than declaring the
    // system encrypted.
    nexus::Finding locked;

    locked.check = "Encryption";

    if (!encryption.unreadable.empty()) {
        locked.health = nexus::Health::Unknown;
        locked.detail = "Could not be determined.";
        locked.examples = encryption.unreadable;
        locked.total = encryption.unreadable.size();
    } else if (encryption.root != nexus::hardware::Encrypted::Yes) {
        locked.health = nexus::Health::Warning;
        locked.detail =
            "The root filesystem is not encrypted; anyone with the "
            "disk can read it.";
    } else if (encryption.swapLeaksMemory()) {
        // The gap almost nobody notices.
        locked.health = nexus::Health::Warning;
        locked.detail =
            "Root is encrypted, swap is not.";
        locked.examples.push_back(
            "swap holds memory contents in plain text on disk");
        locked.total = 1;
    } else {
        locked.health = nexus::Health::Ok;

        locked.detail = "Root";

        if (encryption.homeIsSeparate) {
            locked.detail +=
                encryption.home == nexus::hardware::Encrypted::Yes
                    ? " and home"
                    : " but not home";
        }

        if (encryption.hasSwap) {
            locked.detail +=
                encryption.swap == nexus::hardware::Encrypted::Yes
                    ? " and swap"
                    : "";
        }

        locked.detail += " encrypted";

        if (!encryption.method.empty()) {
            locked.detail += " (" + encryption.method + ")";
        }

        locked.detail +=
            ". This protects the machine when it is off, not while it "
            "is running.";
    }

    findings.push_back(std::move(locked));

    if (havePackages) {
        const auto detail = nexus::diagnose(
            installed, roots,
            nexus::ConflictDetector(versionComparator()),
            protectedIds);

        for (const nexus::Finding& finding : detail.findings) {
            findings.push_back(finding);
        }
    }

    if (!source.gaps.empty()) {
        nexus::Finding gaps;

        gaps.check = "Model";
        gaps.health = nexus::Health::Warning;

        std::size_t total = 0;

        for (const auto& [kind, count] : source.gaps) {
            total += count;
            gaps.examples.push_back(
                kind + " (" + std::to_string(count) + ")");
        }

        gaps.total = total;
        gaps.detail =
            std::to_string(total) +
            " thing(s) in the metadata the model does not represent.";
        gaps.suggestion = "nexus gaps";

        findings.push_back(std::move(gaps));
    }

    nexus::Health worst = nexus::Health::Ok;

    for (const nexus::Finding& finding : findings) {
        if (finding.health == nexus::Health::Problem) {
            worst = nexus::Health::Problem;
        } else if (finding.health == nexus::Health::Warning &&
                   worst == nexus::Health::Ok) {
            worst = nexus::Health::Warning;
        }
    }

    for (const nexus::Finding& finding : findings) {
        std::string mark;

        switch (finding.health) {
            case nexus::Health::Ok:      mark = " ok "; break;
            case nexus::Health::Warning: mark = "warn"; break;
            case nexus::Health::Problem: mark = "FAIL"; break;
            case nexus::Health::Unknown: mark = " ?  "; break;
        }

        std::string name = finding.check;

        while (name.size() < 18) {
            name.push_back(' ');
        }

        std::cout
            << "  [" << mark << "]  " << name
            << finding.detail << "\n";

        for (const std::string& example : finding.examples) {
            std::cout << "              " << example << "\n";
        }

        if (finding.total > finding.examples.size()) {
            std::cout
                << "              ... and "
                << (finding.total - finding.examples.size())
                << " more\n";
        }

        if (!finding.suggestion.empty()) {
            std::cout
                << "              try: " << finding.suggestion << "\n";
        }
    }

    std::cout << "\n";

    switch (worst) {
        case nexus::Health::Ok:
            std::cout << "Nothing wrong that can be seen from here.\n";
            break;
        case nexus::Health::Warning:
            std::cout
                << "Usable, with things worth looking at.\n";
            break;
        case nexus::Health::Problem:
            std::cout
                << "Something is wrong. The findings above say what.\n";
            break;
        case nexus::Health::Unknown:
            std::cout << "Not enough information to judge.\n";
            break;
    }

    std::cout
        << "Nothing has been changed; this command only reports.\n";

    return worst == nexus::Health::Problem ? 1 : 0;
}

int commandHardware(const nexus::hardware::HardwareInfo& info) {
    std::cout << "Graphics:\n";

    if (info.graphics.empty()) {
        std::cout << "    (none detected)\n";
    }

    for (const auto& device : info.graphics) {
        std::cout
            << "    " << device.node
            << "  vendor=" << device.vendor
            << " (" << device.vendorId << ":" << device.deviceId << ")";

        if (!device.driver.empty()) {
            std::cout << "  driver=" << device.driver;
        }

        std::cout << "\n";
    }

    std::cout
        << "\nFirmware:    " << toString(info.firmware) << "\n"
        << "Secure Boot: " << toString(info.secureBoot) << "\n"
        << "Chassis:     "
        << (info.hasBattery ? "laptop" : "desktop") << "\n"
        << "Gamepad:     "
        << (info.hasGamepad ? "present" : "none detected") << "\n"
        << "Architecture:" << " " << info.architecture << "\n";

    if (!info.cpuModel.empty()) {
        std::cout << "CPU:         " << info.cpuModel << "\n";
    }

    std::cout << "\nCapabilities:\n";

    for (const std::string& capability : info.capabilities()) {
        std::cout << "    " << capability << "\n";
    }

    if (!info.unreadable.empty()) {
        std::cout << "\nCould not read:\n";

        for (const std::string& problem : info.unreadable) {
            std::cout << "    " << problem << "\n";
        }
    }

    return 0;
}

int commandProfile(
    const nexus::AliasTable& aliases,
    const std::vector<Component>& installed,
    const std::vector<Component>& universe,
    bool haveAvailable,
    const std::string& action,
    const std::string& name,
    const std::string& directory,
    const std::string& architecture
) {
    const auto loaded =
        nexus::system::parseProfileDirectory(directory);

    for (const std::string& problem : loaded.problems) {
        std::cerr << "Warning: " << problem << "\n";
    }

    if (loaded.profiles.empty()) {
        std::cerr
            << "No profiles found in " << directory << "\n"
            << "Use --profiles <dir> to point somewhere else.\n";
        return 1;
    }

    if (action == "list") {
        std::cout << "Profiles in " << directory << ":\n\n";

        for (const nexus::Profile& profile : loaded.profiles) {
            std::cout
                << "    " << profile.name << "\n"
                << "        " << profile.description << "\n"
                << "        " << profile.requirements.size()
                << " requirement(s)\n";
        }

        return 0;
    }

    nexus::Composition composition;

    if (!findComposition(loaded.profiles, name, composition)) {
        return 1;
    }

    if (composition.refused) {
        std::cerr << composition.refusal << "\n";
        return 1;
    }

    const nexus::Profile* chosen = &composition.profile;

    reportComposition(composition);

    if (action == "show") {
        std::cout
            << "Profile:      " << chosen->name << "\n"
            << "Description:  " << chosen->description << "\n"
            << "Architecture: "
            << (chosen->architecture.empty()
                    ? "(native)" : chosen->architecture)
            << "\n";

        if (!chosen->additionalArchitectures.empty()) {
            std::cout << "Also needs:   ";

            for (const std::string& extra :
                 chosen->additionalArchitectures) {

                std::cout << extra << " ";
            }

            std::cout << "\n";
        }

        std::cout << "\nRequires:\n";

        for (const nexus::Requirement& requirement :
             chosen->requirements) {

            std::cout << "    " << toString(requirement) << "\n";
        }

        if (!chosen->preferred.empty()) {
            std::cout << "\nPrefers:\n";

            for (const auto& [capability, component] :
                 chosen->preferred) {

                std::cout
                    << "    " << capability
                    << " -> " << component << "\n";
            }
        }

        return 0;
    }

    if (action != "check") {
        std::cerr
            << "Unknown profile action: " << action << "\n"
            << "Use list, show or check.\n";
        return 2;
    }

    nexus::Profile profile = *chosen;

    if (!architecture.empty()) {
        profile.architecture = architecture;
    }

    const auto report =
        nexus::checkProfile(
            profile, buildSolver(installed), aliases);

    // When archive data is loaded, a missing requirement can be
    // re-checked against everything available. That turns "you do not
    // have steam" into "steam is available and would bring N
    // components", which is the answer somebody setting up a machine
    // actually wants.
    nexus::ProfileReport possible;

    if (haveAvailable) {
        possible = nexus::checkProfile(
            profile, buildSolver(universe), aliases);
    }

    std::cout
        << "Profile:     " << report.profile << "\n"
        << "             " << report.description << "\n"
        << "Satisfied:   " << report.satisfied
        << " of " << report.items.size() << "\n";

    if (!report.appliedPreferences.empty()) {
        std::cout << "\nApplied for this machine:\n";

        for (const std::string& applied : report.appliedPreferences) {
            std::cout << "    " << applied << "\n";
        }
    }

    if (!report.inactivePreferences.empty()) {
        std::cout << "\nNot applicable here:\n";

        for (const std::string& idle : report.inactivePreferences) {
            std::cout << "    " << idle << "\n";
        }
    }

    std::cout << "\n";

    for (const nexus::ProfileItem& item : report.items) {
        if (item.satisfied) {
            std::cout
                << "  [ok]      " << item.requirement << "\n"
                << "            met by " << item.provided.front()
                << " (+" << (item.provided.size() - 1)
                << " dependencies)\n";
        } else {
            std::cout
                << "  [missing] " << item.requirement << "\n";

            bool explained = false;

            if (haveAvailable) {
                for (const nexus::ProfileItem& option : possible.items) {
                    if (option.requirement != item.requirement) {
                        continue;
                    }

                    if (option.satisfied) {
                        std::cout
                            << "            available: install "
                            << option.provided.front() << " ("
                            << option.provided.size()
                            << " components in total)\n";
                    } else {
                        std::cout
                            << "            not available either: "
                            << option.blockedOn << "\n";
                    }

                    explained = true;
                    break;
                }
            }

            if (!explained) {
                std::cout
                    << "            nothing installed provides this"
                    << "\n";
            }
        }
    }

    if (report.complete()) {
        std::cout
            << "\nThis system already satisfies the "
            << report.profile << " profile.\n";
        return 0;
    }

    std::cout
        << "\n" << report.missing
        << " requirement(s) not met. Nothing has been changed;\n"
        << "this command only reports.\n";

    return 1;
}

int commandGaps(const SourceSummary& result) {
    if (result.gaps.empty()) {
        std::cout
            << result.name
            << " reported nothing the model could not represent.\n";
    }

    for (const auto& [kind, count] : result.gaps) {
        std::cout << kind << ": " << count << "\n";
    }

    for (const std::string& note : result.notes) {
        std::cout << "\n" << note << "\n";
    }

    return 0;
}

}

int main(int argc, char** argv) {
    std::vector<std::string> arguments(argv + 1, argv + argc);

    // Kept so that a suggested re-run is the command actually typed.
    std::string invocation = argv[0] == nullptr ? "nexus" : argv[0];

    for (const std::string& argument : arguments) {
        invocation += " " + argument;
    }

    std::string statusPath = "/var/lib/dpkg/status";
    std::string prefer;
    std::string require;
    std::string arch;
    // Where profiles live, most specific first: an explicit flag, the
    // environment (which the image sets), the installed location, then
    // the source tree for development.
    std::string profileDir;

    if (const char* fromEnvironment = std::getenv("NEXUS_PROFILES")) {
        profileDir = fromEnvironment;
    } else if (std::filesystem::is_directory(
                   "/usr/share/nexus/profiles")) {
        profileDir = "/usr/share/nexus/profiles";
    } else {
        profileDir = "components/profiles";
    }
    std::string listsDir = "/var/lib/apt/lists";
    std::string sysfsRoot = "/";
    std::string statesPath = "/var/lib/apt/extended_states";
    std::string rpmCache = "/var/cache/libdnf5";
    std::string aptConfigDir = "/etc/apt/apt.conf.d";
    std::string rpmRoot;
    std::string aliasDir;

    if (const char* fromEnvironment = std::getenv("NEXUS_ALIASES")) {
        aliasDir = fromEnvironment;
    } else if (std::filesystem::is_directory(
                   "/usr/share/nexus/aliases")) {
        aliasDir = "/usr/share/nexus/aliases";
    } else {
        aliasDir = "components/aliases";
    }
    bool forceRpm = false;
    bool forceDpkg = false;
    bool withAvailable = false;
    bool withFlatpak = false;
    bool explain = false;
    bool apply = false;
    bool assumeYes = false;
    std::vector<std::string> positional;

    for (std::size_t index = 0; index < arguments.size(); ++index) {
        if (arguments[index] == "--status" && index + 1 < arguments.size()) {
            statusPath = arguments[index + 1];
            index += 1;
            continue;
        }

        if (arguments[index] == "--yes") {
            assumeYes = true;
            continue;
        }

        if (arguments[index] == "--apply") {
            apply = true;
            continue;
        }

        if (arguments[index] == "--explain") {
            explain = true;
            continue;
        }

        if (arguments[index] == "--rpm") {
            forceRpm = true;
            continue;
        }

        if (arguments[index] == "--dpkg") {
            forceDpkg = true;
            continue;
        }

        if (arguments[index] == "--rpm-cache" &&
            index + 1 < arguments.size()) {
            rpmCache = arguments[index + 1];
            index += 1;
            continue;
        }

        if (arguments[index] == "--rpm-root" &&
            index + 1 < arguments.size()) {
            rpmRoot = arguments[index + 1];
            index += 1;
            continue;
        }

        if (arguments[index] == "--with-flatpak") {
            withFlatpak = true;
            continue;
        }

        if (arguments[index] == "--with-available") {
            withAvailable = true;
            continue;
        }

        if (arguments[index] == "--states" && index + 1 < arguments.size()) {
            statesPath = arguments[index + 1];
            index += 1;
            continue;
        }

        if (arguments[index] == "--sysfs" && index + 1 < arguments.size()) {
            sysfsRoot = arguments[index + 1];
            index += 1;
            continue;
        }

        if (arguments[index] == "--lists" && index + 1 < arguments.size()) {
            listsDir = arguments[index + 1];
            withAvailable = true;
            index += 1;
            continue;
        }

        if (arguments[index] == "--profiles" &&
            index + 1 < arguments.size()) {
            profileDir = arguments[index + 1];
            index += 1;
            continue;
        }

        if (arguments[index] == "--arch" && index + 1 < arguments.size()) {
            arch = arguments[index + 1];
            index += 1;
            continue;
        }

        if (arguments[index] == "--prefer" && index + 1 < arguments.size()) {
            prefer = arguments[index + 1];
            index += 1;
            continue;
        }

        if (arguments[index] == "--require" && index + 1 < arguments.size()) {
            require = arguments[index + 1];
            index += 1;
            continue;
        }

        positional.push_back(arguments[index]);
    }

    if (positional.empty() ||
        positional[0] == "-h" ||
        positional[0] == "--help") {

        printUsage();
        return 0;
    }

    const std::string command = positional[0];

    // Both of these describe a machine other than this one, and
    // neither can be answered from the installed set alone.
    //
    // Installing means installing something not here yet. Generating
    // an image list means describing a machine that does not exist
    // yet -- and resolving that against what happens to be on this
    // one produced a "minimal" profile listing a full desktop's file
    // manager, because that is what the generating machine ran.
    //
    // Making the archive optional here only ever produces a confusing
    // failure or a quietly wrong answer.
    if (command == "install" || command == "image") {
        withAvailable = true;
    }
    const std::string argument =
        positional.size() > 1 ? positional[1] : std::string{};

    try {
        // Not every system has a package database, and not every
        // command needs one. `nexus hardware` and `nexus profile list`
        // are perfectly meaningful on a machine that has never heard
        // of dpkg -- which includes the Fedora image Nexus ships in.
        //
        // So a missing database is a fact to report, not a reason to
        // refuse to start.
        // Which ecosystem this machine uses is a fact to detect, not
        // a flag to demand. dpkg wins when its database is present
        // because a system with both is a Debian one with rpm
        // installed as a tool.
        SourceSummary result;
        bool havePackages = false;

        gUseRpmVersions = forceRpm ||
            (!forceDpkg &&
             !std::filesystem::exists(statusPath) &&
             nexus::system::RpmDatabase::available());

        const bool preferRpm =
            forceRpm ||
            (!forceDpkg &&
             !std::filesystem::exists(statusPath) &&
             nexus::system::RpmDatabase::available());

        if (preferRpm) {
            const nexus::system::RpmDatabase database(rpmRoot);
            const auto loaded = database.load();

            if (loaded.error.empty()) {
                result.name = "rpm database";
                result.components = loaded.components;
                result.recordsRead = loaded.packagesRead;
                havePackages = true;

                for (const Component& component : result.components) {
                    const auto count = component.requirements().size();

                    result.dependencyClauses += count;
                    result.representableClauses += count;
                }

                if (loaded.fileProvides > 0) {
                    result.notes.push_back(
                        std::to_string(loaded.fileProvides) +
                        " file path(s) are provided as capabilities: "
                        "rpm lets a requirement name a file."
                    );
                }

                if (loaded.booleanRequirements > 0) {
                    result.gaps["boolean-dependency"] =
                        loaded.booleanRequirements;
                }

                if (loaded.rpmlibRequirements > 0) {
                    result.notes.push_back(
                        std::to_string(loaded.rpmlibRequirements) +
                        " rpmlib requirement(s) were skipped: they "
                        "are satisfied by rpm itself, never by a "
                        "package."
                    );
                }
            }
        } else {
            try {
                const nexus::system::DpkgSource source(statusPath);
                const auto loaded = source.load();

                result.name = "dpkg status";
                result.components = loaded.components;
                result.recordsRead = loaded.stanzasRead;
                result.recordsSkipped = loaded.stanzasSkipped;
                result.dependencyClauses = loaded.dependencyClauses;
                result.representableClauses =
                    loaded.representableClauses;

                for (const auto& gap : loaded.gaps) {
                    result.gaps[toString(gap.kind)] += 1;
                }

                havePackages = true;
            } catch (const std::exception& error) {
                result = SourceSummary{};
            }
        }

        // Profiles name intent; the alias table says what this
        // ecosystem calls it. Without one, every profile is written
        // for whichever distribution its author had.
        nexus::AliasTable aliases;

        {
            const std::string file =
                aliasDir + "/" +
                (preferRpm ? "rpm.aliases" : "dpkg.aliases");

            const auto loaded =
                nexus::system::parseAliasFile(file);

            for (const std::string& problem : loaded.problems) {
                std::cerr << "Warning: " << problem << "\n";
            }

            aliases = loaded.table;
        }

        // Flatpak names are a third vocabulary, loaded alongside the
        // ecosystem's own rather than instead of it, so a capability
        // can be offered from both and both show up as options.
        if (withFlatpak) {
            const auto loaded = nexus::system::parseAliasFile(
                aliasDir + "/flatpak.aliases");

            for (const std::string& problem : loaded.problems) {
                std::cerr << "Warning: " << problem << "\n";
            }

            aliases.merge(loaded.table);
        }

        // What the distribution says must never be removed. Read
        // from its own configuration rather than encoded here, so the
        // policy stays whatever the distribution decided.
        const auto protectionRules =
            nexus::system::readProtectionRules(aptConfigDir);

        const std::string runningKernel =
            nexus::system::runningKernelRelease();

        // Commands that read the package database say so plainly when
        // there is not one, rather than failing on a file path the
        // user never mentioned.
        const auto requirePackages = [&]() {
            std::cerr
                << "No package database found.\n"
                << "Looked for dpkg at " << statusPath
                << " and for an rpm database, so \""
                << command << "\" has nothing to read.\n"
                << "Commands that work anywhere: hardware, "
                << "profile list, profile show.\n";
        };

        // Detected hardware joins the universe as a component, so a
        // profile can require "gpu-vendor:amd" and have it resolved
        // exactly like any other capability.
        const nexus::hardware::HardwareDetector detector(sysfsRoot);
        const nexus::hardware::HardwareInfo hardware = detector.detect();

        const nexus::hardware::EncryptionReport encryption =
            nexus::hardware::EncryptionDetector(sysfsRoot).detect();

        // Hardware belongs in the installed baseline too: the GPU in
        // this machine is not something you install, it is something
        // that is already here.
        std::vector<Component> installed = result.components;
        installed.push_back(nexus::hardware::asComponent(hardware));

        const std::set<std::string> protectedIds =
            nexus::system::protectedComponents(
                installed, protectionRules, runningKernel);

        std::vector<Component> universe = installed;

        // Kept separate so that image generation can resolve against
        // what exists rather than what is here.
        std::vector<Component> availableOnly;

        bool haveAvailable = false;

        if (withFlatpak) {
            const nexus::system::FlatpakSource flatpak;
            const auto offered = flatpak.load();

            if (!offered.error.empty()) {
                std::cerr
                    << "Warning: " << offered.error << "\n";
            } else {
                for (const Component& component : offered.components) {
                    universe.push_back(component);
                    availableOnly.push_back(component);
                }

                haveAvailable = true;

                std::cerr
                    << "Loaded " << offered.components.size()
                    << " Flatpak application(s).\n";
            }
        }

        if (withAvailable && preferRpm) {
            const nexus::system::RpmRepository repositories(rpmCache);
            const auto available = repositories.load();

            for (const auto& skipped : available.skipped) {
                std::cerr
                    << "Warning: skipped " << skipped.path
                    << " (" << skipped.reason << ")\n";
            }

            if (!available.components.empty()) {
                availableOnly = available.components;

                universe = nexus::system::mergeAvailable(
                    universe, available.components,
                    versionComparator());

                haveAvailable = true;

                std::cerr
                    << "Loaded " << available.components.size()
                    << " available packages from "
                    << available.repositoriesRead.size()
                    << " repository(ies).\n";
            }
        } else if (withAvailable) {
            const nexus::system::AptSource apt(listsDir);
            const auto available = apt.load();

            for (const auto& skipped : available.filesSkipped) {
                std::cerr
                    << "Warning: skipped " << skipped.path
                    << " (" << skipped.reason << ")\n";
            }

            if (!available.components.empty()) {
                availableOnly = available.components;

                universe = nexus::system::mergeAvailable(
                    universe, available.components,
                    versionComparator());

                haveAvailable = true;

                std::cerr
                    << "Loaded " << available.components.size()
                    << " available packages from "
                    << available.filesRead.size() << " index file(s).\n";
            } else {
                std::cerr
                    << "No package indexes read from " << listsDir
                    << "; continuing with installed packages only.\n";
            }
        }

        if (command == "scan") {
            if (!havePackages) {
                requirePackages();
                return 1;
            }

            return commandScan(result);
        }

        if (command == "what-provides") {
            if (!havePackages) {
                requirePackages();
                return 1;
            }

            if (argument.empty()) {
                std::cerr << "what-provides requires a capability name.\n";
                return 2;
            }

            return commandWhatProvides(result.components, argument);
        }

        if (command == "why") {
            if (!havePackages) {
                requirePackages();
                return 1;
            }

            if (argument.empty()) {
                std::cerr << "why requires a component id.\n";
                return 2;
            }

            return commandWhy(result.components, argument);
        }

        if (command == "inspect") {
            if (!havePackages) {
                requirePackages();
                return 1;
            }

            if (argument.empty()) {
                std::cerr << "inspect requires a component id.\n";
                return 2;
            }

            return commandInspect(result.components, argument);
        }

        if (command == "image") {
            if (argument.empty()) {
                std::cerr << "image requires a profile name.\n";
                return 2;
            }

            if (availableOnly.empty()) {
                std::cerr
                    << "No package archive could be read, so a "
                    << "generated list would\ndescribe this machine "
                    << "rather than the profile.\n";

                return 1;
            }

            return commandImage(
                aliases, availableOnly, argument, profileDir, arch);
        }

        if (command == "install") {
            if (argument.empty()) {
                std::cerr << "install requires a capability name.\n";
                return 2;
            }

            return commandInstall(
                aliases, universe, installed, argument, arch,
                preferRpm, apply, assumeYes, invocation);
        }

        if (command == "options") {
            if (argument.empty()) {
                std::cerr << "options requires a capability name.\n";
                return 2;
            }

            return commandOptions(
                aliases, universe, installed, argument, arch);
        }

        if (command == "plan") {
            if (!havePackages) {
                requirePackages();
                return 1;
            }

            if (argument.empty()) {
                std::cerr << "plan requires a capability name.\n";
                return 2;
            }

            return commandPlan(universe, argument, arch, explain);
        }

        if (command == "remove") {
            if (!havePackages) {
                requirePackages();
                return 1;
            }

            if (argument.empty()) {
                std::cerr << "remove requires a component name.\n";
                return 2;
            }

            return commandRemove(
                installed, argument, statesPath, arch, protectedIds,
                apply, assumeYes, preferRpm, invocation);
        }

        if (command == "history") {
            return commandHistory(
                nexus::system::defaultTransactionLog());
        }

        if (command == "doctor") {
            std::set<std::string> roots;

            const auto automatic =
                nexus::system::readAutoInstalled(statesPath);

            for (const Component& component : installed) {
                const std::string key =
                    component.name() + ":" + component.architecture();

                if (automatic.count(key) == 0) {
                    roots.insert(component.id());
                }
            }

            if (automatic.empty()) {
                roots.clear();
            }

            return commandDoctor(
                result, installed, hardware, encryption, roots,
                havePackages, protectedIds);
        }

        if (command == "hardware") {
            return commandHardware(hardware);
        }


        if (command == "profile") {
            const std::string action =
                positional.size() > 1 ? positional[1] : "list";
            const std::string name =
                positional.size() > 2 ? positional[2] : std::string{};

            if (action == "check" && !havePackages) {
                requirePackages();
                return 1;
            }

            return commandProfile(
                aliases, installed, universe, haveAvailable,
                action, name, profileDir, arch);
        }

        if (command == "solve") {
            if (!havePackages) {
                requirePackages();
                return 1;
            }

            if (argument.empty()) {
                std::cerr << "solve requires a capability name.\n";
                return 2;
            }

            return commandSolve(
                aliases, universe, argument, prefer, require, arch,
                explain);
        }

        if (command == "conflicts") {
            if (!havePackages) {
                requirePackages();
                return 1;
            }

            return commandConflicts(result.components);
        }

        if (command == "gaps") {
            return commandGaps(result);
        }

        std::cerr << "Unknown command: " << command << "\n\n";
        printUsage();
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n";
        return 1;
    }
}
