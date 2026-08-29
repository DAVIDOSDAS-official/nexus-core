#include <algorithm>
#include <exception>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include <nexus/component.hpp>
#include <nexus/conflict_detector.hpp>
#include <nexus/solver.hpp>
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
        << "    nexus solve <capability> [--prefer id] [--require id]\n"
        << "\n"
        << "Options:\n"
        << "    --status <path>   dpkg status file\n"
        << "                      (default: /var/lib/dpkg/status)\n"
        << "\n"
        << "This command never modifies the system.\n";
}

const Component* find(
    const DpkgSourceResult& result,
    const std::string& id
) {
    for (const Component& component : result.components) {
        if (component.id() == id) {
            return &component;
        }
    }

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

int commandScan(const DpkgSourceResult& result) {
    std::cout
        << "Source:      dpkg status\n"
        << "Stanzas:     " << result.stanzasRead << "\n"
        << "Components:  " << result.components.size() << "\n"
        << "Skipped:     " << result.stanzasSkipped << "\n"
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

    std::map<std::string, std::size_t> byKind;

    for (const ModelGap& gap : result.gaps) {
        byKind[toString(gap.kind)] += 1;
    }

    std::cout << "\nModel gaps:\n";

    if (byKind.empty()) {
        std::cout << "    none\n";
    }

    for (const auto& [kind, count] : byKind) {
        std::cout << "    " << kind << ": " << count << "\n";
    }

    return 0;
}

int commandWhatProvides(
    const DpkgSourceResult& result,
    const std::string& capability
) {
    std::vector<std::string> providers;

    for (const Component& component : result.components) {
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
    const DpkgSourceResult& result,
    const std::string& id
) {
    const Component* target = find(result, id);

    if (target == nullptr) {
        std::cout << "Unknown component: " << id << "\n";
        return 1;
    }

    std::cout << "Component:\n    " << id << "\n\n";

    std::vector<std::string> dependents;

    for (const Component& component : result.components) {
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
    const DpkgSourceResult& result,
    const std::string& id
) {
    const Component* component = find(result, id);

    if (component == nullptr) {
        std::cout << "Unknown component: " << id << "\n";
        return 1;
    }

    std::cout
        << "ID:       " << component->id() << "\n"
        << "Version:  " << component->version() << "\n"
        << "Type:     " << toString(component->type()) << "\n";

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

    std::vector<const ModelGap*> gaps;

    for (const ModelGap& gap : result.gaps) {
        if (gap.componentId == id) {
            gaps.push_back(&gap);
        }
    }

    if (!gaps.empty()) {
        std::cout
            << "\nNot represented by the component model ("
            << gaps.size() << "):\n";

        for (const ModelGap* gap : gaps) {
            std::cout
                << "    [" << toString(gap->kind) << "] "
                << gap->field << ": " << gap->detail << "\n";
        }
    }

    return 0;
}

int commandConflicts(const DpkgSourceResult& result) {
    const nexus::ConflictDetector detector(
        [](const std::string& left, const std::string& right) {
            return nexus::system::compareVersions(left, right);
        }
    );

    std::size_t declared = 0;

    for (const Component& component : result.components) {
        declared += component.conflicts().size();
    }

    std::cout
        << "Components:          " << result.components.size() << "\n"
        << "Declared conflicts:  " << declared << "\n\n";

    const auto conflicts = detector.detect(result.components);

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
    const DpkgSourceResult& result,
    const std::string& capability,
    const std::string& prefer,
    const std::string& require
) {
    nexus::Solver solver(
        result.components,
        nexus::ConflictDetector(
            [](const std::string& left, const std::string& right) {
                return nexus::system::compareVersions(left, right);
            }
        )
    );

    nexus::SolverRequest request;
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
        << "Status:      " << toString(solution.status) << "\n"
        << "Decisions:   " << solution.decisions << "\n"
        << "Backtracks:  " << solution.backtracks << "\n\n";

    if (solution.status != nexus::SolverStatus::Success) {
        std::cout << solution.reason << "\n";

        if (!solution.blockedOn.empty()) {
            std::cout << "\nBlocked on:\n    "
                      << solution.blockedOn << "\n";
        }

        return 1;
    }

    std::cout
        << "Selected " << solution.selected.size()
        << " component(s).\n\n"
        << "Why each one:\n";

    for (const nexus::SolverStep& step : solution.steps) {
        std::cout << "    " << step.selected;

        if (step.wasBacktrackedInto) {
            std::cout << "  [after backtracking]";
        }

        std::cout << "\n        " << step.reason << "\n";
    }

    return 0;
}

int commandGaps(
    const DpkgSourceResult& result,
    const std::string& kindFilter
) {
    std::size_t shown = 0;

    for (const ModelGap& gap : result.gaps) {
        const std::string kind = toString(gap.kind);

        if (!kindFilter.empty() && kind != kindFilter) {
            continue;
        }

        std::cout
            << gap.componentId << "  ["
            << kind << "]  "
            << gap.field << ": " << gap.detail << "\n";

        shown += 1;

        if (shown >= 200) {
            std::cout << "... truncated at 200 entries\n";
            break;
        }
    }

    if (shown == 0) {
        std::cout << "No gaps recorded.\n";
    }

    return 0;
}

}

int main(int argc, char** argv) {
    std::vector<std::string> arguments(argv + 1, argv + argc);

    std::string statusPath = "/var/lib/dpkg/status";
    std::string prefer;
    std::string require;
    std::vector<std::string> positional;

    for (std::size_t index = 0; index < arguments.size(); ++index) {
        if (arguments[index] == "--status" && index + 1 < arguments.size()) {
            statusPath = arguments[index + 1];
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
        const nexus::system::DpkgSource source(statusPath);
        const DpkgSourceResult result = source.load();

        if (command == "scan") {
            return commandScan(result);
        }

        if (command == "what-provides") {
            if (argument.empty()) {
                std::cerr << "what-provides requires a capability name.\n";
                return 2;
            }

            return commandWhatProvides(result, argument);
        }

        if (command == "why") {
            if (argument.empty()) {
                std::cerr << "why requires a component id.\n";
                return 2;
            }

            return commandWhy(result, argument);
        }

        if (command == "inspect") {
            if (argument.empty()) {
                std::cerr << "inspect requires a component id.\n";
                return 2;
            }

            return commandInspect(result, argument);
        }

        if (command == "solve") {
            if (argument.empty()) {
                std::cerr << "solve requires a capability name.\n";
                return 2;
            }

            return commandSolve(result, argument, prefer, require);
        }

        if (command == "conflicts") {
            return commandConflicts(result);
        }

        if (command == "gaps") {
            return commandGaps(result, argument);
        }

        std::cerr << "Unknown command: " << command << "\n\n";
        printUsage();
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << "\n";
        return 1;
    }
}
