#include <nexus/profile_check.hpp>

#include <set>

namespace nexus {

ProfileReport checkProfile(
    const Profile& profile,
    const Solver& solver
) {
    ProfileReport report;

    report.profile = profile.name;
    report.description = profile.description;

    // Work out which conditional preferences apply here, before
    // resolving anything. A condition is just a capability, so it is
    // checked the same way every other capability is.
    std::set<std::string> preferredComponents;

    for (const ConditionalPreference& conditional :
         profile.conditionalPreferences) {

        SolverRequest probe;

        probe.architecture = profile.architecture;
        probe.requirements.push_back(
            Requirement(Constraint(conditional.when))
        );

        const bool holds =
            solver.solve(probe).status == SolverStatus::Success;

        const std::string description =
            conditional.when + " -> prefer " + conditional.prefer;

        if (holds) {
            preferredComponents.insert(conditional.prefer);
            report.appliedPreferences.push_back(description);
        } else {
            report.inactivePreferences.push_back(description);
        }
    }

    for (const Requirement& requirement : profile.requirements) {
        SolverRequest request;

        request.architecture = profile.architecture;
        request.requirements.push_back(requirement);
        request.preferred = profile.preferred;
        request.required = profile.required;
        request.preferredComponents = preferredComponents;

        const SolverResult result = solver.solve(request);

        ProfileItem item;

        item.requirement = toString(requirement);
        item.satisfied = result.status == SolverStatus::Success;

        if (item.satisfied) {
            item.provided = result.selected;
            report.satisfied += 1;
        } else {
            item.blockedOn = result.blockedOn.empty()
                ? result.reason
                : result.blockedOn;

            report.missing += 1;
        }

        report.items.push_back(std::move(item));
    }

    return report;
}

}
