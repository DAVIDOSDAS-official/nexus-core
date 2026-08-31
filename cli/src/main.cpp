#include <algorithm>
#include <exception>
#include <iostream>
#include <cstdlib>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <vector>

#include <nexus/component.hpp>
#include <nexus/conflict_detector.hpp>
#include <nexus/profile_check.hpp>
#include <nexus/alias.hpp>
#include <nexus/options.hpp>
#include <nexus/system/alias_file.hpp>
#include <nexus/removal.hpp>
#include <nexus/transaction.hpp>
#include <nexus/system/auto_installed.hpp>
#include <nexus/solver.hpp>
#include <nexus/hardware/hardware.hpp>
#include <nexus/system/apt_source.hpp>
#include <nexus/system/rpm_database.hpp>
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
        << "    nexus plan <capability>\n"
        << "    nexus remove <component>\n"
        << "    nexus hardware\n"
        << "    nexus profile list\n"
        << "    nexus profile show <name>\n"
        << "    nexus profile check <name>\n"
        << "    nexus solve <capability> [--arch a] [--prefer id]\n"
        << "                             [--require id]\n"
        << "\n"
        << "Options:\n"
        << "    --status <path>   dpkg status file\n"
        << "    --profiles <dir>  profile directory\n"
        << "    --arch <arch>     target architecture\n"
        << "    --with-available  also read available packages\n"
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

nexus::Solver buildSolver(const std::vector<Component>& components) {
    return nexus::Solver(
        components,
        nexus::ConflictDetector(
            [](const std::string& left, const std::string& right) {
                return nexus::system::compareVersions(left, right);
            }
        )
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
    const nexus::ConflictDetector detector(
        [](const std::string& left, const std::string& right) {
            return nexus::system::compareVersions(left, right);
        }
    );

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
        nexus::Requirement(nexus::Constraint(capability))
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
            std::cout
                << "\nNothing available provides:\n    "
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


int commandOptions(
    const std::vector<Component>& universe,
    const std::vector<Component>& installed,
    const std::string& capability,
    const std::string& architecture
) {
    const nexus::ConflictDetector detector(
        [](const std::string& left, const std::string& right) {
            return nexus::system::compareVersions(left, right);
        }
    );

    const auto report = nexus::findOptions(
        capability, universe, installed,
        buildSolver(universe), detector, architecture);

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

        std::cout << "\n";

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
            std::cout
                << "\nNothing available provides:\n    "
                << solution.blockedOn << "\n";
        }

        return 1;
    }

    const auto plan = nexus::planTransaction(
        solution.selected,
        universe,
        nexus::ConflictDetector(
            [](const std::string& left, const std::string& right) {
                return nexus::system::compareVersions(left, right);
            }
        )
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
    const std::string& architecture
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
        nexus::ConflictDetector(
            [](const std::string& left, const std::string& right) {
                return nexus::system::compareVersions(left, right);
            }
        ),
        architecture);

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

    std::cout
        << "\nNothing has been changed; this command only reports.\n";

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

    const nexus::Profile* chosen = nullptr;

    for (const nexus::Profile& profile : loaded.profiles) {
        if (profile.name == name) {
            chosen = &profile;
            break;
        }
    }

    if (chosen == nullptr) {
        std::cerr << "Unknown profile: " << name << "\n";
        return 1;
    }

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
    bool explain = false;
    std::vector<std::string> positional;

    for (std::size_t index = 0; index < arguments.size(); ++index) {
        if (arguments[index] == "--status" && index + 1 < arguments.size()) {
            statusPath = arguments[index + 1];
            index += 1;
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

        // Hardware belongs in the installed baseline too: the GPU in
        // this machine is not something you install, it is something
        // that is already here.
        std::vector<Component> installed = result.components;
        installed.push_back(nexus::hardware::asComponent(hardware));

        std::vector<Component> universe = installed;

        bool haveAvailable = false;

        if (withAvailable && preferRpm) {
            const nexus::system::RpmRepository repositories(rpmCache);
            const auto available = repositories.load();

            for (const auto& skipped : available.skipped) {
                std::cerr
                    << "Warning: skipped " << skipped.path
                    << " (" << skipped.reason << ")\n";
            }

            if (!available.components.empty()) {
                universe = nexus::system::mergeAvailable(
                    universe, available.components);

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
                universe = nexus::system::mergeAvailable(
                    universe, available.components);

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

        if (command == "options") {
            if (argument.empty()) {
                std::cerr << "options requires a capability name.\n";
                return 2;
            }

            return commandOptions(
                universe, installed, argument, arch);
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
                installed, argument, statesPath, arch);
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
                universe, argument, prefer, require, arch, explain);
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
