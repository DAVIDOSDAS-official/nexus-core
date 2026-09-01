#include <nexus/diagnosis.hpp>

#include <algorithm>
#include <map>

#include <nexus/removal.hpp>

namespace nexus {

namespace {

std::map<std::string, std::vector<std::size_t>> buildIndex(
    const std::vector<Component>& components
) {
    std::map<std::string, std::vector<std::size_t>> index;

    for (std::size_t position = 0;
         position < components.size();
         ++position) {

        const Component& component = components[position];

        const auto add = [&](const std::string& key) {
            std::vector<std::size_t>& entries = index[key];

            if (entries.empty() || entries.back() != position) {
                entries.push_back(position);
            }
        };

        add(component.id());
        add(component.name());

        for (const Capability& capability :
             component.providedCapabilities()) {

            add(capability.name());
        }
    }

    return index;
}

void keepExamples(Finding& finding, const std::string& name) {
    finding.total += 1;

    if (finding.examples.size() < 5) {
        finding.examples.push_back(name);
    }
}

}

std::string toString(Health health) {
    switch (health) {
        case Health::Ok:
            return "ok";
        case Health::Warning:
            return "warning";
        case Health::Problem:
            return "problem";
        case Health::Unknown:
            return "unknown";
    }

    return "unknown";
}

Health Diagnosis::overall() const {
    Health worst = Health::Ok;

    for (const Finding& finding : findings) {
        if (finding.health == Health::Problem) {
            return Health::Problem;
        }

        if (finding.health == Health::Warning) {
            worst = Health::Warning;
        }
    }

    return worst;
}

Diagnosis diagnose(
    const std::vector<Component>& installed,
    const std::set<std::string>& roots,
    const ConflictDetector& detector
) {
    Diagnosis diagnosis;

    const auto index = buildIndex(installed);

    // Requirements nothing installed can satisfy. The condition
    // everybody means by "broken packages", and the one a package
    // manager usually reports only when you try to change something.
    {
        Finding finding;

        finding.check = "Dependencies";

        for (const Component& component : installed) {
            for (const Requirement& requirement :
                 component.requirements()) {

                const bool satisfied = std::any_of(
                    requirement.alternatives.begin(),
                    requirement.alternatives.end(),
                    [&](const Constraint& option) {
                        const auto entry =
                            index.find(option.capability);

                        if (entry == index.end()) {
                            return false;
                        }

                        return std::any_of(
                            entry->second.begin(),
                            entry->second.end(),
                            [&](std::size_t position) {
                                return detector.matches(
                                    installed[position], option);
                            }
                        );
                    }
                );

                if (!satisfied) {
                    keepExamples(
                        finding,
                        component.id() + " needs " +
                            toString(requirement)
                    );
                }
            }
        }

        if (finding.total == 0) {
            finding.health = Health::Ok;
            finding.detail = "Every requirement is satisfied.";
        } else {
            finding.health = Health::Problem;
            finding.detail =
                std::to_string(finding.total) +
                " requirement(s) nothing installed satisfies.";
            finding.suggestion = "nexus solve <capability>";
        }

        diagnosis.findings.push_back(std::move(finding));
    }

    // Conflicts that are live rather than merely declared.
    {
        Finding finding;

        finding.check = "Conflicts";

        // One collision, not one per direction: A conflicts with B
        // and B conflicts with A are the same fact reported twice.
        std::set<std::string> seen;

        for (const Conflict& conflict : detector.detect(installed)) {
            std::string first = conflict.componentId;
            std::string second = conflict.conflictsWith;

            if (second < first) {
                std::swap(first, second);
            }

            if (!seen.insert(first + " vs " + second).second) {
                continue;
            }

            keepExamples(finding, first + " vs " + second);
        }

        if (finding.total == 0) {
            finding.health = Health::Ok;
            finding.detail =
                "No installed component collides with another.";
        } else {
            finding.health = Health::Problem;
            finding.detail =
                std::to_string(finding.total) +
                " active conflict(s).";
            finding.suggestion = "nexus conflicts";
        }

        diagnosis.findings.push_back(std::move(finding));
    }

    // Installed, but nothing asked for it and nothing needs it.
    {
        Finding finding;

        finding.check = "Unused";

        if (roots.empty()) {
            finding.health = Health::Unknown;
            finding.detail =
                "No record of what was explicitly wanted, so nothing "
                "can be called unused.";
        } else {
            const std::set<std::string> reachable =
                reachableFrom(installed, roots, detector);

            for (const Component& component : installed) {
                if (reachable.count(component.id()) == 0) {
                    keepExamples(finding, component.id());
                }
            }

            if (finding.total == 0) {
                finding.health = Health::Ok;
                finding.detail =
                    "Everything installed is held up by something.";
            } else {
                // Not a problem. Unused is untidy, not broken, and
                // calling it a problem trains people to ignore the
                // word.
                finding.health = Health::Warning;
                finding.detail =
                    std::to_string(finding.total) +
                    " component(s) nothing needs.";
                finding.suggestion = "nexus remove <component>";
            }
        }

        diagnosis.findings.push_back(std::move(finding));
    }

    return diagnosis;
}

}
