#include <nexus/conflict_detector.hpp>

#include <utility>

namespace nexus {

namespace {

bool relationHolds(VersionRelation relation, int comparison) {
    switch (relation) {
        case VersionRelation::Earlier:
            return comparison < 0;
        case VersionRelation::EarlierOrEqual:
            return comparison <= 0;
        case VersionRelation::Exactly:
            return comparison == 0;
        case VersionRelation::LaterOrEqual:
            return comparison >= 0;
        case VersionRelation::Later:
            return comparison > 0;
    }

    return false;
}

// The version at which a component provides a capability.
//
// A virtual capability may be provided at a version unrelated to the
// component's own: perl (5.38.2) provides libnet-perl (= 3.15). When
// the provider states a version, that is the one a constraint must be
// checked against.
//
// Returns nullptr when the component does not provide the capability
// at all.
const std::string* providedVersion(
    const Component& component,
    const std::string& name
) {
    for (const Capability& capability :
         component.providedCapabilities()) {

        if (capability.name() != name) {
            continue;
        }

        if (capability.hasVersion()) {
            return &capability.version();
        }

        // Provided, but with no version stated: fall back to the
        // component's own version.
        return &component.version();
    }

    if (component.id() == name) {
        return &component.version();
    }

    return nullptr;
}

}

ConflictDetector::ConflictDetector(VersionComparator comparator)
    : comparator_(std::move(comparator)) {
}

bool ConflictDetector::hasComparator() const {
    return static_cast<bool>(comparator_);
}

bool ConflictDetector::matches(
    const Component& component,
    const Constraint& constraint,
    const std::string& requesterArchitecture
) const {
    const std::string* version =
        providedVersion(component, constraint.capability);

    if (version == nullptr) {
        return false;
    }

    if (!architectureSatisfies(
            component.architecture(),
            component.multiArch(),
            constraint.architecture,
            requesterArchitecture)) {
        return false;
    }

    if (constraint.isUnversioned()) {
        return true;
    }

    if (!comparator_) {
        // No way to compare versions, so assume the condition holds.
        // Over-reporting a conflict is safe; missing one is not.
        return true;
    }

    const int comparison =
        comparator_(*version, constraint.version->version);

    return relationHolds(constraint.version->relation, comparison);
}

std::vector<Conflict> ConflictDetector::detect(
    const std::vector<Component>& components
) const {
    std::vector<Conflict> conflicts;

    for (const Component& declaring : components) {
        for (const Constraint& constraint : declaring.conflicts()) {
            for (const Component& other : components) {
                if (other.id() == declaring.id()) {
                    continue;
                }

                if (!matches(other, constraint)) {
                    continue;
                }

                conflicts.push_back(Conflict{
                    declaring.id(),
                    other.id(),
                    constraint,
                    declaring.id() + " declares a conflict with " +
                        toString(constraint) +
                        ", which " + other.id() +
                        " (" + other.version() + ") satisfies."
                });
            }
        }
    }

    return conflicts;
}

std::vector<Conflict> ConflictDetector::check(
    const Component& candidate,
    const std::vector<Component>& installed
) const {
    std::vector<Conflict> conflicts;

    // The candidate conflicting with something already installed.
    for (const Constraint& constraint : candidate.conflicts()) {
        for (const Component& other : installed) {
            if (other.id() == candidate.id()) {
                continue;
            }

            if (!matches(other, constraint)) {
                continue;
            }

            conflicts.push_back(Conflict{
                candidate.id(),
                other.id(),
                constraint,
                candidate.id() + " conflicts with " +
                    toString(constraint) + ", already installed as " +
                    other.id() + " (" + other.version() + ")."
            });
        }
    }

    // Something already installed conflicting with the candidate.
    for (const Component& other : installed) {
        if (other.id() == candidate.id()) {
            continue;
        }

        for (const Constraint& constraint : other.conflicts()) {
            if (!matches(candidate, constraint)) {
                continue;
            }

            conflicts.push_back(Conflict{
                other.id(),
                candidate.id(),
                constraint,
                "Installed component " + other.id() +
                    " conflicts with " + toString(constraint) +
                    ", which " + candidate.id() +
                    " (" + candidate.version() + ") satisfies."
            });
        }
    }

    return conflicts;
}

}
