#include <nexus/resolver.hpp>

#include <utility>

namespace nexus {

Resolver::Resolver(std::vector<Component> components)
    : components_(std::move(components)) {
}

const Component* Resolver::findProvider(
    const Capability& capability
) const {
    for (const Component& component : components_) {
        for (const Capability& provided : component.providedCapabilities()) {
            if (provided.name() == capability.name()) {
                return &component;
            }
        }
    }

    return nullptr;
}

}
