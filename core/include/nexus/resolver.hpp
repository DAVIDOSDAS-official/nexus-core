#pragma once

#include <string>
#include <vector>

#include <nexus/capability.hpp>
#include <nexus/component.hpp>
#include <nexus/resolution.hpp>
#include <nexus/resolution_plan.hpp>
#include <nexus/resolution_request.hpp>

namespace nexus {

class Resolver {
public:
    explicit Resolver(std::vector<Component> components);

    ResolutionResult resolve(
        const ResolutionRequest& request
    ) const;

    ResolutionPlan createPlan(
        const ResolutionRequest& request
    ) const;

private:
    const Component* findComponent(
        const std::string& id
    ) const;

    ResolutionResult resolveCapability(
        const Capability& capability,
        const std::string& preferredProvider,
        const std::string& requiredProvider,
        std::vector<std::string>& resolving,
        ResolutionPlan& plan
    ) const;

    std::vector<Component> components_;
};

}
