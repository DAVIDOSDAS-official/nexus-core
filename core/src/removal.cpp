#include <nexus/removal.hpp>

#include <algorithm>

namespace nexus {

namespace {

const Component* findById(
    const std::vector<Component>& components,
    const std::string& id
) {
    for (const Component& component : components) {
        if (component.id() == id || component.name() == id) {
            return &component;
        }
    }

    return nullptr;
}

// Components still in the kept set that directly ask for something
// the target provides. Used to explain a blocked removal.
std::vector<std::string> directRequirers(
    const Component& target,
    const std::vector<Component>& installed,
    const std::set<std::string>& kept
) {
    std::vector<std::string> names;

    std::set<std::string> provided;

    provided.insert(target.name());

    for (const Capability& capability : target.providedCapabilities()) {
        provided.insert(capability.name());
    }

    for (const Component& component : installed) {
        if (component.id() == target.id()) {
            continue;
        }

        if (kept.count(component.id()) == 0) {
            continue;
        }

        for (const Requirement& requirement : component.requirements()) {
            const bool wantsIt = std::any_of(
                requirement.alternatives.begin(),
                requirement.alternatives.end(),
                [&provided](const Constraint& option) {
                    return provided.count(option.capability) > 0;
                }
            );

            if (wantsIt) {
                names.push_back(component.id());
                break;
            }
        }
    }

    std::sort(names.begin(), names.end());

    return names;
}

}

RemovalPlan planRemoval(
    const std::string& target,
    const std::vector<Component>& installed,
    const std::set<std::string>& roots,
    const Solver& solver,
    const std::string& architecture
) {
    RemovalPlan plan;

    plan.target = target;

    const Component* component = findById(installed, target);

    if (component == nullptr) {
        plan.reason = target + " is not installed.";
        return plan;
    }

    plan.target = component->id();

    // Two solves, not one. The first establishes what the roots
    // actually hold up today; the second does the same without this
    // component. The difference is what the target -- and only the
    // target -- was keeping alive.
    //
    // A single solve would blame the removal for anything that was
    // already orphaned, which makes every removal look identical.
    const auto solveRoots =
        [&](const std::string& excluded) {
            SolverRequest request;

            request.architecture = architecture;

            for (const std::string& root : roots) {
                if (!excluded.empty() && root == excluded) {
                    continue;
                }

                request.requirements.push_back(
                    Requirement(Constraint(root))
                );
            }

            return solver.solve(request);
        };

    const SolverResult baseline = solveRoots("");

    if (baseline.status != SolverStatus::Success) {
        plan.reason =
            "The system does not currently resolve, so no removal "
            "can be proposed. " + baseline.reason;

        if (!baseline.blockedOn.empty()) {
            plan.reason += " Blocked on: " + baseline.blockedOn;
        }

        return plan;
    }

    const std::set<std::string> before(
        baseline.selected.begin(),
        baseline.selected.end()
    );

    const SolverResult result = solveRoots(component->id());

    if (result.status != SolverStatus::Success) {
        // Without a complete re-solve there is no trustworthy answer,
        // and a guess here would mean deleting the wrong things.
        plan.reason =
            "The system could not be resolved without " +
            plan.target + ", so no removal can be proposed. " +
            result.reason;

        if (!result.blockedOn.empty()) {
            plan.reason += " Blocked on: " + result.blockedOn;
        }

        return plan;
    }

    const std::set<std::string> kept(
        result.selected.begin(),
        result.selected.end()
    );

    // Still needed by something that remains.
    if (kept.count(component->id()) > 0) {
        plan.requiredBy = directRequirers(*component, installed, kept);
        plan.reason =
            plan.target + " is still required by something that " +
            "would remain installed.";

        return plan;
    }

    plan.possible = true;
    plan.removed.push_back(component->id());

    // Only what was held up before and is not held up now.
    for (const std::string& id : before) {
        if (id == component->id()) {
            continue;
        }

        if (kept.count(id) > 0) {
            continue;
        }

        // Synthetic components are not packages and cannot be
        // removed.
        if (id == "system-hardware") {
            continue;
        }

        plan.orphaned.push_back(id);
        plan.removed.push_back(id);
    }

    std::sort(plan.orphaned.begin(), plan.orphaned.end());
    std::sort(plan.removed.begin(), plan.removed.end());

    plan.reason =
        plan.target + " can be removed, along with " +
        std::to_string(plan.orphaned.size()) +
        " component(s) that nothing else needs.";

    return plan;
}

}
