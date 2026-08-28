#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace nexus {

class DependencyGraph {
public:
    // Add a component to the graph.
    void addComponent(const std::string& componentId);

    // Add a dependency:
    // componentId requires dependencyId.
    void addDependency(
        const std::string& componentId,
        const std::string& dependencyId
    );

    // Check whether the graph contains a dependency cycle.
    bool hasCycle() const;

    // Return the direct dependencies of a component.
    const std::vector<std::string>& dependencies(
        const std::string& componentId
    ) const;

private:
    bool hasCycleFrom(
        const std::string& componentId,
        std::unordered_set<std::string>& visiting,
        std::unordered_set<std::string>& visited
    ) const;

    std::unordered_map<
        std::string,
        std::vector<std::string>
    > dependencies_;
};

}
