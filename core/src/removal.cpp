#include <nexus/removal.hpp>

#include <algorithm>
#include <map>
#include <vector>

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

// capability name -> positions of installed components providing it.
std::map<std::string, std::vector<std::size_t>> buildIndex(
    const std::vector<Component>& installed
) {
    std::map<std::string, std::vector<std::size_t>> index;

    for (std::size_t position = 0;
         position < installed.size();
         ++position) {

        const Component& component = installed[position];

        const auto add = [&](const std::string& key) {
            std::vector<std::size_t>& entries = index[key];

            if (entries.empty() || entries.back() != position) {
                entries.push_back(position);
            }
        };

        add(component.id());
        add(component.name());

        for (const Capability& capability :
             component.providedCapabilities()) {

            add(capability.name());
        }
    }

    return index;
}

}

std::set<std::string> reachableFrom(
    const std::vector<Component>& installed,
    const std::set<std::string>& roots,
    const ConflictDetector& detector
) {
    const auto index = buildIndex(installed);

    std::vector<bool> marked(installed.size(), false);
    std::vector<std::size_t> pending;

    const auto mark = [&](std::size_t position) {
        if (!marked[position]) {
            marked[position] = true;
            pending.push_back(position);
        }
    };

    for (const std::string& root : roots) {
        const auto entry = index.find(root);

        if (entry == index.end()) {
            continue;
        }

        for (std::size_t position : entry->second) {
            if (installed[position].id() == root ||
                installed[position].name() == root) {
                mark(position);
            }
        }
    }

    while (!pending.empty()) {
        const std::size_t position = pending.back();

        pending.pop_back();

        const auto follow = [&](const Constraint& option) {
            const auto entry = index.find(option.capability);

            if (entry == index.end()) {
                return;
            }

            for (std::size_t candidate : entry->second) {
                if (detector.matches(installed[candidate], option)) {
                    mark(candidate);
                }
            }
        };

        for (const Requirement& requirement :
             installed[position].requirements()) {

            for (const Constraint& option : requirement.alternatives) {
                follow(option);
            }
        }

        // Recommendations keep things alive too. Suggests do not:
        // apt treats them as optional and so does this.
        for (const Capability& recommended :
             installed[position].recommendedCapabilities()) {

            follow(Constraint(recommended.name()));
        }
    }

    std::set<std::string> result;

    for (std::size_t position = 0;
         position < installed.size();
         ++position) {

        if (marked[position]) {
            result.insert(installed[position].id());
        }
    }

    return result;
}

RemovalPlan planRemoval(
    const std::string& target,
    const std::vector<Component>& installed,
    const std::set<std::string>& roots,
    const ConflictDetector& detector,
    const std::string& architecture
) {
    (void)architecture;

    RemovalPlan plan;

    plan.target = target;

    const Component* component = findById(installed, target);

    if (component == nullptr) {
        plan.reason = target + " is not installed.";
        return plan;
    }

    plan.target = component->id();

    const std::set<std::string> before =
        reachableFrom(installed, roots, detector);

    std::set<std::string> without = roots;

    without.erase(component->id());
    without.erase(component->name());

    const std::set<std::string> after =
        reachableFrom(installed, without, detector);

    // Still held up by something that remains.
    if (after.count(component->id()) > 0) {
        std::set<std::string> provided;

        provided.insert(component->name());
        provided.insert(component->id());

        for (const Capability& capability :
             component->providedCapabilities()) {

            provided.insert(capability.name());
        }

        for (const Component& other : installed) {
            if (other.id() == component->id()) {
                continue;
            }

            if (after.count(other.id()) == 0) {
                continue;
            }

            for (const Requirement& requirement : other.requirements()) {
                const bool wantsIt = std::any_of(
                    requirement.alternatives.begin(),
                    requirement.alternatives.end(),
                    [&provided](const Constraint& option) {
                        return provided.count(option.capability) > 0;
                    }
                );

                if (wantsIt) {
                    plan.requiredBy.push_back(other.id());
                    break;
                }
            }
        }

        std::sort(plan.requiredBy.begin(), plan.requiredBy.end());

        plan.reason =
            plan.target + " is still required by something that " +
            "would remain installed.";

        return plan;
    }

    plan.possible = true;
    plan.removed.push_back(component->id());

    for (const std::string& id : before) {
        if (id == component->id() || id == "system-hardware") {
            continue;
        }

        if (after.count(id) > 0) {
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
