#pragma once

#include <vector>

#include <nexus/capability.hpp>
#include <nexus/component.hpp>
#include <nexus/resolution.hpp>
#include <nexus/resolution_request.hpp>

namespace nexus {

class Resolver {
public:
    explicit Resolver(std::vector<Component> components);

    ResolutionResult resolve(
        const ResolutionRequest& request
    ) const;

private:
    std::vector<Component> components_;
};

}
