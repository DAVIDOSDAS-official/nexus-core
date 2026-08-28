#pragma once

#include <vector>

#include <nexus/capability.hpp>
#include <nexus/component.hpp>

namespace nexus {

class Resolver {
public:
    explicit Resolver(std::vector<Component> components);

    const Component* findProvider(const Capability& capability) const;

private:
    std::vector<Component> components_;
};

}
