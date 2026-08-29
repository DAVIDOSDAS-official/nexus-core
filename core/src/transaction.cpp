#include <nexus/transaction.hpp>

#include <algorithm>
#include <map>
#include <queue>
#include <set>

namespace nexus {

namespace {

struct Node {
    const Component* component = nullptr;

    // Components this one must follow.
    std::set<std::size_t> waitsFor;

    // Components waiting on this one.
    std::set<std::size_t> blocks;

    bool hasPreDependency = false;
};

}

TransactionPlan planTransaction(
    const std::vector<std::string>& selected,
    const std::vector<Component>& universe,
    const ConflictDetector& detector
) {
    TransactionPlan plan;

    // Only the selected components take part. Ordering is about this
    // transaction, not about everything that exists.
    std::map<std::string, std::size_t> positionOf;
    std::vector<Node> nodes;

    for (const std::string& id : selected) {
        for (const Component& component : universe) {
            if (component.id() != id) {
                continue;
            }

            positionOf[id] = nodes.size();
            nodes.push_back(Node{&component, {}, {}, false});
            break;
        }
    }

    if (nodes.size() != selected.size()) {
        plan.reason =
            "Some selected components are not present in the "
            "universe, so no order can be produced.";

        return plan;
    }

    // capability -> the selected components providing it.
    std::map<std::string, std::vector<std::size_t>> providers;

    for (std::size_t index = 0; index < nodes.size(); ++index) {
        const Component& component = *nodes[index].component;

        const auto add = [&](const std::string& key) {
            std::vector<std::size_t>& entries = providers[key];

            if (entries.empty() || entries.back() != index) {
                entries.push_back(index);
            }
        };

        add(component.id());
        add(component.name());

        for (const Capability& capability :
             component.providedCapabilities()) {

            add(capability.name());
        }
    }

    // An edge means "this must come first".
    for (std::size_t index = 0; index < nodes.size(); ++index) {
        for (const Requirement& requirement :
             nodes[index].component->requirements()) {

            for (const Constraint& option : requirement.alternatives) {
                const auto entry = providers.find(option.capability);

                if (entry == providers.end()) {
                    continue;
                }

                for (std::size_t provider : entry->second) {
                    if (provider == index) {
                        continue;
                    }

                    if (!detector.matches(
                            *nodes[provider].component, option)) {
                        continue;
                    }

                    nodes[index].waitsFor.insert(provider);
                    nodes[provider].blocks.insert(index);

                    if (requirement.pre) {
                        nodes[index].hasPreDependency = true;
                    }
                }
            }
        }
    }

    // Kahn's algorithm. Whatever is left over is in a cycle.
    std::vector<std::size_t> remaining(nodes.size());

    for (std::size_t index = 0; index < nodes.size(); ++index) {
        remaining[index] = nodes[index].waitsFor.size();
    }

    std::vector<std::size_t> ready;

    for (std::size_t index = 0; index < nodes.size(); ++index) {
        if (remaining[index] == 0) {
            ready.push_back(index);
        }
    }

    // Deterministic order: same input, same plan.
    const auto byName = [&nodes](std::size_t left, std::size_t right) {
        return nodes[left].component->id() >
               nodes[right].component->id();
    };

    std::make_heap(ready.begin(), ready.end(), byName);

    std::vector<bool> placed(nodes.size(), false);

    const auto emit = [&](std::size_t index, bool inCycle) {
        TransactionStep step;

        step.component = nodes[index].component->id();
        step.inCycle = inCycle;

        for (std::size_t earlier : nodes[index].waitsFor) {
            step.after.push_back(nodes[earlier].component->id());

            if (step.after.size() >= 3) {
                break;
            }
        }

        std::sort(step.after.begin(), step.after.end());

        plan.steps.push_back(std::move(step));
        placed[index] = true;
    };

    while (!ready.empty()) {
        std::pop_heap(ready.begin(), ready.end(), byName);

        const std::size_t index = ready.back();

        ready.pop_back();

        emit(index, false);

        for (std::size_t waiting : nodes[index].blocks) {
            if (remaining[waiting] > 0) {
                remaining[waiting] -= 1;

                if (remaining[waiting] == 0) {
                    ready.push_back(waiting);
                    std::push_heap(ready.begin(), ready.end(), byName);
                }
            }
        }
    }

    if (plan.steps.size() == nodes.size()) {
        plan.status = TransactionStatus::Ready;
        plan.reason =
            "Every component can be applied after the ones it needs.";

        return plan;
    }

    // What is left is in, or downstream of, a cycle.
    //
    // Grouping by "everything transitively connected" is wrong: that
    // is weak connectivity, and it reports a blob rather than a loop.
    // A cycle is a strongly connected component -- a set where every
    // member can reach every other by following dependencies.
    //
    // Tarjan's algorithm, iteratively: the depth here is the size of
    // the transaction, which does not belong on a call stack.
    std::vector<std::size_t> index(nodes.size(), 0);
    std::vector<std::size_t> lowlink(nodes.size(), 0);
    std::vector<bool> visited(nodes.size(), false);
    std::vector<bool> onStack(nodes.size(), false);
    std::vector<std::size_t> tarjanStack;

    std::size_t counter = 1;

    // Strongly connected components, in the order Tarjan produces
    // them, which is reverse topological -- dependencies first.
    std::vector<std::vector<std::size_t>> components;

    struct Visit {
        std::size_t node;
        std::vector<std::size_t> neighbours;
        std::size_t next = 0;
    };

    for (std::size_t root = 0; root < nodes.size(); ++root) {
        if (placed[root] || visited[root]) {
            continue;
        }

        std::vector<Visit> stack;

        const auto open = [&](std::size_t node) {
            visited[node] = true;
            index[node] = counter;
            lowlink[node] = counter;
            counter += 1;

            tarjanStack.push_back(node);
            onStack[node] = true;

            Visit visit;

            visit.node = node;

            for (std::size_t neighbour : nodes[node].waitsFor) {
                if (!placed[neighbour]) {
                    visit.neighbours.push_back(neighbour);
                }
            }

            stack.push_back(std::move(visit));
        };

        open(root);

        while (!stack.empty()) {
            Visit& visit = stack.back();

            if (visit.next < visit.neighbours.size()) {
                const std::size_t neighbour =
                    visit.neighbours[visit.next];

                visit.next += 1;

                if (!visited[neighbour]) {
                    open(neighbour);
                } else if (onStack[neighbour]) {
                    lowlink[visit.node] = std::min(
                        lowlink[visit.node], index[neighbour]);
                }

                continue;
            }

            const std::size_t node = visit.node;

            stack.pop_back();

            if (!stack.empty()) {
                lowlink[stack.back().node] = std::min(
                    lowlink[stack.back().node], lowlink[node]);
            }

            if (lowlink[node] == index[node]) {
                std::vector<std::size_t> component;

                while (true) {
                    const std::size_t member = tarjanStack.back();

                    tarjanStack.pop_back();
                    onStack[member] = false;
                    component.push_back(member);

                    if (member == node) {
                        break;
                    }
                }

                components.push_back(std::move(component));
            }
        }
    }

    for (const std::vector<std::size_t>& component : components) {
        if (component.size() < 2) {
            // A single node left over is not itself a loop; it is
            // waiting on one.
            continue;
        }

        TransactionCycle cycle;

        for (std::size_t member : component) {
            cycle.members.push_back(nodes[member].component->id());

            if (nodes[member].hasPreDependency) {
                cycle.containsPreDependency = true;
            }
        }

        std::sort(cycle.members.begin(), cycle.members.end());

        plan.cycles.push_back(std::move(cycle));
    }

    bool breakable = true;

    for (const TransactionCycle& cycle : plan.cycles) {
        if (cycle.containsPreDependency) {
            breakable = false;
            break;
        }
    }

    if (!breakable) {
        plan.status = TransactionStatus::Blocked;
        plan.reason =
            "A dependency cycle contains a pre-dependency, which "
            "cannot be broken: a pre-dependency must be configured "
            "before its dependent is unpacked.";

        return plan;
    }

    // Break the remaining cycles the way dpkg does: unpack everything
    // in the loop, then configure it. The members are emitted in a
    // deterministic order and marked, so the report never pretends
    // the order was derived.
    // Tarjan produced the components in reverse topological order,
    // so emitting them in that order still puts dependencies first
    // wherever the graph allows it.
    for (const std::vector<std::size_t>& component : components) {
        std::vector<std::size_t> members = component;

        std::sort(
            members.begin(),
            members.end(),
            [&nodes](std::size_t left, std::size_t right) {
                return nodes[left].component->id() <
                       nodes[right].component->id();
            }
        );

        for (std::size_t member : members) {
            if (!placed[member]) {
                emit(member, component.size() > 1);
            }
        }
    }

    plan.status = TransactionStatus::Ready;
    plan.reason =
        "Ordered, with " + std::to_string(plan.cycles.size()) +
        " dependency cycle(s) broken by unpacking their members "
        "before configuring them.";

    return plan;
}

std::string toString(TransactionStatus status) {
    switch (status) {
        case TransactionStatus::Ready:
            return "ready";
        case TransactionStatus::Blocked:
            return "blocked";
    }

    return "unknown";
}

}
