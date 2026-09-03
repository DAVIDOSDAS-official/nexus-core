#include <nexus/options.hpp>

#include <algorithm>
#include <map>
#include <set>

namespace nexus {

bool OptionsReport::anyInstalled() const {
    return std::any_of(
        options.begin(),
        options.end(),
        [](const Option& option) {
            return option.installed;
        }
    );
}

OptionsReport findOptions(
    const std::string& capability,
    const std::vector<Component>& universe,
    const std::vector<Component>& installed,
    const Solver& solver,
    const ConflictDetector& detector,
    const std::string& architecture,
    const AliasTable& aliases
) {
    OptionsReport report;

    report.capability = capability;

    // Matched on name and architecture, not on id.
    //
    // Ids are requalified when sources are merged, so the same
    // component can be "mawk" in the installed set and "mawk:amd64"
    // in the universe. Matching by id silently finds nothing, and the
    // output stays plausible while being wrong.
    const auto identityOf = [](const Component& component) {
        return component.name() + ":" + component.architecture();
    };

    std::set<std::string> installedIds;

    for (const Component& component : installed) {
        installedIds.insert(identityOf(component));
    }

    // An abstract capability has no provider of its own: nothing
    // ships a package called "window-manager". Without expansion the
    // command that exists to show choices finds none.
    const Requirement wanted =
        aliases.expand(Requirement(Constraint(capability)));

    for (const Component& component : universe) {
        const bool fits = std::any_of(
            wanted.alternatives.begin(),
            wanted.alternatives.end(),
            [&](const Constraint& option) {
                return detector.matches(component, option, architecture);
            }
        );

        if (!fits) {
            continue;
        }

        Option option;

        option.component = component.id();
        option.version = component.version();
        option.architecture = component.architecture();
        option.source = component.source();
        option.installed =
            installedIds.count(identityOf(component)) > 0;

        // Cost it by resolving for this component specifically,
        // rather than for the capability. Asking for the capability
        // would just return whatever the solver prefers, which is the
        // question this function exists to avoid answering.
        SolverRequest request;

        request.architecture = architecture;
        request.requirements.push_back(
            Requirement(Constraint(component.id()))
        );

        const SolverResult result = solver.solve(request);

        option.workable = result.status == SolverStatus::Success;

        if (option.workable) {
            option.componentCount = result.selected.size();

            for (const std::string& id : result.selected) {
                // result.selected holds ids; map back through the
                // universe to compare identities.
                const Component* found = nullptr;

                for (const Component& other : universe) {
                    if (other.id() == id) {
                        found = &other;
                        break;
                    }
                }

                if (found == nullptr) {
                    continue;
                }

                if (installedIds.count(identityOf(*found)) > 0) {
                    continue;
                }

                option.wouldAdd += 1;

                // Only what would actually be fetched counts.
                if (found->downloadSize() > 0) {
                    option.downloadBytes += found->downloadSize();
                    option.sizeKnown = true;
                }

                if (found->installedSize() > 0) {
                    option.installBytes += found->installedSize();
                    option.sizeKnown = true;
                }
            }
        } else {
            option.blockedOn = result.blockedOn.empty()
                ? result.reason
                : result.blockedOn;
        }

        // What is already here that this would collide with. Worth
        // knowing before choosing, not after.
        for (const Conflict& conflict :
             detector.check(component, installed)) {

            const std::string& other =
                conflict.componentId == component.id()
                    ? conflict.conflictsWith
                    : conflict.componentId;

            if (std::find(
                    option.conflictsWith.begin(),
                    option.conflictsWith.end(),
                    other) == option.conflictsWith.end()) {

                option.conflictsWith.push_back(other);
            }
        }

        report.options.push_back(std::move(option));
    }

    // Two builds of one package are one choice, not two. For an
    // amd64 request the i386 build is only eligible because of
    // multi-arch rules; offering it as an alternative is noise.
    if (!architecture.empty()) {
        const auto tier = [&architecture](const Option& option) {
            if (option.architecture == architecture) {
                return 0;
            }

            if (option.architecture == kArchitectureAll) {
                return 1;
            }

            return 2;
        };

        std::map<std::string, std::size_t> bestByName;
        std::vector<Option> kept;

        for (Option& option : report.options) {
            // The name is the id with any architecture suffix removed.
            std::string name = option.component;

            const std::size_t colon = name.rfind(':');

            if (colon != std::string::npos &&
                name.substr(colon + 1) == option.architecture) {
                name = name.substr(0, colon);
            }

            const auto existing = bestByName.find(name);

            if (existing == bestByName.end()) {
                bestByName[name] = kept.size();
                kept.push_back(std::move(option));
                continue;
            }

            Option& incumbent = kept[existing->second];

            // Prefer the installed one, then the better architecture.
            const bool replace =
                (option.installed && !incumbent.installed) ||
                (option.installed == incumbent.installed &&
                 tier(option) < tier(incumbent));

            if (replace) {
                incumbent = std::move(option);
            }
        }

        report.options = std::move(kept);
    }

    // Installed first, then workable, then cheapest. Somebody
    // choosing wants the thing they already have at the top and the
    // thing that cannot work at the bottom.
    std::stable_sort(
        report.options.begin(),
        report.options.end(),
        [](const Option& left, const Option& right) {
            if (left.installed != right.installed) {
                return left.installed;
            }

            if (left.workable != right.workable) {
                return left.workable;
            }

            if (left.wouldAdd != right.wouldAdd) {
                return left.wouldAdd < right.wouldAdd;
            }

            // Equal cost to add, so the smaller thing overall is the
            // lighter choice. Falling straight through to the name
            // put a 54-component option above a 42-component one.
            if (left.componentCount != right.componentCount) {
                return left.componentCount < right.componentCount;
            }

            return left.component < right.component;
        }
    );

    return report;
}

}
