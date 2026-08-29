#include <nexus/profile_check.hpp>

namespace nexus {

ProfileReport checkProfile(
    const Profile& profile,
    const Solver& solver
) {
    ProfileReport report;

    report.profile = profile.name;
    report.description = profile.description;

    for (const Requirement& requirement : profile.requirements) {
        SolverRequest request;

        request.architecture = profile.architecture;
        request.requirements.push_back(requirement);
        request.preferred = profile.preferred;
        request.required = profile.required;

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
