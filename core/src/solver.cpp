#include <nexus/solver.hpp>

#include <algorithm>
#include <functional>
#include <utility>
#include <set>
#include <utility>

namespace nexus {

namespace {

struct SearchState {
    std::vector<const Component*> selected;
    std::set<std::string> selectedIds;
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
        [&state]() -> std::vector<Component> {
            std::vector<Component> result;
            result.reserve(state.selected.size());

            for (const Component* component : state.selected) {
                result.push_back(*component);
            }

            return result;
        };

    // Is this requirement already met by something we have chosen?
    const auto alreadySatisfied =
        [&](const Requirement& requirement,
            const std::string& arch) -> bool {
            for (const Constraint& option : requirement.alternatives) {
                for (const Component* chosen : state.selected) {
                    if (detector_.matches(*chosen, option, arch)) {
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

                for (const Component& component : components_) {
                    if (!detector_.matches(
                            component,
                            option,
                            requesterArchitecture)) {
                        continue;
                    }

                    candidates.push_back(Candidate{
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
                        leftPreferred != request.preferred.end() &&
                        leftPreferred->second == left.component->id();

                    const bool rightWins =
                        rightPreferred != request.preferred.end() &&
                        rightPreferred->second == right.component->id();

                    if (leftWins != rightWins) {
                        return leftWins;
                    }

                    return left.rank < right.rank;
                }
            );

            bool firstAttempt = true;

            for (const Candidate& candidate : candidates) {
                if (state.decisions >= decisionLimit_) {
                    limitHit = true;
                    return false;
                }

                state.decisions += 1;

                if (state.selectedIds.count(candidate.component->id())) {
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

                state.selected.push_back(candidate.component);
                state.selectedIds.insert(candidate.component->id());

                std::string reason;

                if (candidates.size() == 1) {
                    reason = "only component providing " +
                             toString(*candidate.option);
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

                for (const Requirement& theirs :
                     candidate.component->requirements()) {

                    next.push_back(Pending{
                        theirs,
                        candidate.component->architecture()
                    });
                }

                if (search(next)) {
                    return true;
                }

                // Undo and try the next alternative.
                state.selected.pop_back();
                state.selectedIds.erase(candidate.component->id());
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

    for (const Component* component : state.selected) {
        result.selected.push_back(component->id());
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
