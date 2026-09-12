#include <nexus/composition.hpp>

#include <algorithm>
#include <map>
#include <set>

namespace nexus {

Composition compose(
    const std::vector<Profile>& profiles,
    const std::string& name
) {
    Composition composition;

    composition.profile.name = name;

    // A profile that describes the whole machine cannot be one
    // ingredient among several.
    // base is the floor, not a peer.
    //
    // Two whole-machine profiles cannot be combined because they are
    // rival answers to "what is this computer". base is not a rival:
    // it is what every answer stands on. Counting it as one made
    // base,minimal a refusal, and the image that came out had no
    // init, no libc and nothing that could reach a network.
    std::size_t peers = 0;

    for (const Profile& profile : profiles) {
        if (profile.name != "base") {
            peers += 1;
        }
    }

    if (peers > 1) {
        for (const Profile& profile : profiles) {
            if (!profile.exclusive) {
                continue;
            }

            composition.refused = true;
            composition.refusal =
                profile.name + " describes a whole machine rather "
                "than a set of things to add, so it cannot be "
                "combined.";

            if (!profile.insteadUse.empty()) {
                std::string others;

                for (const Profile& other : profiles) {
                    if (other.name == profile.name) {
                        continue;
                    }

                    if (!others.empty()) {
                        others += ",";
                    }

                    others += other.name;
                }

                composition.refusal +=
                    " Use " + profile.insteadUse;

                if (!others.empty()) {
                    composition.refusal += "," + others;
                }

                composition.refusal += " instead.";
            }

            return composition;
        }
    }

    std::vector<std::string> descriptions;

    // Written form -> which profile asked for it first.
    std::map<std::string, std::string> seen;

    // Capability -> the profile whose preference won.
    std::map<std::string, std::string> preferenceOwner;
    std::map<std::string, std::string> requirementOwner;

    for (const Profile& profile : profiles) {
        composition.sources.push_back(profile.name);

        if (!profile.description.empty()) {
            descriptions.push_back(profile.description);
        }

        if (composition.profile.architecture.empty()) {
            composition.profile.architecture = profile.architecture;
        }

        for (const std::string& extra :
             profile.additionalArchitectures) {

            const auto& present =
                composition.profile.additionalArchitectures;

            if (std::find(present.begin(), present.end(), extra) ==
                present.end()) {

                composition.profile.additionalArchitectures
                    .push_back(extra);
            }
        }

        for (const Requirement& requirement : profile.requirements) {
            const std::string written = toString(requirement);

            const auto existing = seen.find(written);

            if (existing != seen.end()) {
                // Asked for twice. Worth reporting: it is why the
                // combined total is smaller than the sum.
                composition.shared.push_back(written);
                continue;
            }

            seen[written] = profile.name;
            requirementOwner[written] = profile.name;

            composition.profile.requirements.push_back(requirement);
        }

        for (const auto& [capability, component] :
             profile.preferred) {

            const auto existing =
                composition.profile.preferred.find(capability);

            if (existing == composition.profile.preferred.end()) {
                composition.profile.preferred[capability] = component;
                preferenceOwner[capability] = profile.name;
                continue;
            }

            if (existing->second == component) {
                continue;
            }

            // First profile named wins, and the clash is reported
            // rather than resolved silently.
            composition.clashes.push_back(PreferenceClash{
                capability,
                existing->second,
                component,
                preferenceOwner[capability],
                profile.name
            });
        }

        for (const auto& [capability, component] : profile.required) {
            composition.profile.required[capability] = component;
        }

        for (const ConditionalPreference& conditional :
             profile.conditionalPreferences) {

            const auto& present =
                composition.profile.conditionalPreferences;

            const bool already = std::any_of(
                present.begin(),
                present.end(),
                [&conditional](const ConditionalPreference& other) {
                    return other.when == conditional.when &&
                           other.prefer == conditional.prefer;
                }
            );

            if (!already) {
                composition.profile.conditionalPreferences
                    .push_back(conditional);
            }
        }
    }

    for (std::size_t index = 0;
         index < descriptions.size();
         ++index) {

        if (index > 0) {
            composition.profile.description += "; ";
        }

        composition.profile.description += descriptions[index];
    }

    return composition;
}

}
