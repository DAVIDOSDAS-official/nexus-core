#include <nexus/system/identity.hpp>

#include <map>
#include <string>

namespace nexus::system {

void qualifyAmbiguousIds(std::vector<Component>& components) {
    std::map<std::string, int> counts;

    for (const Component& component : components) {
        counts[component.name()] += 1;
    }

    for (Component& component : components) {
        if (counts[component.name()] > 1) {
            component.setId(
                component.name() + ":" + component.architecture()
            );
        } else {
            component.setId(component.name());
        }
    }
}

}
