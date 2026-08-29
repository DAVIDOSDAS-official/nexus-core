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
    // The pending work list lives here, not in the recursion.
    //
    // It used to be passed by value, so every recursive call copied a
    // vector that can hold thousands of entries. On a large system
    // that is megabytes per stack frame, and the search overflows the
    // stack long before it runs out of options.
    std::vector<std::pair<Requirement, std::string>> queue;

    // Tracked by index rather than id. Identity is the source's
    // business; the solver must not silently merge two distinct
    // components that happen to share a name.
    std::vector<std::size_t> selected;
    std::set<std::size_t> selectedIndices;

    // Indexes over what is currently selected, so that satisfaction
    // and conflict checks cost the size of the answer rather than the
    // size of the selection.
    //
    // Without these, every candidate evaluation walked every selected
    // component -- and the conflict check copied them all. On a
    // system with thousands of packages that is quadratic in the
    // worst place possible.
    std::map<std::string, std::vector<std::size_t>> provided;
    std::map<
        std::string,
        std::vector<std::pair<std::size_t, const Constraint*>>
    > declaredConflicts;
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

    // Every name a component answers to.
    const auto namesOf =
        [this](std::size_t position) -> std::vector<std::string> {
            const Component& component = components_[position];

            std::vector<std::string> names;

            names.push_back(component.id());

            if (component.name() != component.id()) {
                names.push_back(component.name());
            }

            for (const Capability& capability :
                 component.providedCapabilities()) {

                names.push_back(capability.name());
            }

            return names;
        };

    const auto indexSelection = [&](std::size_t position) {
        for (const std::string& name : namesOf(position)) {
            state.provided[name].push_back(position);
        }

        for (const Constraint& conflict :
             components_[position].conflicts()) {

            state.declaredConflicts[conflict.capability].push_back(
                {position, &conflict}
            );
        }
    };

    const auto unindexSelection = [&](std::size_t position) {
        for (const std::string& name : namesOf(position)) {
            std::vector<std::size_t>& entries = state.provided[name];

            entries.erase(
                std::remove(entries.begin(), entries.end(), position),
                entries.end()
            );
        }

        for (const Constraint& conflict :
             components_[position].conflicts()) {

            auto& entries = state.declaredConflicts[conflict.capability];

            entries.erase(
                std::remove_if(
                    entries.begin(),
                    entries.end(),
                    [position](const auto& entry) {
                        return entry.first == position;
                    }
                ),
                entries.end()
            );
        }
    };

    // Is this requirement already met by something we have chosen?
    const auto alreadySatisfied =
        [&](const Requirement& requirement,
            const std::string& arch) -> bool {
            for (const Constraint& option : requirement.alternatives) {
                const auto entry =
                    state.provided.find(option.capability);

                if (entry == state.provided.end()) {
                    continue;
                }

                for (std::size_t position : entry->second) {
                    if (detector_.matches(
                            components_[position], option, arch)) {
                        return true;
                    }
                }
            }

            return false;
        };

    // Would choosing this collide with anything already chosen, in
    // either direction? Looked up by name rather than scanned.
    const auto collidesWithSelection =
        [&](const Component& candidate) -> std::string {
            for (const Constraint& conflict : candidate.conflicts()) {
                const auto entry =
                    state.provided.find(conflict.capability);

                if (entry == state.provided.end()) {
                    continue;
                }

                for (std::size_t position : entry->second) {
                    if (detector_.matches(
                            components_[position], conflict)) {

                        return candidate.id() + " conflicts with " +
                               toString(conflict) + ", satisfied by " +
                               components_[position].id() + ".";
                    }
                }
            }

            std::vector<std::string> candidateNames;

            candidateNames.push_back(candidate.id());
            candidateNames.push_back(candidate.name());

            for (const Capability& capability :
                 candidate.providedCapabilities()) {

                candidateNames.push_back(capability.name());
            }

            for (const std::string& name : candidateNames) {
                const auto entry = state.declaredConflicts.find(name);

                if (entry == state.declaredConflicts.end()) {
                    continue;
                }

                for (const auto& [position, conflict] : entry->second) {
                    if (detector_.matches(candidate, *conflict)) {
                        return components_[position].id() +
                               " conflicts with " +
                               toString(*conflict) + ", satisfied by " +
                               candidate.id() + ".";
                    }
                }
            }

            return "";
        };

    // Recursive backtracking search over a work list of requirements.
    // Each pending item carries the architecture of the component
    // that asked for it, because "libfoo" from an amd64 package means
    // libfoo:amd64 unless the provider is marked foreign.
    using Pending = std::pair<Requirement, std::string>;

    // The recursion carries only a position in the shared queue.
    struct Candidate {
        std::size_t position;
        const Component* component;
        const Constraint* option;
        std::size_t rank;
    };

    // Build and order every component that could satisfy one
    // requirement. Leaves candidates empty, and sets blockedOn, when
    // nothing can.
    const auto gather =
        [&](const Requirement& requirement,
            const std::string& requesterArchitecture,
            std::vector<Candidate>& candidates) {

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

                return;
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
                    if (left.rank != right.rank) {
                        return left.rank < right.rank;
                    }

                    // Within one alternative, prefer the build for
                    // this architecture, then the architecture
                    // independent one, then a foreign build.
                    if (!requesterArchitecture.empty()) {
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
        };

    // One open decision.
    //
    // These live on the heap, not on the call stack. The depth of the
    // search is the size of the resolved system -- thousands of
    // components on a real machine -- and a recursive search of that
    // depth overflows the stack. It did: a 3,000-package system
    // crashed while every test on a 900-package one passed.
    struct Frame {
        std::size_t head = 0;
        Requirement requirement;
        std::string architecture;
        std::vector<Candidate> candidates;
        std::size_t next = 0;
        bool firstAttempt = true;
        bool holdsSelection = false;
        std::size_t selectedPosition = 0;
        std::size_t queueMark = 0;
    };

    std::vector<Frame> frames;

    bool solved = false;

    // Open a decision at the next queue position that needs one,
    // skipping requirements already satisfied. Sets solved when the
    // queue runs out.
    const auto openFrame = [&](std::size_t head) -> bool {
        while (head < state.queue.size()) {
            const Requirement requirement = state.queue[head].first;
            const std::string architecture = state.queue[head].second;

            if (requirement.empty() ||
                alreadySatisfied(requirement, architecture)) {
                head += 1;
                continue;
            }

            Frame frame;

            frame.head = head;
            frame.requirement = requirement;
            frame.architecture = architecture;

            gather(frame.requirement, frame.architecture,
                   frame.candidates);

            frames.push_back(std::move(frame));

            return true;
        }

        solved = true;

        return false;
    };

    state.queue.reserve(request.requirements.size() * 4);

    for (const Requirement& requirement : request.requirements) {
        state.queue.push_back(
            Pending{requirement, request.architecture}
        );
    }

    openFrame(0);

    while (!solved && !frames.empty()) {
        // Undo whatever this frame currently holds before trying its
        // next option.
        {
            Frame& frame = frames.back();

            if (frame.holdsSelection) {
                state.queue.resize(frame.queueMark);
                unindexSelection(frame.selectedPosition);
                state.selected.pop_back();
                state.selectedIndices.erase(frame.selectedPosition);
                state.steps.pop_back();
                state.backtracks += 1;

                frame.holdsSelection = false;
                frame.firstAttempt = false;
            }
        }

        bool descended = false;

        while (true) {
            Frame& frame = frames.back();

            if (frame.next >= frame.candidates.size()) {
                break;
            }

            if (state.decisions >= decisionLimit_) {
                limitHit = true;
                frames.clear();
                break;
            }

            const Candidate candidate = frame.candidates[frame.next];

            frame.next += 1;
            state.decisions += 1;

            if (state.selectedIndices.count(candidate.position)) {
                // Already chosen for something else, so the
                // requirement is met without a new selection. This
                // frame has no further options if what follows fails.
                frame.next = frame.candidates.size();

                const std::size_t nextHead = frame.head + 1;

                descended = openFrame(nextHead);
                break;
            }

            const std::string collision =
                collidesWithSelection(*candidate.component);

            if (!collision.empty()) {
                if (blockedOn.empty()) {
                    blockedOn = collision;
                }

                continue;
            }

            state.selected.push_back(candidate.position);
            state.selectedIndices.insert(candidate.position);
            indexSelection(candidate.position);

            // Everything else that could have satisfied this
            // requirement, so the reason can say what was passed over
            // instead of silently claiming there was no choice.
            std::vector<std::string> rejected;

            for (const Candidate& other : frame.candidates) {
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

            if (frame.candidates.size() == 1) {
                reason = "only component providing " +
                         toString(*candidate.option);
            } else if (!frame.requirement.hasChoice()) {
                reason = "chosen over " + listRejected() +
                         " for " + toString(*candidate.option);

                if (!frame.architecture.empty() &&
                    candidate.component->architecture() ==
                        frame.architecture) {
                    reason += " (matches " + frame.architecture + ")";
                }
            } else if (!frame.firstAttempt) {
                reason = "chosen after earlier alternatives for " +
                         toString(frame.requirement) + " failed";
            } else {
                reason = "first workable alternative for " +
                         toString(frame.requirement);
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
                       frame.candidates.size() > 1) {

                reason = "preferred on this machine";
            }

            state.steps.push_back(SolverStep{
                toString(frame.requirement),
                candidate.component->id(),
                reason,
                frame.candidates.size(),
                !frame.firstAttempt
            });

            // The chosen component brings its own requirements,
            // appended after whatever is already waiting.
            frame.queueMark = state.queue.size();

            // An architecture-independent component still runs on a
            // concrete architecture. Its own dependencies resolve
            // against the architecture that asked for it.
            const std::string childArchitecture =
                (candidate.component->architecture() ==
                 kArchitectureAll)
                    ? frame.architecture
                    : candidate.component->architecture();

            for (const Requirement& theirs :
                 candidate.component->requirements()) {

                state.queue.push_back(
                    Pending{theirs, childArchitecture}
                );
            }

            frame.holdsSelection = true;
            frame.selectedPosition = candidate.position;

            const std::size_t nextHead = frame.head + 1;

            // Note: openFrame may reallocate frames, so nothing may
            // touch this frame afterwards.
            descended = openFrame(nextHead);
            break;
        }

        if (solved || limitHit) {
            break;
        }

        if (descended) {
            continue;
        }

        // Out of options here; fall back to whatever asked for it.
        if (blockedOn.empty()) {
            blockedOn = toString(frames.back().requirement);
        }

        frames.pop_back();
    }

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
