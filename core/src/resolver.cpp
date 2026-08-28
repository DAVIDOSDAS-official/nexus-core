#include <nexus/resolver.hpp>

#include <utility>

namespace nexus {

Resolver::Resolver(std::vector<Component> components)
    : components_(std::move(components)) {
}

ResolutionResult Resolver::resolve(
    const Capability& capability
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

            if (provided.name() == capability.name()) {
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
