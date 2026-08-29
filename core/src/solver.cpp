#include <nexus/solver.hpp>

#include <algorithm>
#include <functional>
#include <utility>
#include <map>
#include <set>
#include <utility>

namespace nexus {

namespace {

struct SearchState {
    // Tracked by index rather than id. Identity is the source's
    // business; the solver must not silently merge two distinct
    // components that happen to share a name.
    std::vector<std::size_t> selected;
    std::set<std::size_t> selectedIndices;
    std::vector<SolverStep> steps;
    std::size_t decisions = 0;
    std::size_t backtracks = 0;
};

}

Solver::Solver(
    std::vector<Component> components,
    ConflictDetector detector
)
    : components_(std::move(components)),
      detector_(std::move(detector)) {
    buildIndex();
}

void Solver::buildIndex() {
    for (std::size_t index = 0; index < components_.size(); ++index) {
        const Component& component = components_[index];

        // A component is reachable by its id, its name, and every
        // capability it provides -- and those overlap, since a
        // package normally provides its own name. Inserting the same
        // index twice under one key would make a single provider look
        // like a choice between two.
        const auto add = [&](const std::string& key) {
            std::vector<std::size_t>& entries = byCapability_[key];

            if (!entries.empty() && entries.back() == index) {
                return;
            }

            entries.push_back(index);
        };

        add(component.id());
        add(component.name());

        for (const Capability& capability :
             component.providedCapabilities()) {

            add(capability.name());
        }
    }
}

std::size_t Solver::componentCount() const {
    return components_.size();
}

void Solver::setDecisionLimit(std::size_t limit) {
    decisionLimit_ = limit;
}

SolverResult Solver::solve(const SolverRequest& request) const {
    SearchState state;

    std::string blockedOn;
    bool limitHit = false;

    // Components already selected, as values, for conflict checking.
    const auto selectedComponents =
        [&state, this]() -> std::vector<Component> {
            std::vector<Component> result;
            result.reserve(state.selected.size());

            for (std::size_t position : state.selected) {
                result.push_back(components_[position]);
            }

            return result;
        };

    // Is this requirement already met by something we have chosen?
    const auto alreadySatisfied =
        [&](const Requirement& requirement,
            const std::string& arch) -> bool {
            for (const Constraint& option : requirement.alternatives) {
                for (std::size_t position : state.selected) {
                    if (detector_.matches(
                            components_[position], option, arch)) {
                        return true;
                    }
                }
            }

            return false;
        };

    // Recursive backtracking search over a work list of requirements.
    // Each pending item carries the architecture of the component
    // that asked for it, because "libfoo" from an amd64 package means
    // libfoo:amd64 unless the provider is marked foreign.
    using Pending = std::pair<Requirement, std::string>;

    std::function<bool(std::vector<Pending>)> search =
        [&](std::vector<Pending> pending) -> bool {
            if (pending.empty()) {
                return true;
            }

            const Requirement requirement = pending.front().first;
            const std::string requesterArchitecture =
                pending.front().second;

            pending.erase(pending.begin());

            if (requirement.empty() ||
                alreadySatisfied(requirement, requesterArchitecture)) {
                return search(pending);
            }

            // Gather every component that satisfies any alternative,
            // keeping the declared order of the alternatives.
            struct Candidate {
                std::size_t position;
                const Component* component;
                const Constraint* option;
                std::size_t rank;
            };

            std::vector<Candidate> candidates;

            for (std::size_t index = 0;
                 index < requirement.alternatives.size();
                 ++index) {

                const Constraint& option =
                    requirement.alternatives[index];

                const auto entry =
                    byCapability_.find(option.capability);

                if (entry == byCapability_.end()) {
                    continue;
                }

                for (std::size_t position : entry->second) {
                    const Component& component = components_[position];

                    if (!detector_.matches(
                            component,
                            option,
                            requesterArchitecture)) {
                        continue;
                    }

                    candidates.push_back(Candidate{
                        position,
                        &component,
                        &option,
                        index
                    });
                }
            }

            if (candidates.empty()) {
                if (blockedOn.empty()) {
                    blockedOn = toString(requirement);
                }

                return false;
            }

            // A hard requirement removes every other candidate; a
            // preference only reorders them. This is what keeps an
            // explicit choice from being silently replaced.
            for (const Candidate& candidate : candidates) {
                const auto pinned =
                    request.required.find(candidate.option->capability);

                if (pinned == request.required.end()) {
                    continue;
                }

                std::vector<Candidate> filtered;

                for (const Candidate& other : candidates) {
                    if (other.component->id() == pinned->second) {
                        filtered.push_back(other);
                    }
                }

                if (filtered.empty()) {
                    blockedOn = pinned->second + " (required for " +
                                candidate.option->capability + ")";
                    return false;
                }

                candidates = filtered;
                break;
            }

            std::stable_sort(
                candidates.begin(),
                candidates.end(),
                [&](const Candidate& left, const Candidate& right) {
                    const auto leftPreferred =
                        request.preferred.find(left.option->capability);
                    const auto rightPreferred =
                        request.preferred.find(right.option->capability);

                    const bool leftWins =
                        (leftPreferred != request.preferred.end() &&
                         leftPreferred->second ==
                             left.component->id()) ||
                        request.preferredComponents.count(
                            left.component->id()) > 0;

                    const bool rightWins =
                        (rightPreferred != request.preferred.end() &&
                         rightPreferred->second ==
                             right.component->id()) ||
                        request.preferredComponents.count(
                            right.component->id()) > 0;

                    if (leftWins != rightWins) {
                        return leftWins;
                    }

                    // The order of alternatives is the packager's
                    // stated preference and outranks everything below.
                    // Architecture only breaks ties *within* one
                    // alternative -- otherwise a native-architecture
                    // provider of the fifth alternative would beat an
                    // arch-independent provider of the first.
                    if (left.rank != right.rank) {
                        return left.rank < right.rank;
                    }

                    // Within a single alternative, several builds of
                    // one package can be eligible: a foreign-marked
                    // component satisfies any architecture. Prefer the
                    // requester's own, rather than whichever the index
                    // listed first. Arch-independent components count
                    // as native; they are not a worse answer.
                    if (!requesterArchitecture.empty()) {
                        // Three tiers, most specific first:
                        //   0  built for exactly this architecture
                        //   1  architecture independent
                        //   2  another architecture, eligible only
                        //      because it is marked foreign
                        const auto tier =
                            [&](const Component& component) {
                                if (component.architecture() ==
                                    requesterArchitecture) {
                                    return 0;
                                }

                                if (component.architecture() ==
                                    kArchitectureAll) {
                                    return 1;
                                }

                                return 2;
                            };

                        const int leftTier = tier(*left.component);
                        const int rightTier = tier(*right.component);

                        if (leftTier != rightTier) {
                            return leftTier < rightTier;
                        }
                    }

                    return false;
                }
            );

            bool firstAttempt = true;

            for (const Candidate& candidate : candidates) {
                if (state.decisions >= decisionLimit_) {
                    limitHit = true;
                    return false;
                }

                state.decisions += 1;

                if (state.selectedIndices.count(candidate.position)) {
                    return search(pending);
                }

                // Never select something that collides with what is
                // already chosen, in either direction.
                const auto conflicts = detector_.check(
                    *candidate.component,
                    selectedComponents()
                );

                if (!conflicts.empty()) {
                    if (blockedOn.empty()) {
                        blockedOn = conflicts.front().reason;
                    }

                    continue;
                }

                state.selected.push_back(candidate.position);
                state.selectedIndices.insert(candidate.position);

                // Everything else that could have satisfied this
                // requirement, so the reason can say what was passed
                // over instead of silently claiming there was no
                // choice.
                std::vector<std::string> rejected;

                for (const Candidate& other : candidates) {
                    if (other.position == candidate.position) {
                        continue;
                    }

                    rejected.push_back(other.component->id());

                    if (rejected.size() >= 3) {
                        break;
                    }
                }

                const auto listRejected = [&rejected]() {
                    std::string text;

                    for (std::size_t i = 0; i < rejected.size(); ++i) {
                        if (i > 0) {
                            text += ", ";
                        }

                        text += rejected[i];
                    }

                    return text;
                };

                std::string reason;

                if (candidates.size() == 1) {
                    reason = "only component providing " +
                             toString(*candidate.option);
                } else if (!requirement.hasChoice()) {
                    // One alternative, several components able to
                    // satisfy it -- usually different architectures
                    // of the same package.
                    reason = "chosen over " + listRejected() +
                             " for " + toString(*candidate.option);

                    if (!requesterArchitecture.empty() &&
                        candidate.component->architecture() ==
                            requesterArchitecture) {
                        reason += " (matches " +
                                  requesterArchitecture + ")";
                    }
                } else if (!firstAttempt) {
                    reason = "chosen after earlier alternatives for " +
                             toString(requirement) + " failed";
                } else if (requirement.hasChoice()) {
                    reason = "first workable alternative for " +
                             toString(requirement);
                } else {
                    reason = "provides " + toString(*candidate.option);
                }

                const auto pinned =
                    request.required.find(candidate.option->capability);

                if (pinned != request.required.end()) {
                    reason = "explicitly required for " +
                             candidate.option->capability;
                }

                const auto liked =
                    request.preferred.find(candidate.option->capability);

                if (liked != request.preferred.end() &&
                    liked->second == candidate.component->id()) {

                    reason = "preferred provider for " +
                             candidate.option->capability;
                } else if (request.preferredComponents.count(
                               candidate.component->id()) > 0 &&
                           candidates.size() > 1) {

                    reason = "preferred on this machine";
                }

                state.steps.push_back(SolverStep{
                    toString(requirement),
                    candidate.component->id(),
                    reason,
                    candidates.size(),
                    !firstAttempt
                });

                // The chosen component brings its own requirements.
                std::vector<Pending> next = pending;

                // An architecture-independent component still runs on
                // a concrete architecture. Its own dependencies must
                // be resolved against the architecture that asked for
                // it, not against "all" -- "all" says what it
                // satisfies, not what it needs.
                const std::string childArchitecture =
                    (candidate.component->architecture() ==
                     kArchitectureAll)
                        ? requesterArchitecture
                        : candidate.component->architecture();

                for (const Requirement& theirs :
                     candidate.component->requirements()) {

                    next.push_back(Pending{theirs, childArchitecture});
                }

                if (search(next)) {
                    return true;
                }

                // Undo and try the next alternative.
                state.selected.pop_back();
                state.selectedIndices.erase(candidate.position);
                state.steps.pop_back();
                state.backtracks += 1;

                firstAttempt = false;
            }

            if (blockedOn.empty()) {
                blockedOn = toString(requirement);
            }

            return false;
        };

    std::vector<Pending> initial;

    for (const Requirement& requirement : request.requirements) {
        initial.push_back(Pending{requirement, request.architecture});
    }

    const bool solved = search(initial);

    SolverResult result;

    result.steps = state.steps;
    result.decisions = state.decisions;
    result.backtracks = state.backtracks;

    for (std::size_t position : state.selected) {
        result.selected.push_back(components_[position].id());
    }

    if (solved) {
        result.status = SolverStatus::Success;
        result.reason =
            "All requirements satisfied without conflicts.";
        return result;
    }

    if (limitHit) {
        result.status = SolverStatus::LimitExceeded;
        result.reason =
            "Search exceeded the decision limit before finding a "
            "solution. No conclusion can be drawn.";
        return result;
    }

    result.status = SolverStatus::Unsatisfiable;
    result.blockedOn = blockedOn;
    result.reason =
        "No combination of components satisfies every requirement.";

    return result;
}

std::string toString(SolverStatus status) {
    switch (status) {
        case SolverStatus::Success:
            return "success";
        case SolverStatus::Unsatisfiable:
            return "unsatisfiable";
        case SolverStatus::LimitExceeded:
            return "limit-exceeded";
    }

    return "unknown";
}

}
