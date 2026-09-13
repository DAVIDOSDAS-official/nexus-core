#include <algorithm>
#include <exception>
#include <iostream>
#include <istream>
#include <cstdio>
#include <cstdlib>
#include <optional>
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
#include <nexus/system/container.hpp>
#include <nexus/system/process.hpp>
#include <nexus/system/services.hpp>
#include <nexus/system/nix_source.hpp>
#include <nexus/system/snap_source.hpp>
#include <nexus/system/portage_source.hpp>
#include <nexus/system/vpn.hpp>
#include <nexus/system/source_build.hpp>
#include <nexus/system/alias_file.hpp>
#include <nexus/removal.hpp>
#include <nexus/transaction.hpp>
#include <nexus/system/auto_installed.hpp>
#include <nexus/solver.hpp>
#include <nexus/hardware/encryption.hpp>
#include <nexus/hardware/secure_boot.hpp>
#include <nexus/hardware/hardware.hpp>
#include <nexus/system/apt_source.hpp>
#include <nexus/system/flatpak_source.hpp>
#include <nexus/system/pacman_source.hpp>
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
        << "    nexus guide            how to work this system\n"
        << "    nexus scan\n"
        << "    nexus what-provides <capability>\n"
        << "    nexus why <component>\n"
        << "    nexus inspect <component>\n"
        << "    nexus gaps [kind]\n"
        << "    nexus conflicts\n"
        << "    nexus options <capability>\n"
        << "    nexus install <capability> [--apply] [--yes]\n"
        << "    nexus source <package>       what building it would cost\n"
        << "    nexus container <package> [--from-distro d] [--apply]\n"
        << "    nexus image <profile>\n"
        << "    nexus plan <capability>\n"
        << "    nexus remove <component> [--apply] [--yes]\n"
        << "    nexus doctor\n"
        << "    nexus history\n"
        << "    nexus largest [count] [--unused]\n"
        << "    nexus vpn keys | vpn config --address A --peer-key K --endpoint H:P\n"
        << "    nexus services [--all]\n"
        << "    nexus hardware\n"
        << "    nexus secureboot\n"
        << "    nexus setup [<profile>[,<profile>...]] [--apply]\n"
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
        << "    --with-snap       also read installed snaps\n"
        << "    --with-nix        also read the Nix profile\n"
        << "    --with-portage <dir>  also read a Gentoo md5-cache tree\n"
        << "    --with-arch <db>  also offer Arch packages, via a container\n"
        << "    --from <source>   only options from base, flatpak, container or nix\n"
        << "    --rpm | --dpkg    force a package ecosystem\n"
        << "    --rpm-root <dir>  inspect an rpm root elsewhere\n"
        << "    --commands        print the commands instead of running them\n"
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

// A detector that knows how each source orders its versions.
nexus::ConflictDetector buildDetector() {
    nexus::ConflictDetector detector(versionComparator());

    // Arch packages, reachable through a container, order versions
    // pacman's way regardless of what this machine runs.
    detector.setComparatorFor(
        nexus::Source::Container,
        [](const std::string& provided, const std::string& required) {
            return nexus::system::comparePacmanConstraint(
                provided, required);
        });

    return detector;
}

nexus::Solver buildSolver(const std::vector<Component>& components) {
    return nexus::Solver(components, buildDetector());
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

            // Somebody typing a name from memory gets it close.
            // "cybersecurity" for "security" is a better thing to
            // answer than a list of eleven names.
            std::vector<std::string> near;

            for (const nexus::Profile& profile : available) {
                const bool contains =
                    profile.name.find(one) != std::string::npos ||
                    one.find(profile.name) != std::string::npos;

                if (contains) {
                    near.push_back(profile.name);
                }
            }

            if (!near.empty()) {
                std::cerr << "Did you mean:";

                for (const std::string& name : near) {
                    std::cerr << " " << name;
                }

                std::cerr << "?\n";
            } else {
                std::cerr << "Known profiles:";

                for (const nexus::Profile& profile : available) {
                    std::cerr << " " << profile.name;
                }

                std::cerr << "\n";
            }

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

    // base is composed into every image.
    //
    // It is defined as what any usable system needs regardless of
    // what it is for, so an image without it is not a machine. Asking
    // each caller to remember it is the kind of thing that gets
    // forgotten -- and was, for every image built until a profile
    // asked for something the base image did not already provide.
    const std::string requested =
        (profileName == "base" ||
         profileName.find("base") != std::string::npos)
            ? profileName
            : "base," + profileName;

    nexus::Composition composition;

    if (!findComposition(loaded.profiles, requested, composition)) {
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
    bool showCommands,
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

    if (showCommands) {
        // The commands, not the outcome.
        //
        // The appeal of assembling a system by hand is not the
        // typing, it is knowing what happened. A tool that resolves
        // and explains and then hands over the exact commands gives
        // that without hiding anything -- and without pretending the
        // base is something it is not.
        std::cout
            << "\nWhat this would run:\n\n"
            << "    sudo " << (useRpm ? "dnf" : "apt-get")
            << " install "
            << (useRpm
                    ? "--setopt=install_weak_deps=False "
                    : "--no-install-recommends ")
            << requested << "\n"
            << "\nRun it yourself, or use --apply to have Nexus do "
            << "it.\n";

        return 0;
    }

    if (!apply) {
        std::cout
            << "\nNothing has been changed. Use --apply to install, "
            << "or\n--commands to see what it would run.\n";

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

    // Written before the work starts. An interrupted process does
    // not get to run its cleanup, so a record that only exists
    // afterwards is a record that a power cut erases -- along with
    // any sign that the machine was changed.
    nexus::system::TransactionRecord record;

    record.when = nexus::system::currentTimestamp();
    record.request = capability;
    record.resolved = requested;
    record.packages = expected;

    const std::string log = nexus::system::defaultTransactionLog();

    const std::string marker =
        nexus::system::beginTransaction(log, record);

    // Whether a change made here will still be here after a reboot.
    //
    // An image-based system mounts /usr read-only and is updated by
    // replacing the whole image. bootc usr-overlay makes it writable
    // until the next boot, which is meant for debugging -- so an
    // install can succeed, report success, and silently undo itself.
    //
    // That is worse than refusing. Everything else in this tool is
    // built on saying what actually happened.
    bool useRpmOstree = false;

    if (nexus::system::commandExists("bootc")) {
        const auto mounts = nexus::system::runCommand(
            "findmnt -no OPTIONS /usr 2>/dev/null", false);

        const bool writable =
            mounts.text.find("rw,") != std::string::npos ||
            mounts.text.rfind("rw", 0) == 0;

        if (!writable) {
            std::cerr
                << "\nThis is an image-based system, so this will "
                << "be layered into a new\ndeployment rather than "
                << "installed into the running one.\n\n"
                << "Nothing changes until the machine reboots. The "
                << "current deployment\nstays bootable, so if the "
                << "new one is worse you can go back to it.\n";

            useRpmOstree = true;
        }

        const auto overlay = nexus::system::runCommand(
            "findmnt -no SOURCE /usr 2>/dev/null", false);

        if (overlay.text.find("overlay") != std::string::npos) {
            std::cout
                << "\nWarning: /usr is writable only because "
                << "development mode is on.\n"
                << "Anything installed now disappears at the next "
                << "reboot.\n";

            if (!assumeYes) {
                std::cout << "\nContinue anyway? [y/N] ";

                std::string answer;
                std::getline(std::cin, answer);

                if (answer != "y" && answer != "Y") {
                    std::cout << "\nNothing has been changed.\n";
                    return 0;
                }
            }
        }
    }

    std::cout << "\nHanding the plan to " << manager << ".\n\n";

    // Three appliers, one shape: verify, hand over, record what
    // happened. An image-based system layers into a new deployment
    // rather than changing the running one, so what it records is
    // "staged" -- true until the machine reboots, and false to call
    // it anything else.
    const auto applied =
        useRpmOstree
            ? nexus::system::layerWithRpmOstree(requested)
            : useRpm ? nexus::system::applyWithDnf(requested)
                     : nexus::system::applyWithApt(requested);

    for (const std::string& line : applied.output) {
        std::cout << line << "\n";
    }

    // A tool that changes a system owes an account of what it did.
    record.outcome = toString(applied.outcome);
    record.exitCode = applied.exitCode;
    record.succeeded =
        applied.outcome == nexus::system::ApplyOutcome::Applied;

    if (!nexus::system::finishTransaction(log, marker, record)) {
        std::cerr
            << "\nWarning: could not write the transaction record to "
            << log << ".\n";
    }

    switch (applied.outcome) {
        case nexus::system::ApplyOutcome::Applied:
            std::cout << "\nDone.\n";
            return 0;

        case nexus::system::ApplyOutcome::Staged:
            // Not done. Written, and waiting for a reboot -- saying
            // "done" here would be false for as long as the machine
            // stays up, which on a laptop is most of the time.
            std::cout
                << "\nStaged. None of it is in effect yet.\n\n"
                << "    systemctl reboot         boot into it\n"
                << "    rpm-ostree status        what is waiting\n"
                << "    rpm-ostree rollback      after rebooting, if "
                << "it is worse\n\n"
                << "The deployment you are running now stays "
                << "bootable.\n";
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
    const std::string& architecture,
    const std::optional<nexus::Source>& only
) {
    const nexus::ConflictDetector detector(versionComparator());

    const auto report = nexus::findOptions(
        capability, universe, installed,
        buildSolver(universe), detector, architecture, aliases, only);

    std::cout << "Capability:  " << capability << "\n";

    if (report.options.empty()) {
        std::cout << "\nNothing available provides it";

        if (only.has_value()) {
            std::cout << " from " << toString(*only);
        }

        std::cout
            << ".\nTry --with-available or --with-flatpak to widen "
            << "the search.\n";

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
                << " of them new";

            // Counts and bytes are different questions, and one
            // Flatpak is not cheaper than forty packages just
            // because it is one thing. Both are stated; neither is
            // folded into the other.
            if (option.sizeKnown) {
                std::cout << " (";

                if (option.downloadBytes > 0) {
                    std::cout
                        << nexus::system::formatSize(
                               option.downloadBytes)
                        << " to fetch";
                }

                if (option.downloadBytes > 0 &&
                    option.installBytes > 0) {
                    std::cout << ", ";
                }

                if (option.installBytes > 0) {
                    std::cout
                        << nexus::system::formatSize(
                               option.installBytes)
                        << " on disk";
                }

                std::cout << ")";
            }

            std::cout << "\n";
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
        buildDetector()
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
    bool showCommands,
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
        buildDetector(),
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

    if (showCommands) {
        std::cout << "\nWhat this would run:\n\n    sudo "
                  << (useRpm ? "dnf" : "apt-get") << " remove";

        for (const std::string& name : plan.removed) {
            std::cout << " " << name;
        }

        std::cout
            << "\n\nRun it yourself, or use --apply to have Nexus "
            << "do it.\n";

        return 0;
    }

    if (!apply) {
        std::cout
            << "\nNothing has been changed. Use --apply to remove, "
            << "or\n--commands to see what it would run.\n";

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

    nexus::system::TransactionRecord record;

    record.kind = nexus::system::TransactionKind::Remove;
    record.when = nexus::system::currentTimestamp();
    record.request = "remove " + target;
    record.resolved = plan.target;
    record.packages = std::set<std::string>(
        plan.removed.begin(), plan.removed.end());

    const std::string log = nexus::system::defaultTransactionLog();

    const std::string marker =
        nexus::system::beginTransaction(log, record);

    std::cout
        << "\nHanding the plan to " << (useRpm ? "dnf" : "apt")
        << ".\n\n";

    const auto removed = useRpm
        ? nexus::system::removeWithDnf(plan.removed)
        : nexus::system::removeWithApt(plan.removed);

    for (const std::string& line : removed.output) {
        std::cout << line << "\n";
    }

    record.outcome = toString(removed.outcome);
    record.exitCode = removed.exitCode;
    record.succeeded =
        removed.outcome == nexus::system::ApplyOutcome::Applied;

    if (!nexus::system::finishTransaction(log, marker, record)) {
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
    // Every log, not the one belonging to whoever asked. Changes
    // are made under sudo and read back without it, so a machine's
    // history lives in two files and belongs to neither.
    const auto records = nexus::system::readAllTransactions();

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

        std::cout
            << "  ["
            << (record.unfinished ? "interrupted" : record.outcome)
            << "]\n";

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
            buildDetector(),
            protectedIds);

        for (const nexus::Finding& finding : detail.findings) {
            findings.push_back(finding);
        }
    }

    // Whether this machine can receive updates at all.
    //
    // An image-based system updates by pulling a newer image from
    // where it was installed from. Installed from a local build,
    // there is nowhere to pull from and no fix can ever arrive --
    // which nothing else reports, because everything works.
    if (nexus::system::commandExists("bootc")) {
        // --format=json is not understood by every version, and a
        // check that gives up without saying why is a check that
        // reports a healthy machine it never examined.
        auto status = nexus::system::runCommand(
            "bootc status --format=yaml 2>/dev/null", false);

        if (!status.ok || status.text.empty()) {
            status = nexus::system::runCommand(
                "bootc status 2>/dev/null", false);
        }

        nexus::Finding updates;

        updates.check = "Updates";

        if (!status.ok || status.text.empty()) {
            updates.health = nexus::Health::Unknown;
            updates.detail =
                "bootc is present but did not report a status.";
        } else if (status.text.find("localhost/") !=
                   std::string::npos) {
            updates.health = nexus::Health::Warning;
            updates.detail =
                "Installed from a local image, so there is nowhere "
                "to update from.";
            updates.examples.push_back(
                "no fix can reach this machine, however urgent");
            updates.total = 1;
        } else {
            updates.health = nexus::Health::Ok;
            updates.detail =
                "Image-based; bootc upgrade will pull a newer one.";
        }

        findings.push_back(std::move(updates));
    }

    // A change that started and never finished. The machine may have
    // been left part-way through one, and nothing else will say so.
    {
        const auto unfinished = nexus::system::unfinishedTransactions(
            nexus::system::defaultTransactionLog());

        if (!unfinished.empty()) {
            nexus::Finding interrupted;

            interrupted.check = "Changes";
            interrupted.health = nexus::Health::Warning;
            interrupted.total = unfinished.size();
            interrupted.detail =
                std::to_string(unfinished.size()) +
                " change(s) started and never finished.";

            for (const auto& record : unfinished) {
                if (interrupted.examples.size() >= 5) {
                    break;
                }

                interrupted.examples.push_back(
                    record.when + "  " + record.request);
            }

            interrupted.suggestion = "nexus history";

            findings.push_back(std::move(interrupted));
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

// What this machine runs.
//
// "Enabled" is the question people mean: not what is installed, and
// not what happens to be running this minute, but what will start the
// next time the machine boots.
// What is taking up the room.
//
// A system's size is not evenly spread: on a Fedora base a handful of
// components are most of it, and knowing which ones is the difference
// between "the image is 2.9 GB" and "the image is 2.9 GB because
// firmware for every device on earth is 1.2 GB of it".
//
// Sizes come from the same metadata everything else does, so this is
// a report rather than a measurement -- it says what the packages
// claim, which can differ from what is on disk.
// How to work this system, by task rather than by command.
//
// The question somebody has is "how do I update this", not "what
// commands exist". Every distribution answers it differently and the
// answer is usually somewhere else -- a wiki, a forum, a video. It
// belongs in the tool.
//
// What Nexus does not do is listed too. A guide that only says what
// works leaves somebody searching for the rest.
// What building a package from source would cost.
//
// The benefit is specific -- your own flags, a patch, a version the
// archive does not have -- and it is not free. Nexus can say what the
// build requires, because Build-Depends is written in the same
// grammar as everything else and the resolver already costs
// dependency sets.
//
// What it does not say is how long the compile takes. That depends on
// the package, the machine and the flags, and an invented number
// would be worse than none.
// A WireGuard tunnel.
//
// This is the first thing Nexus creates rather than reads, and what
// it creates is a secret. So: the key is generated by wg rather than
// here, it is never printed, the file is created private rather than
// made private afterwards, and a file that cannot be made private is
// deleted rather than left.
//
// The parts Nexus cannot know are required rather than defaulted. A
// config with placeholders looks finished and does not work, and the
// failure arrives later looking like something else.
int commandVpn(
    const std::string& action,
    const nexus::system::TunnelSettings& settings,
    const std::string& path,
    bool overwrite,
    bool apply
) {
    if (!nexus::system::wireguardAvailable()) {
        std::cerr
            << "wg is not installed.\n"
            << "    nexus install vpn-client\n";

        return 1;
    }

    if (action == "keys") {
        const auto pair = nexus::system::generateKeyPair();

        if (!pair.ok()) {
            std::cerr << pair.error << "\n";
            return 1;
        }

        // Only the public half. The private one belongs in a file
        // with the right permissions, not in a terminal that keeps
        // scrollback and a shell that keeps history.
        std::cout
            << "Public key:  " << pair.publicKey << "\n"
            << "\nGive that to whoever runs the server.\n"
            << "The private key is not printed. Use 'nexus vpn "
            << "config' to write a\ntunnel, which generates and "
            << "stores one properly.\n";

        return 0;
    }

    if (action != "config") {
        std::cerr
            << "Unknown vpn action: " << action << "\n"
            << "    nexus vpn keys\n"
            << "    nexus vpn config --address A --peer-key K "
            << "--endpoint H:P\n";

        return 2;
    }

    const auto missing = settings.missing();

    if (!missing.empty()) {
        std::cerr
            << "A tunnel needs things Nexus cannot work out:\n\n";

        for (const std::string& item : missing) {
            std::cerr << "    " << item << "\n";
        }

        std::cerr
            << "\nThese come from whoever runs the server. Nexus "
            << "does not guess\nthem: a config with invented values "
            << "looks finished and does not\nwork.\n";

        return 1;
    }

    const auto pair = nexus::system::generateKeyPair();

    if (!pair.ok()) {
        std::cerr << pair.error << "\n";
        return 1;
    }

    const std::string contents =
        nexus::system::renderTunnel(settings, pair.privateKey);

    std::cout
        << "Interface:   " << settings.interfaceName << "\n"
        << "Address:     " << settings.address << "\n"
        << "Endpoint:    " << settings.endpoint << "\n"
        << "Routes:      " << settings.allowedIps << "\n"
        << "File:        " << path << "\n"
        << "\nYour public key: " << pair.publicKey << "\n"
        << "The server needs this. Your private key goes in the "
        << "file and\nnowhere else.\n";

    if (settings.allowedIps.find("0.0.0.0/0") != std::string::npos) {
        std::cout
            << "\nThis routes everything through the server, so the "
            << "server sees\nall of your traffic and you appear to "
            << "be wherever it is.\n";
    }

    if (!apply) {
        std::cout
            << "\nNothing has been written. Use --apply to write "
            << "it.\n";

        return 0;
    }

    const auto written =
        nexus::system::writeTunnel(path, contents, overwrite);

    if (!written.ok) {
        std::cerr << "\n" << written.error << "\n";
        return 1;
    }

    std::cout
        << "\nWritten to " << path << ", readable only by you.\n"
        << "\nBring it up with:\n"
        << "    sudo wg-quick up " << settings.interfaceName << "\n";

    return 0;
}

int commandSource(
    const std::vector<Component>& universe,
    const std::vector<Component>& installed,
    const std::string& package,
    const std::string& architecture,
    bool showCommands
) {
    const auto info = nexus::system::readSourceBuild(package);

    if (!info.error.empty()) {
        std::cerr << info.error << "\n";

        if (!info.sourcesAvailable) {
            // apt says what is missing without saying what to do
            // about it, which is most of why people give up here.
            std::cerr
                << "\nDebian and Ubuntu ship source separately, and "
                << "it is off by default.\nEnable it with:\n\n"
                << "    sudo sed -i 's/^Types: deb$/Types: deb "
                << "deb-src/' \\\n"
                << "        /etc/apt/sources.list.d/ubuntu.sources\n"
                << "    sudo apt update\n\n"
                << "On older releases the same thing is a commented "
                << "deb-src line in\n/etc/apt/sources.list.\n";
        }

        return 1;
    }

    std::cout
        << "Package:     " << info.package << "\n"
        << "Source:      " << info.sourcePackage;

    if (!info.version.empty()) {
        std::cout << "  " << info.version;
    }

    std::cout
        << "\nBuild needs: " << info.buildDependencies.size()
        << " requirement(s)\n";

    // Cost them the way anything else is costed.
    std::set<std::string> here;

    for (const Component& component : installed) {
        here.insert(component.name() + ":" + component.architecture());
    }

    nexus::SolverRequest request;

    request.architecture = architecture;
    request.scope = nexus::Source::Base;

    for (const auto& requirement : info.buildDependencies) {
        request.requirements.push_back(requirement);
    }

    const auto solution = buildSolver(universe).solve(request);

    if (solution.status != nexus::SolverStatus::Success) {
        std::cout
            << "\nThe build dependencies cannot be resolved.\n";

        if (!solution.blockedOn.empty()) {
            std::cout << "Blocked on:\n    "
                      << solution.blockedOn << "\n";
        }

        return 1;
    }

    std::vector<std::string> toInstall;
    std::uint64_t download = 0;
    std::uint64_t disk = 0;

    for (const std::string& id : solution.selected) {
        for (const Component& component : universe) {
            if (component.id() != id) {
                continue;
            }

            if (here.count(component.name() + ":" +
                           component.architecture()) == 0) {
                toInstall.push_back(component.name());
                download += component.downloadSize();
                disk += component.installedSize();
            }

            break;
        }
    }

    std::cout
        << "Would add:   " << toInstall.size() << " component(s)";

    if (download > 0 || disk > 0) {
        std::cout << " (";

        if (download > 0) {
            std::cout
                << nexus::system::formatSize(download) << " to fetch";
        }

        if (download > 0 && disk > 0) {
            std::cout << ", ";
        }

        if (disk > 0) {
            std::cout
                << nexus::system::formatSize(disk) << " on disk";
        }

        std::cout << ")";
    }

    std::cout << "\n";

    if (!toInstall.empty()) {
        std::cout << "\nBuild tools it would install:\n";

        std::size_t shown = 0;

        for (const std::string& name : toInstall) {
            std::cout << "    " << name << "\n";

            if (++shown >= 12) {
                std::cout
                    << "    ... and " << (toInstall.size() - shown)
                    << " more\n";
                break;
            }
        }
    }

    std::cout
        << "\nHow long the compile takes is not predicted: it "
        << "depends on the\npackage, the machine and the flags.\n";

    if (showCommands || true) {
        std::cout
            << "\nWhat to run:\n\n"
            << "    sudo apt-get build-dep " << package << "\n"
            << "    apt-get source " << package << "\n"
            << "    cd " << info.sourcePackage << "-*\n"
            << "    dpkg-buildpackage -b -uc -us\n"
            << "\nYour flags go in DEB_CFLAGS_APPEND, or edit "
            << "debian/rules.\n"
            << "Nexus does not run these: a build is yours to watch.\n";
    }

    return 0;
}

int commandGuide(bool useRpm) {
    const std::string manager = useRpm ? "dnf" : "apt";

    std::cout << R"(Nexus - how to work this system

Nexus decides and explains; )" << manager << R"( does the work.
Every command reads by default. Only --apply changes anything.

WHAT IS ON THIS MACHINE

  nexus doctor                  is anything wrong?
  nexus scan                    what is installed, in summary
  nexus hardware                what this machine is
  nexus services                what starts when it boots
  nexus largest 20              what is taking up the room
  nexus largest 20 --unused     what is big and needed by nothing
  nexus history                 what Nexus has changed

INSTALLING AND REMOVING

  nexus options web-browser     every way to get one, with costs
  nexus install firefox         what it would do, and nothing more
  nexus install firefox --commands
                                the exact commands, to run yourself
  sudo nexus install firefox --apply
  nexus remove firefox          what removing it would take with it
  sudo nexus remove firefox --apply

  nexus source nmap             what building it from source costs

  --commands works on install, remove and setup. Nexus works out
  what to do and hands you the commands; nothing is hidden and
  nothing is run. The appeal of assembling a system by hand is
  knowing what happened, and that does not require doing the
  resolving by hand as well.

  Nexus checks the plan with )" << manager << R"( before applying it,
  and refuses to apply one )" << manager << R"( will not agree to.

UPDATING

  Nexus does not update the system. Use )" << manager << R"( directly:
)";

    if (useRpm) {
        std::cout << R"(
      sudo bootc upgrade               pull a newer image
      sudo reboot                      and boot into it

  An image-based system updates by replacing the whole image rather
  than by changing packages one at a time, so the change arrives at
  a reboot and the previous one stays bootable if it does not work.

      sudo bootc rollback              go back to the previous one

  If bootc reports nothing to pull, this machine was installed from
  an image that was never published, and cannot update. That is a
  property of how it was built, not a fault.

      sudo dnf upgrade                 packages layered on top
)";
    } else {
        std::cout << R"(
      sudo apt update                  refresh what is available
      sudo apt upgrade                 update everything
)";
    }

    std::cout << R"(
  Updating is one operation with one correct implementation, and it
  already exists. A second one would only be a way to get it wrong.

SETTING UP A MACHINE

  nexus setup                   what this machine could be
  nexus setup gaming            what that would take
  sudo nexus setup gaming --apply

  Several at once: nexus setup school,gaming

BUILDING FROM SOURCE

  nexus source <package>        the build dependencies, and what
                                they would cost here

  Nexus prints the commands and does not run them: a build is yours
  to watch, and how long it takes depends on the package, the
  machine and your flags.

  On Debian and Ubuntu this needs source packages enabled, which
  they are not by default. nexus source says so and gives the line
  to fix it.

SOFTWARE FROM ELSEWHERE

  nexus options X --with-flatpak     also offer Flatpaks
  nexus options X --with-snap        also read installed snaps
  nexus container metasploit --apply run another distribution's
                                     package, in a container

  Packages from different distributions cannot share one filesystem.
  They can run beside each other in containers, and Nexus says what
  that costs before you agree to it.

WHEN SOMETHING IS WRONG

  nexus doctor                  start here
  nexus why <component>         what pulled this in
  nexus conflicts               what collides with what
  nexus solve X --explain       why every component was chosen
  nexus services                anything failing or restarting

WHAT NEXUS DOES NOT DO

  It does not update, and it does not unpack, configure or remove
  files. )" << manager << R"( does those, correctly, and a second
  implementation would be a second set of bugs.

  It does not mix distributions in one filesystem. Nothing can.

  It does not know about services beyond what systemd reports, or
  about anything installed by a script rather than a package. Those
  show up in nexus services with no owner, which is worth looking at.

)";

    return 0;
}

int commandLargest(
    const std::vector<Component>& installed,
    std::size_t howMany,
    bool onlyUnused,
    const std::set<std::string>& unused
) {
    std::vector<const Component*> ordered;

    std::uint64_t total = 0;
    std::size_t unknown = 0;

    for (const Component& component : installed) {
        // Whether to filter is a flag, not something inferred from
        // the set being empty. An empty set means both "no filter
        // asked for" and "nothing is unused", and treating the second
        // as the first showed every component on a system that had no
        // unused ones at all.
        if (onlyUnused && unused.count(component.id()) == 0) {
            continue;
        }

        if (component.installedSize() == 0) {
            // Unknown is not zero, and counting it as zero would
            // understate the total by however much it actually is.
            unknown += 1;
            continue;
        }

        total += component.installedSize();
        ordered.push_back(&component);
    }

    std::sort(
        ordered.begin(),
        ordered.end(),
        [](const Component* left, const Component* right) {
            return left->installedSize() > right->installedSize();
        }
    );

    std::cout
        << "Components:  " << ordered.size() << "\n"
        << "Accounted:   "
        << nexus::system::formatSize(total) << "\n";

    if (onlyUnused) {
        // The two halves together are the question somebody actually
        // has about an image: not what is big, and not what is
        // unneeded, but what is both.
        std::cout
            << "Showing only what nothing needs.\n";

        if (ordered.empty()) {
            std::cout
                << "\nNothing is both installed and unneeded.\n";
        }
    }

    if (unknown > 0) {
        std::cout
            << "Unknown:     " << unknown
            << " component(s) report no size\n";
    }

    std::cout << "\n";

    std::uint64_t shown = 0;
    std::size_t count = 0;

    for (const Component* component : ordered) {
        if (count >= howMany) {
            break;
        }

        const double share =
            total == 0
                ? 0.0
                : 100.0 * static_cast<double>(
                      component->installedSize()) /
                  static_cast<double>(total);

        std::string size = nexus::system::formatSize(
            component->installedSize());

        while (size.size() < 8) {
            size = " " + size;
        }

        std::printf(
            "  %s  %5.1f%%  %s\n",
            size.c_str(),
            share,
            component->id().c_str());

        shown += component->installedSize();
        count += 1;
    }

    if (total > 0) {
        std::printf(
            "\nThose %zu are %.0f%% of the total.\n",
            count,
            100.0 * static_cast<double>(shown) /
                static_cast<double>(total));
    }

    return 0;
}

int commandServices(bool showAll, bool useRpm) {
    auto result = nexus::system::readServices();

    if (!result.error.empty()) {
        std::cerr << result.error << "\n";
        return 1;
    }

    // Which component installed each one. This is the part that makes
    // it a question about the system rather than about systemd.
    nexus::system::attachOwners(result, useRpm);

    std::cout
        << "Services:    " << result.services.size() << " unit(s)\n"
        << "Starts at boot: " << result.enabled << "\n";

    if (result.systemdRunning) {
        std::cout << "Failed:      " << result.failedCount << "\n";

        if (result.restartingCount > 0) {
            std::cout
                << "Restarting:  " << result.restartingCount
                << " (stuck starting, never reaches failed)\n";
        }
    } else {
        // Unit files can be read without a running systemd; whether
        // anything is running now cannot.
        std::cout
            << "Running:     unknown (systemd is not running here)\n";
    }

    std::cout << "\n";

    std::size_t shown = 0;

    for (const auto& service : result.services) {
        if (!showAll && !service.startsAtBoot() && !service.failed &&
            !service.restarting) {
            continue;
        }

        std::cout << "  ";

        if (service.failed) {
            std::cout << "[failed] ";
        } else if (service.restarting) {
            std::cout << "[loop]   ";
        } else if (service.startsAtBoot()) {
            std::cout << "[boot]   ";
        } else {
            std::cout << "         ";
        }

        std::cout << service.name;

        if (!service.startsAtBoot() && !service.failed &&
            !service.restarting) {
            std::cout << "  (" << toString(service.state) << ")";
        }

        if (!service.owner.empty()) {
            std::cout << "  from " << service.owner;
        }

        std::cout << "\n";

        shown += 1;
    }

    if (shown == 0) {
        std::cout << "  nothing starts at boot\n";
    }

    if (!showAll) {
        std::cout
            << "\nShowing what starts at boot. --all for every "
            << "unit.\n";
    }

    return 0;
}

// What Secure Boot is doing, and what could be done about it.
//
// The usual advice for a custom distribution is to turn it off. That
// is the wrong answer given to everybody because the right answer is
// unfamiliar -- and it is given by projects whose own security
// features it undermines.
int commandSecureBoot(const std::string& root) {
    const auto report =
        nexus::hardware::SecureBootDetector(root).detect();

    std::cout << "Secure Boot: " << toString(report.state) << "\n";

    if (report.ownerKeysEnrolled) {
        std::cout << "Owner keys:  enrolled\n";
    }

    for (const std::string& problem : report.unreadable) {
        std::cout << "             (" << problem << ")\n";
    }

    std::cout << "\n";

    switch (report.state) {
        case nexus::hardware::SecureBootState::NotSupported:
            std::cout
                << "This machine does not use Secure Boot. Nothing "
                << "to do.\n";
            return 0;

        case nexus::hardware::SecureBootState::Disabled:
            std::cout
                << "The firmware will boot anything, signed or not.\n"
                << "\nThat is how most custom systems are run, and "
                << "it means nothing\nchecks the kernel before it "
                << "starts.\n";
            break;

        case nexus::hardware::SecureBootState::Enabled:
            std::cout
                << "The firmware will only boot a kernel signed by a "
                << "key it trusts.\n";

            if (!report.ownerKeysEnrolled) {
                std::cout
                    << "\nOnly the manufacturer's keys are enrolled, "
                    << "so a kernel Nexus\nbuilt would not boot as "
                    << "things stand.\n";
            } else {
                std::cout
                    << "\nA key of your own is enrolled, so a kernel "
                    << "signed with it\nwould boot.\n";
            }

            break;

        case nexus::hardware::SecureBootState::SetupMode:
            std::cout
                << "The firmware is in setup mode and will accept new "
                << "keys.\n";
            break;

        case nexus::hardware::SecureBootState::Unknown:
            std::cout
                << "The state could not be read.\n";
            break;
    }

    std::cout << R"(
There are three ways a custom system can boot with Secure Boot on,
and only one of them is available to a project without a company
behind it.

  A shim signed by Microsoft
      What every distribution you have heard of ships. Getting one
      requires review by the shim review board: a real process,
      months long, and they want a track record first.

  Your own key, enrolled by you
      You sign the kernel; the machine's owner enrols the key once,
      through the firmware, and it is trusted from then on.

      This is not exotic. It is what happens on Ubuntu when a
      driver is built for your kernel -- millions of people have
      done it without knowing what it was called.

      It needs: sbsigntool to sign, mokutil to enrol, one reboot,
      and a password typed into a blue screen the firmware shows.

  Turning Secure Boot off
      What most custom systems tell you to do. It works, and it
      means nothing checks the kernel before it starts. Reasonable
      on a machine you are experimenting with; a poor default for
      anybody else.

Nexus does not sign anything itself, and does not need to: its
images are built on Fedora's, and the kernel in them is Fedora's,
carrying Fedora's signature through the standard shim. Adding
packages on top does not touch it.

So an image built this way boots with Secure Boot on, unchanged.
That was checked the only way it can be -- by installing one on a
machine with Secure Boot enforcing and watching it start.

The three options above matter again the moment a kernel is built
rather than inherited.
)";

    return 0;
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

// The first thing somebody sees.
//
// Everything under this already exists -- profiles, composition,
// hardware detection, verified installs. What was missing was
// anything that presents them, and a system nobody can choose is a
// system nobody uses.
//
// It states what each profile would cost on this machine before
// asking, rather than after. Choosing between names alone is not
// choosing.
// Install a package from another distribution, through a container.
//
// The container is distrobox's: it creates it, wires the home
// directory and the display, and exports the binary onto the host
// PATH. Nexus decides which distribution, which container, and which
// package -- and says what it will cost before doing any of it.
int commandContainerInstall(
    const std::vector<Component>& universe,
    const std::string& requested,
    const std::string& distribution,
    bool apply,
    bool assumeYes
) {
    if (!nexus::system::Containers::available()) {
        std::cerr
            << "distrobox is not installed.\n"
            << "It creates and wires the container; Nexus only "
            << "decides what goes in it.\n";

        return 1;
    }

    const std::string image =
        nexus::system::containerImageFor(distribution);

    if (image.empty()) {
        std::cerr
            << "No container image known for '" << distribution
            << "'.\nKnown: arch, fedora, debian, ubuntu\n";

        return 1;
    }

    const std::string container =
        nexus::system::containerNameFor(distribution);

    // What this actually costs, before anything is created.
    const Component* found = nullptr;

    for (const Component& component : universe) {
        if (component.name() == requested &&
            component.source() == nexus::Source::Container) {
            found = &component;
            break;
        }
    }

    std::cout
        << "Package:     " << requested << "\n"
        << "From:        " << distribution << " (" << image << ")\n"
        << "Container:   " << container << "\n";

    if (found != nullptr && found->downloadSize() > 0) {
        std::cout
            << "Package:     "
            << nexus::system::formatSize(found->downloadSize())
            << " to fetch\n";
    }

    std::cout
        << "\nThe container is a whole distribution. The first "
        << "package from it\ncosts several hundred megabytes; "
        << "later ones cost only themselves.\n";

    if (!apply) {
        std::cout
            << "\nNothing has been changed. Use --apply to do it.\n";

        return 0;
    }

    if (!assumeYes) {
        std::cout << "\nProceed? [y/N] ";

        std::string answer;

        std::getline(std::cin, answer);

        if (answer != "y" && answer != "Y" && answer != "yes") {
            std::cout << "Nothing has been changed.\n";
            return 1;
        }
    }

    std::cout << "\nPreparing the container.\n";

    const auto prepared =
        nexus::system::Containers::ensure(container, image);

    for (const std::string& line : prepared.output) {
        std::cout << line << "\n";
    }

    if (!prepared.ok) {
        std::cerr
            << "\nCould not prepare the container";

        if (!prepared.error.empty()) {
            std::cerr << ": " << prepared.error;
        }

        std::cerr << "\nNothing has been changed on the host.\n";

        return 1;
    }

    std::cout << "\nInstalling inside it.\n";

    // The container's own package manager does the installing, the
    // same way apt does on the host.
    const std::string install =
        distribution == "arch"
            ? "sudo pacman -Sy --noconfirm " + requested
            : distribution == "fedora"
                ? "sudo dnf install -y " + requested
                : "sudo apt-get install -y " + requested;

    const auto installed =
        nexus::system::Containers::run(container, install);

    for (const std::string& line : installed.output) {
        std::cout << line << "\n";
    }

    if (!installed.ok) {
        std::cerr
            << "\nThe package did not install. The container is "
            << "still there\nand the host is unchanged.\n";

        return 1;
    }

    std::cout << "\nFinding out what it installed.\n";

    // Not "the binary named after the package". Arch's metasploit
    // ships msfconsole, msfvenom and msfdb, and nothing called
    // metasploit.
    const auto binaries = nexus::system::Containers::binariesOf(
        container, distribution, requested);

    if (binaries.paths.empty()) {
        // Say which of the two things happened.
        if (!binaries.queried || binaries.linesSeen == 0) {
            std::cout
                << "\nCould not ask the container what it installed"
                << " (" << binaries.linesSeen << " line(s) back).\n";

            if (!binaries.firstLine.empty()) {
                std::cout
                    << "First line was: " << binaries.firstLine
                    << "\n";
            }
        } else {
            std::cout
                << "\nInstalled. Its " << binaries.linesSeen
                << " file(s) include no commands under /usr/bin.\n";

            if (!binaries.firstLine.empty()) {
                std::cout
                    << "First line was: " << binaries.firstLine
                    << "\n";
            }
        }

        std::cout
            << "Reach the container with:\n"
            << "    distrobox enter " << container << "\n";

        return 0;
    }

    std::cout
        << "\nPutting " << binaries.paths.size()
        << " command(s) on your PATH.\n";

    std::vector<std::string> exported;
    std::vector<std::string> refused;

    for (const std::string& path : binaries.paths) {
        const auto result =
            nexus::system::Containers::exportBinary(container, path);

        const std::size_t slash = path.rfind('/');

        const std::string command =
            slash == std::string::npos
                ? path
                : path.substr(slash + 1);

        if (result.ok) {
            exported.push_back(command);
        } else {
            refused.push_back(command);
        }
    }

    for (const std::string& command : exported) {
        std::cout << "    " << command << "\n";
    }

    if (exported.empty()) {
        std::cout
            << "\nInstalled, but nothing could be exported. Reach "
            << "it with:\n"
            << "    distrobox enter " << container << "\n";

        return 0;
    }

    std::cout
        << "\nDone. Those run inside " << container
        << ". They are on your PATH at\n"
        << "~/.local/bin, so a new shell will find them.\n";

    if (!refused.empty()) {
        std::cout
            << "\n" << refused.size()
            << " could not be exported.\n";
    }

    return 0;
}

int commandSetup(
    const nexus::AliasTable& aliases,
    const std::vector<Component>& installed,
    const std::vector<Component>& universe,
    const nexus::hardware::HardwareInfo& hardware,
    const std::string& directory,
    const std::string& architecture,
    const std::string& chosen,
    bool apply,
    bool assumeYes,
    bool showCommands,
    const std::string& invocation
) {
    const auto loaded =
        nexus::system::parseProfileDirectory(directory);

    if (loaded.profiles.empty()) {
        std::cerr << "No profiles found in " << directory << ".\n";
        return 1;
    }

    // The name is checked before anything is described. An error
    // that arrives after a paragraph of context nobody asked for
    // reads as though the context mattered.
    nexus::Composition composition;

    if (!chosen.empty()) {
        if (!findComposition(loaded.profiles, chosen, composition)) {
            return 1;
        }

        if (composition.refused) {
            std::cerr << composition.refusal << "\n";
            return 1;
        }
    }

    std::cout << "This machine\n\n";

    for (const std::string& capability : hardware.capabilities()) {
        std::cout << "    " << capability << "\n";
    }

    if (!hardware.unreadable.empty()) {
        for (const std::string& problem : hardware.unreadable) {
            std::cout << "    (" << problem << ")\n";
        }
    }

    std::cout << "\n";

    // What each profile would mean here, before anything is chosen.
    if (chosen.empty()) {
        std::cout << "What would you like this machine to be?\n\n";

        for (const nexus::Profile& profile : loaded.profiles) {
            nexus::Profile local = profile;

            if (!architecture.empty()) {
                local.architecture = architecture;
            }

            // Against what is installed, not against what could be.
            // Checking the whole universe reports every profile as
            // satisfied the moment its packages exist in an archive,
            // which is every profile.
            const auto report = nexus::checkProfile(
                local, buildSolver(installed), aliases);

            const std::size_t missing =
                report.items.size() - report.satisfied;

            std::cout
                << "  " << profile.name << "\n"
                << "      " << profile.description << "\n"
                << "      ";

            if (missing == 0) {
                std::cout << "already satisfied";
            } else {
                std::cout
                    << missing << " of " << report.items.size()
                    << " requirement(s) missing";
            }

            if (profile.exclusive) {
                std::cout << "; cannot be combined";
            }

            std::cout << "\n\n";
        }

        std::cout
            << "Choose one, or several separated by commas:\n"
            << "    " << invocation << " <name>[,<name>...]\n"
            << "\nNothing has been changed.\n";

        return 0;
    }

    nexus::Profile profile = composition.profile;

    if (!architecture.empty()) {
        profile.architecture = architecture;
    }

    reportComposition(composition);

    // Two phases, as elsewhere: what is here decides what is
    // satisfied, and only then does the archive say what would fix
    // the rest.
    const auto here = nexus::checkProfile(
        profile, buildSolver(installed), aliases);

    const auto possible = nexus::checkProfile(
        profile, buildSolver(universe), aliases);

    std::cout
        << "Chosen:      " << chosen << "\n"
        << "Satisfied:   " << here.satisfied << " of "
        << here.items.size() << "\n\n";

    std::vector<std::string> wanted;
    std::size_t unavailable = 0;

    for (std::size_t index = 0; index < here.items.size(); ++index) {
        const auto& item = here.items[index];

        if (item.satisfied) {
            continue;
        }

        const bool available =
            index < possible.items.size() &&
            possible.items[index].satisfied &&
            !possible.items[index].provided.empty();

        if (!available) {
            unavailable += 1;

            std::cout
                << "  [missing] " << item.requirement
                << "\n            nothing available provides this\n";
            continue;
        }

        const std::string& selected =
            possible.items[index].provided.front();

        // The package name, not the internal id. Ids carry an
        // architecture suffix so multilib builds stay distinct in the
        // resolver, and "gamemode:amd64" is not something apt will
        // accept.
        std::string name = selected;

        for (const Component& component : universe) {
            if (component.id() == selected) {
                name = component.name();
                break;
            }
        }

        std::cout
            << "  [install] " << item.requirement
            << "\n            " << name << "\n";

        wanted.push_back(name);
    }

    if (wanted.empty()) {
        // Nothing to install is two different situations, and calling
        // both of them success told somebody their machine was
        // already what they asked for while six requirements went
        // unmet.
        if (unavailable > 0) {
            std::cout
                << "\n" << unavailable
                << " requirement(s) cannot be met from what is "
                << "available.\nNothing has been changed.\n";

            return 1;
        }

        std::cout
            << "\nThis machine already is what you asked for.\n";

        return 0;
    }

    if (showCommands) {
        std::cout
            << "\nWhat this would run:\n\n";

        for (const std::string& name : wanted) {
            std::cout
                << "    sudo "
                << (gUseRpmVersions ? "dnf" : "apt-get")
                << " install "
                << (gUseRpmVersions
                        ? "--setopt=install_weak_deps=False "
                        : "--no-install-recommends ")
                << name << "\n";
        }

        std::cout
            << "\nOne at a time on purpose: if the fourth fails the "
            << "first three\nstill happened, and you can see which.\n";

        return 0;
    }

    if (!apply) {
        std::cout
            << "\n" << wanted.size()
            << " thing(s) would be installed.\n"
            << "Use --apply to do it, or --commands to see what it "
            << "would run.\n";

        return 0;
    }

    if (!nexus::system::haveRootPrivileges()) {
        // The invocation already carries the profile and the flags.
        // Appending them again produced a suggestion that named the
        // profile twice and --apply twice.
        std::cout
            << "\nInstalling needs root. Re-run with sudo:\n"
            << "    sudo " << invocation
            << "\n\nNothing has been changed.\n";

        return 1;
    }

    if (!assumeYes) {
        std::cout
            << "\nInstall " << wanted.size()
            << " thing(s)? [y/N] ";

        std::string answer;

        std::getline(std::cin, answer);

        if (answer != "y" && answer != "Y" && answer != "yes") {
            std::cout << "Nothing has been changed.\n";
            return 1;
        }
    }

    // One at a time, each verified before it is applied. A setup that
    // half-succeeds should leave the parts that worked, and say which
    // ones did not.
    std::size_t done = 0;
    std::vector<std::string> failed;

    for (const std::string& name : wanted) {
        std::cout << "\n=== " << name << " ===\n";

        const int status = commandInstall(
            aliases, universe, installed, name, architecture,
            gUseRpmVersions, true, true, false, invocation);

        if (status == 0) {
            done += 1;
        } else {
            failed.push_back(name);
        }
    }

    std::cout
        << "\n" << done << " of " << wanted.size()
        << " installed.\n";

    if (!failed.empty()) {
        std::cout << "\nDid not install:\n";

        for (const std::string& name : failed) {
            std::cout << "    " << name << "\n";
        }

        std::cout
            << "\nThe rest of the system is unchanged and usable.\n";

        return 1;
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
    bool withSnap = false;
    bool withNix = false;
    std::optional<nexus::Source> onlySource;
    std::vector<std::string> archDatabases;
    std::string portagePath;
    std::string fromDistribution = "arch";
    bool explain = false;
    bool apply = false;
    bool assumeYes = false;
    bool showAll = false;
    bool showUnused = false;
    bool showCommands = false;
    std::string vpnInterface = "wg0";
    std::string vpnAddress;
    std::string vpnPeerKey;
    std::string vpnEndpoint;
    std::string vpnRoutes;
    std::string vpnDns;
    std::string vpnPath;
    int vpnKeepalive = 0;
    std::vector<std::string> positional;

    for (std::size_t index = 0; index < arguments.size(); ++index) {
        if (arguments[index] == "--status" && index + 1 < arguments.size()) {
            statusPath = arguments[index + 1];
            index += 1;
            continue;
        }

        static const std::vector<std::pair<std::string,
                                           std::string*>> vpnFlags{
            {"--interface", &vpnInterface},
            {"--address", &vpnAddress},
            {"--peer-key", &vpnPeerKey},
            {"--endpoint", &vpnEndpoint},
            {"--routes", &vpnRoutes},
            {"--dns", &vpnDns},
            {"--config", &vpnPath},
        };

        bool tookVpnFlag = false;

        for (const auto& [flag, target] : vpnFlags) {
            if (arguments[index] == flag &&
                index + 1 < arguments.size()) {
                *target = arguments[index + 1];
                index += 1;
                tookVpnFlag = true;
                break;
            }
        }

        if (tookVpnFlag) {
            continue;
        }

        if (arguments[index] == "--keepalive" &&
            index + 1 < arguments.size()) {
            vpnKeepalive = std::atoi(arguments[index + 1].c_str());
            index += 1;
            continue;
        }

        if (arguments[index] == "--commands") {
            showCommands = true;
            continue;
        }

        if (arguments[index] == "--unused") {
            showUnused = true;
            continue;
        }

        if (arguments[index] == "--all") {
            showAll = true;
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

        if (arguments[index] == "--from" &&
            index + 1 < arguments.size()) {
            const std::string named = arguments[index + 1];

            if (named == "base") {
                onlySource = nexus::Source::Base;
            } else if (named == "flatpak") {
                onlySource = nexus::Source::Flatpak;
                withFlatpak = true;
            } else if (named == "snap") {
                onlySource = nexus::Source::Snap;
                withSnap = true;
            } else if (named == "container") {
                onlySource = nexus::Source::Container;
            } else if (named == "nix") {
                onlySource = nexus::Source::Nix;
                withNix = true;
            } else {
                std::cerr
                    << "Unknown source: " << named << "\n"
                    << "Known: base, flatpak, snap, container, nix\n";
                return 2;
            }

            index += 1;
            continue;
        }

        if (arguments[index] == "--from-distro" &&
            index + 1 < arguments.size()) {
            fromDistribution = arguments[index + 1];
            index += 1;
            continue;
        }

        if (arguments[index] == "--with-portage" &&
            index + 1 < arguments.size()) {
            portagePath = arguments[index + 1];
            index += 1;
            continue;
        }

        if (arguments[index] == "--with-arch" &&
            index + 1 < arguments.size()) {
            archDatabases.push_back(arguments[index + 1]);
            index += 1;
            continue;
        }

        if (arguments[index] == "--with-nix") {
            withNix = true;
            continue;
        }

        if (arguments[index] == "--with-snap") {
            withSnap = true;
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

    // These describe a machine other than this one, and none of them
    // can be answered from the installed set alone.
    //
    // setup is the one that was missed: it exists to install things
    // that are not here, so without the archive every gap reads as
    // "nothing available provides this" and the machine looks
    // unimprovable rather than unconfigured.
    //
    // Installing means installing something not here yet. Generating
    // an image list means describing a machine that does not exist
    // yet -- and resolving that against what happens to be on this
    // one produced a "minimal" profile listing a full desktop's file
    // manager, because that is what the generating machine ran.
    //
    // Making the archive optional here only ever produces a confusing
    // failure or a quietly wrong answer.
    if (command == "install" || command == "image" ||
        command == "setup" || command == "source") {
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

        // Each additional source brings its own vocabulary, loaded
        // alongside the ecosystem's own rather than instead of it, so
        // a capability offered from several places shows options from
        // all of them.
        //
        // Reading a source's packages without its aliases loads the
        // components and offers none of them, which looks exactly
        // like the source being empty.
        if (!archDatabases.empty()) {
            const auto loaded = nexus::system::parseAliasFile(
                aliasDir + "/arch.aliases");

            for (const std::string& problem : loaded.problems) {
                std::cerr << "Warning: " << problem << "\n";
            }

            aliases.merge(loaded.table);
        }

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

        // An Arch database read on a machine that is not Arch: the
        // packages are real and reachable, through a container rather
        // than by installing them here.
        for (const std::string& database : archDatabases) {
            const auto arch =
                nexus::system::readPacmanDatabase(database);

            if (!arch.error.empty()) {
                std::cerr << "Warning: " << arch.error << "\n";
                continue;
            }

            for (const Component& component : arch.components) {
                universe.push_back(component);
                availableOnly.push_back(component);
            }

            haveAvailable = true;

            std::cerr
                << "Loaded " << arch.components.size()
                << " Arch package(s) from " << database << ".\n";
        }

        // Gentoo's tree, read wherever it is. On Gentoo that is
        // /var/db/repos/gentoo/metadata/md5-cache; anywhere else it
        // is a clone, which is how this was built and tested.
        if (!portagePath.empty()) {
            const auto portage =
                nexus::system::readPortageTree(portagePath);

            if (!portage.error.empty()) {
                std::cerr << "Warning: " << portage.error << "\n";
            } else {
                for (const Component& component :
                     portage.components) {

                    universe.push_back(component);
                    availableOnly.push_back(component);
                }

                haveAvailable = true;

                std::cerr
                    << "Loaded " << portage.components.size()
                    << " portage package(s) from "
                    << portage.packagesRead << " ebuild(s); "
                    << portage.conditional
                    << " condition(s) assumed from USE defaults";

                if (portage.liveEbuilds > 0) {
                    std::cerr
                        << ", " << portage.liveEbuilds
                        << " live ebuild(s) skipped";
                }

                std::cerr << ".\n";
            }
        }

        if (withNix) {
            const auto loadedNix = nexus::system::parseAliasFile(
                aliasDir + "/nix.aliases");

            for (const std::string& problem : loadedNix.problems) {
                std::cerr << "Warning: " << problem << "\n";
            }

            aliases.merge(loadedNix.table);

            const auto nix = nexus::system::readNixProfile();

            if (!nix.error.empty()) {
                std::cerr << "Warning: " << nix.error << "\n";
            } else {
                for (const Component& component : nix.components) {
                    universe.push_back(component);
                    availableOnly.push_back(component);
                }

                if (nix.installed > 0) {
                    haveAvailable = true;
                }

                std::cerr
                    << "Loaded " << nix.installed
                    << " Nix package(s).\n";
            }
        }

        if (withSnap) {
            const auto snaps = nexus::system::readSnaps();

            if (!snaps.error.empty()) {
                std::cerr << "Warning: " << snaps.error << "\n";
            } else {
                for (const Component& component : snaps.components) {
                    universe.push_back(component);
                    availableOnly.push_back(component);
                }

                haveAvailable = true;

                std::cerr
                    << "Loaded " << snaps.installed
                    << " snap(s) and " << snaps.infrastructure
                    << " base(s).\n";
            }

            const auto loaded = nexus::system::parseAliasFile(
                aliasDir + "/snap.aliases");

            for (const std::string& problem : loaded.problems) {
                std::cerr << "Warning: " << problem << "\n";
            }

            aliases.merge(loaded.table);
        }

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
                preferRpm, apply, assumeYes, showCommands,
                invocation);
        }

        if (command == "options") {
            if (argument.empty()) {
                std::cerr << "options requires a capability name.\n";
                return 2;
            }

            return commandOptions(
                aliases, universe, installed, argument, arch,
                onlySource);
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
                apply, assumeYes, preferRpm, showCommands,
                invocation);
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

        if (command == "vpn") {
            nexus::system::TunnelSettings settings;

            settings.interfaceName = vpnInterface;
            settings.address = vpnAddress;
            settings.peerPublicKey = vpnPeerKey;
            settings.endpoint = vpnEndpoint;

            if (!vpnRoutes.empty()) {
                settings.allowedIps = vpnRoutes;
            }

            settings.dns = vpnDns;
            settings.keepalive = vpnKeepalive;

            const std::string path =
                vpnPath.empty()
                    ? "/etc/wireguard/" + vpnInterface + ".conf"
                    : vpnPath;

            return commandVpn(
                argument.empty() ? "keys" : argument,
                settings, path, showAll, apply);
        }

        if (command == "source") {
            if (argument.empty()) {
                std::cerr << "source requires a package name.\n";
                return 2;
            }

            if (!havePackages) {
                requirePackages();
                return 1;
            }

            return commandSource(
                universe, installed, argument, arch, showCommands);
        }

        if (command == "guide" || command == "help") {
            return commandGuide(preferRpm);
        }

        if (command == "largest") {
            if (!havePackages) {
                requirePackages();
                return 1;
            }

            const std::size_t howMany =
                argument.empty()
                    ? 15
                    : static_cast<std::size_t>(
                          std::strtoul(argument.c_str(), nullptr, 10));

            std::set<std::string> unusedOnly;

            if (showUnused) {
                std::set<std::string> roots;
                bool knowWhatWasWanted = false;

                if (preferRpm) {
                    // dnf records which packages a person asked for.
                    const auto asked = nexus::system::runCommand(
                        "dnf repoquery --userinstalled --qf "
                        "'%{name}' 2>/dev/null", false);

                    for (const std::string& name : asked.lines) {
                        if (name.empty()) {
                            continue;
                        }

                        for (const Component& component : installed) {
                            if (component.name() == name) {
                                roots.insert(component.id());
                            }
                        }
                    }

                    knowWhatWasWanted = !roots.empty();
                } else {
                    const auto automatic =
                        nexus::system::readAutoInstalled(statesPath);

                    knowWhatWasWanted = !automatic.empty();

                    for (const Component& component : installed) {
                        const std::string key =
                            component.name() + ":" +
                            component.architecture();

                        if (automatic.count(key) == 0) {
                            roots.insert(component.id());
                        }
                    }
                }

                // With no record of what was asked for, nothing can
                // be called unused. Treating that as "everything is
                // unused" listed all 525 components of a base image
                // as removable, which is the same mistake as reading
                // an empty result from broken input.
                if (!knowWhatWasWanted) {
                    std::cerr
                        << "No record of which components were asked "
                        << "for, so nothing\ncan be called unused. "
                        << "Showing everything by size instead.\n\n";

                    showUnused = false;
                } else {
                    const std::set<std::string> reachable =
                        nexus::reachableFrom(
                            installed, roots, buildDetector());

                    for (const Component& component : installed) {
                        if (protectedIds.count(component.id()) > 0) {
                            continue;
                        }

                        if (reachable.count(component.id()) == 0) {
                            unusedOnly.insert(component.id());
                        }
                    }
                }
            }

            return commandLargest(
                result.components, howMany == 0 ? 15 : howMany,
                showUnused, unusedOnly);
        }

        if (command == "services") {
            return commandServices(
                argument == "all" || showAll, preferRpm);
        }

        if (command == "secureboot" || command == "secure-boot") {
            return commandSecureBoot(sysfsRoot);
        }

        if (command == "hardware") {
            return commandHardware(hardware);
        }


        if (command == "container") {
            if (argument.empty()) {
                std::cerr
                    << "container requires a package name.\n"
                    << "    nexus container <package> "
                    << "[--from-distro arch] [--apply]\n";
                return 2;
            }

            return commandContainerInstall(
                universe, argument, fromDistribution, apply,
                assumeYes);
        }

        if (command == "setup") {
            return commandSetup(
                aliases, installed, universe, hardware, profileDir,
                arch, argument, apply, assumeYes, showCommands,
                invocation);
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
