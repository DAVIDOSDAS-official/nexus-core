#include <nexus/resolver.hpp>

#include <utility>

namespace nexus {

Resolver::Resolver(std::vector<Component> components)
    : components_(std::move(components)) {
}

ResolutionResult Resolver::resolve(
    const ResolutionRequest& request
) const {
    ResolutionResult result{
        ResolutionStatus::NotFound,
        "",
        {},
        ""
    };

    for (const Component& component : components_) {
        for (const Capability& provided :
             component.providedCapabilities()) {

            if (provided.name() == request.capability.name()) {
                result.candidates.push_back(component.id());
            }
        }
    }

    if (result.candidates.empty()) {
        result.status = ResolutionStatus::NotFound;
        result.reason =
            "No component provides the requested capability.";

        return result;
    }

    if (request.requiredProvider.has_value()) {
        const std::string& required =
            request.requiredProvider.value();

        for (const std::string& candidate : result.candidates) {
            if (candidate == required) {
                result.status = ResolutionStatus::Success;
                result.selectedProvider = candidate;
                result.reason =
                    "The required provider was found.";

                return result;
            }
        }

        result.status = ResolutionStatus::NotFound;
        result.reason =
            "The required provider does not provide the requested capability.";

        return result;
    }

    if (request.preferredProvider.has_value()) {
        const std::string& preferred =
            request.preferredProvider.value();

        for (const std::string& candidate : result.candidates) {
            if (candidate == preferred) {
                result.status = ResolutionStatus::Success;
                result.selectedProvider = candidate;
                result.reason =
                    "The preferred provider was selected.";

                return result;
            }
        }
    }

    if (result.candidates.size() > 1) {
        result.status = ResolutionStatus::Ambiguous;
        result.reason =
            "Multiple components provide the requested capability.";

        return result;
    }

    result.status = ResolutionStatus::Success;
    result.selectedProvider = result.candidates.front();
    result.reason =
        "Exactly one compatible provider was found.";

    return result;
}

}
