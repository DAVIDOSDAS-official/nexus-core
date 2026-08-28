#include <nexus/resolver.hpp>
#include <functional>
#include <algorithm>
#include <utility>

namespace nexus {

Resolver::Resolver(std::vector<Component> components)
    : components_(std::move(components)) {
}

const Component* Resolver::findComponent(
    const std::string& id
) const {
    for (const Component& component : components_) {
        if (component.id() == id) {
            return &component;
        }
    }

    return nullptr;
}

ResolutionResult Resolver::resolve(
    const ResolutionRequest& request
) const {
    ResolutionPlan plan;
    std::vector<std::string> resolving;

    return resolveCapability(
        request.capability,
        request.preferredProvider.value_or(""),
        request.requiredProvider.value_or(""),
        resolving,
        plan
    );
}

ResolutionResult Resolver::resolveCapability(
    const Capability& capability,
    const std::string& preferredProvider,
    const std::string& requiredProvider,
    std::vector<std::string>& resolving,
    ResolutionPlan& plan
) const {
    ResolutionResult result{
        ResolutionStatus::NotFound,
        "",
        {},
        "",
        plan
    };

    for (const Component& component : components_) {
        for (const Capability& provided :
             component.providedCapabilities()) {

            if (provided.name() == capability.name()) {
                result.candidates.push_back(component.id());
                break;
            }
        }
    }

    if (result.candidates.empty()) {
        result.status = ResolutionStatus::NotFound;
        result.reason =
            "No component provides the requested capability.";

        return result;
    }

    std::string selected;

    if (!requiredProvider.empty()) {
        const auto it = std::find(
            result.candidates.begin(),
            result.candidates.end(),
            requiredProvider
        );

        if (it == result.candidates.end()) {
            result.status = ResolutionStatus::NotFound;
            result.reason =
                "The required provider does not provide the requested capability.";

            return result;
        }

        selected = *it;
    } else if (!preferredProvider.empty()) {
        const auto it = std::find(
            result.candidates.begin(),
            result.candidates.end(),
            preferredProvider
        );

        if (it != result.candidates.end()) {
            selected = *it;
        }
    }

    if (selected.empty()) {
        if (result.candidates.size() > 1) {
            result.status = ResolutionStatus::Ambiguous;
            result.reason =
                "Multiple components provide the requested capability.";

            return result;
        }

        selected = result.candidates.front();
    }

    const Component* component = findComponent(selected);

    if (component == nullptr) {
        result.status = ResolutionStatus::NotFound;
        result.reason =
            "Selected provider could not be found.";

        return result;
    }

    if (std::find(
            resolving.begin(),
            resolving.end(),
            selected
        ) != resolving.end()) {

        result.status = ResolutionStatus::NotFound;
        result.reason =
            "Dependency cycle detected while resolving component.";

        return result;
    }

    resolving.push_back(selected);

    for (const Capability& requirement :
         component->requiredCapabilities()) {

        ResolutionResult dependencyResult =
            resolveCapability(
                requirement,
                "",
                "",
                resolving,
                plan
            );

        if (dependencyResult.status != ResolutionStatus::Success) {
            resolving.pop_back();

            result.status = dependencyResult.status;
            result.reason =
                "Failed to resolve dependency '" +
                requirement.name() +
                "': " +
                dependencyResult.reason;

            result.plan = plan;

            return result;
        }
    }

    resolving.pop_back();

    if (std::find(
            plan.install.begin(),
            plan.install.end(),
            selected
        ) == plan.install.end()) {

        plan.install.push_back(selected);
    }

    result.status = ResolutionStatus::Success;
    result.selectedProvider = selected;
    result.reason =
        "Provider and all required dependencies were resolved.";
    result.plan = plan;

    return result;
}

ResolutionPlan Resolver::createPlan(
    const ResolutionRequest& request
) const {
    ResolutionPlan plan{
        ResolutionPlanStatus::Failed,
        "",
        {},
        {},
        {}
    };

    ResolutionResult root = resolve(request);

    if (root.status != ResolutionStatus::Success) {
        plan.reason =
            "The requested capability could not be resolved.";

        return plan;
    }

    std::vector<std::string> visiting;
    std::vector<std::string> visited;

    auto contains = [](const std::vector<std::string>& values,
                       const std::string& value) {
        for (const std::string& item : values) {
            if (item == value) {
                return true;
            }
        }

        return false;
    };

    auto findComponent = [this](const std::string& id)
        -> const Component* {
        for (const Component& component : components_) {
            if (component.id() == id) {
                return &component;
            }
        }

        return nullptr;
    };

    std::function<bool(const std::string&)> visit;

    visit = [&](const std::string& componentId) -> bool {
        if (contains(visiting, componentId)) {
            plan.reason =
                "Dependency cycle detected while creating resolution plan.";

            return false;
        }

        if (contains(visited, componentId)) {
            return true;
        }

        const Component* component = findComponent(componentId);

        if (component == nullptr) {
            plan.reason =
                "A component required by the resolution plan was not found.";

            return false;
        }

        visiting.push_back(componentId);

        for (const Capability& dependency :
             component->requiredCapabilities()) {

            ResolutionRequest dependencyRequest{
                dependency,
                std::nullopt,
                std::nullopt
            };

            ResolutionResult dependencyResult =
                resolve(dependencyRequest);

            if (dependencyResult.status != ResolutionStatus::Success) {
                plan.reason =
                    "A required dependency could not be resolved.";

                return false;
            }

            if (!visit(dependencyResult.selectedProvider)) {
                return false;
            }
        }

        visiting.pop_back();
        visited.push_back(componentId);

        if (!contains(plan.install, componentId)) {
            plan.install.push_back(componentId);
        }

        return true;
    };

    if (!visit(root.selectedProvider)) {
        plan.install.clear();
        return plan;
    }

    plan.status = ResolutionPlanStatus::Ready;
    plan.reason =
        "Resolution plan created successfully.";

    return plan;
}

}
