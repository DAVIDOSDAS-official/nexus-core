#include <nexus/dependency_graph.hpp>

namespace nexus {

void DependencyGraph::addComponent(const std::string& componentId) {
    dependencies_.try_emplace(componentId);
}

void DependencyGraph::addDependency(
    const std::string& componentId,
    const std::string& dependencyId
) {
    addComponent(componentId);
    addComponent(dependencyId);

    dependencies_[componentId].push_back(dependencyId);
}

const std::vector<std::string>& DependencyGraph::dependencies(
    const std::string& componentId
) const {
    static const std::vector<std::string> empty;

    auto it = dependencies_.find(componentId);

    if (it == dependencies_.end()) {
        return empty;
    }

    return it->second;
}

bool DependencyGraph::hasCycleFrom(
    const std::string& componentId,
    std::unordered_set<std::string>& visiting,
    std::unordered_set<std::string>& visited
) const {
    if (visiting.contains(componentId)) {
        return true;
    }

    if (visited.contains(componentId)) {
        return false;
    }

    visiting.insert(componentId);

    auto it = dependencies_.find(componentId);

    if (it != dependencies_.end()) {
        for (const auto& dependency : it->second) {
            if (hasCycleFrom(dependency, visiting, visited)) {
                return true;
            }
        }
    }

    visiting.erase(componentId);
    visited.insert(componentId);

    return false;
}

bool DependencyGraph::hasCycle() const {
    std::unordered_set<std::string> visiting;
    std::unordered_set<std::string> visited;

    for (const auto& [componentId, _] : dependencies_) {
        if (hasCycleFrom(componentId, visiting, visited)) {
            return true;
        }
    }

    return false;
}

}
