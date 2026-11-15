// Profiles and image building: solve, image, profile.
//
// Part of the `nexus` tool. Until 0.1.96 this was in cli/src/main.cpp,
// which had grown past 11,000 lines; it was moved here unchanged.

#include "cli.hpp"

namespace nexus_cli {

int commandSolve(
    const nexus::AliasTable& aliases,
    const std::vector<Component>& universe,
    const std::string& capability,
    const std::string& prefer,
    const std::string& require,
    const std::string& architecture,
    bool explain
) {
    nexus::Solver solver = buildSolver(universe);

    nexus::SolverRequest request;
    request.architecture = architecture;
    request.requirements.push_back(
        aliases.expand(nexus::Requirement(nexus::Constraint(capability)))
    );

    if (!prefer.empty()) {
        request.preferred[capability] = prefer;
    }

    if (!require.empty()) {
        request.required[capability] = require;
    }

    const auto solution = solver.solve(request);

    std::cout
        << "Request:     " << capability << "\n"
        << "Arch:        "
        << (architecture.empty() ? "(any)" : architecture) << "\n"
        << "Status:      " << toString(solution.status) << "\n"
        << "Decisions:   " << solution.decisions << "\n"
        << "Backtracks:  " << solution.backtracks << "\n\n";

    if (solution.status != nexus::SolverStatus::Success) {
        std::cout << solution.reason << "\n";

        if (!solution.blockedOn.empty()) {
            // blockedOn may be a capability nothing provides or a
            // rule that was violated. "Nothing available provides"
            // fits only the first.
            std::cout << "\nBlocked on:\n    "
                      << solution.blockedOn << "\n";
        }

        return 1;
    }

    std::size_t choices = 0;

    for (const nexus::SolverStep& step : solution.steps) {
        if (step.alternativesConsidered > 1) {
            choices += 1;
        }
    }

    std::cout
        << "Selected " << solution.selected.size()
        << " component(s); " << choices
        << " involved a real choice.\n";

    if (!explain) {
        // A few hundred lines of "only component providing X" buries
        // the handful of lines that actually say something.
        if (choices > 0) {
            std::cout << "\nWhere there was a choice:\n";

            for (const nexus::SolverStep& step : solution.steps) {
                if (step.alternativesConsidered <= 1) {
                    continue;
                }

                std::cout
                    << "    " << step.selected << "\n"
                    << "        " << step.reason << "\n";
            }
        }

        std::cout
            << "\nUse --explain for the reason behind every "
            << "component.\n";

        return 0;
    }

    std::cout << "\nWhy each one:\n";

    for (const nexus::SolverStep& step : solution.steps) {
        std::cout << "    " << step.selected;

        if (step.wasBacktrackedInto) {
            std::cout << "  [after backtracking]";
        }

        std::cout << "\n        " << step.reason << "\n";
    }

    return 0;
}

// Emit the package list a profile resolves to, annotated with the
// capability each line answers.
//
// Only the directly chosen packages, not their dependencies: dnf and
// apt resolve those themselves, and a list of five hundred names is
// not something anybody can read. The point of generating it is that
// it stays readable -- a build you cannot read is a build you cannot
// audit.
// Resolve a profile name, which may be several separated by commas.
//
// Somebody who wants a machine for school and gaming wants both, not
// a choice between them. Returns false and explains when a name does
// not exist.
bool findComposition(
    const std::vector<nexus::Profile>& available,
    const std::string& name,
    nexus::Composition& composition
) {
    std::vector<std::string> wanted;
    std::string current;

    for (char character : name) {
        if (character == ',') {
            if (!current.empty()) {
                wanted.push_back(current);
            }

            current.clear();
            continue;
        }

        current.push_back(character);
    }

    if (!current.empty()) {
        wanted.push_back(current);
    }

    std::vector<nexus::Profile> picked;

    // Renamed (0.1.67): the security add-on is now "cybersecurity".
    // The old name keeps working, for people and for scripts.
    for (std::string& one : wanted) {
        if (one == "security") {
            one = "cybersecurity";
        }
    }

    for (const std::string& one : wanted) {
        const nexus::Profile* found = nullptr;

        for (const nexus::Profile& profile : available) {
            if (profile.name == one) {
                found = &profile;
                break;
            }
        }

        if (found == nullptr) {
            // Left out of this edition on purpose: say why, rather than
            // "unknown", which reads as a typo or a bug.
            if (one == "gaming") {
                std::cerr
                    << "The gaming add-on is in the KDE edition only.\n"
                    << "This edition leaves it out: Steam needs more "
                       "memory than the computers it is made for.\n"
                    << "'nexus gamecheck <game>' still works here.\n";
                return false;
            }

            std::cerr << "Unknown profile: " << one << "\n";

            // Somebody typing a name from memory gets it close.
            // "cybersecurity" for "security" is a better thing to
            // answer than a list of eleven names.
            std::vector<std::string> near;

            for (const nexus::Profile& profile : available) {
                const bool contains =
                    profile.name.find(one) != std::string::npos ||
                    one.find(profile.name) != std::string::npos;

                if (contains || closeSpelling(profile.name, one)) {
                    near.push_back(profile.name);
                }
            }

            if (!near.empty()) {
                std::cerr << "Did you mean:";

                for (const std::string& name : near) {
                    std::cerr << " " << name;
                }

                std::cerr << "?\n";
            } else {
                std::cerr << "Known profiles:";

                for (const nexus::Profile& profile : available) {
                    std::cerr << " " << profile.name;
                }

                std::cerr << "\n";
            }

            return false;
        }

        picked.push_back(*found);
    }

    if (picked.empty()) {
        std::cerr << "No profile named.\n";
        return false;
    }

    composition = nexus::compose(picked, name);

    return true;
}

void reportComposition(const nexus::Composition& composition) {
    if (composition.sources.size() < 2) {
        return;
    }

    std::cout
        << "Combined:    " << composition.sources.size()
        << " profiles, "
        << composition.profile.requirements.size()
        << " requirement(s)";

    if (!composition.shared.empty()) {
        std::cout
            << " (" << composition.shared.size()
            << " shared, asked for more than once)";
    }

    std::cout << "\n";

    for (const auto& clash : composition.clashes) {
        std::cout
            << "  note: " << clash.capability << " -> "
            << clash.chosen << " (from " << clash.chosenBy
            << "), not " << clash.overridden << " (from "
            << clash.overriddenBy << ")\n";
    }

    std::cout << "\n";
}

// Note that this resolves against the archive alone, never against
// what happens to be installed here.
//
// A generated list is a description of a machine that does not exist
// yet, so what is on this one must not decide it -- otherwise two
// people generating the same profile get different files, and a
// profile called minimal picks up whatever desktop the generating
// machine happened to run.
int commandImage(
    const nexus::AliasTable& aliases,
    const std::vector<Component>& universe,
    const std::string& profileName,
    const std::string& directory,
    const std::string& architecture
) {
    const auto loaded =
        nexus::system::parseProfileDirectory(directory);

    // base is composed into every image.
    //
    // It is defined as what any usable system needs regardless of
    // what it is for, so an image without it is not a machine. Asking
    // each caller to remember it is the kind of thing that gets
    // forgotten -- and was, for every image built until a profile
    // asked for something the base image did not already provide.
    const std::string requested =
        (profileName == "base" ||
         profileName.find("base") != std::string::npos)
            ? profileName
            : "base," + profileName;

    nexus::Composition composition;

    if (!findComposition(loaded.profiles, requested, composition)) {
        return 1;
    }

    if (composition.refused) {
        std::cerr << composition.refusal << "\n";
        return 1;
    }

    nexus::Profile profile = composition.profile;

    if (!architecture.empty()) {
        profile.architecture = architecture;
    }

    nexus::Solver solver = buildSolver(universe);

    std::cout
        << "# Generated by: nexus image " << profile.name << "\n"
        << "# " << profile.description << "\n"
        << "#\n"
        << "# One package per capability the profile asks for. Their\n"
        << "# dependencies are left to the package manager.\n"
        << "#\n"
        << "# Regenerate with:\n"
        << "#     nexus image " << profile.name
        << " --with-available\n"
        << "\n";

    std::size_t unresolved = 0;

    for (const nexus::Requirement& requirement :
         profile.requirements) {

        const nexus::Requirement expanded =
            aliases.expand(requirement);

        nexus::SolverRequest request;

        request.architecture = profile.architecture;
        request.requirements.push_back(expanded);
        request.preferred = profile.preferred;
        request.required = profile.required;

        // A preference keyed by capability does not survive alias
        // expansion: the profile asks for "terminal-emulator", the
        // alternative that matches is "x-terminal-emulator", and the
        // lookup misses. Favouring the component by id works whichever
        // alternative it came in on.
        for (const auto& [capability, component] :
             profile.preferred) {

            request.preferredComponents.insert(component);
        }

        const auto result = solver.solve(request);

        const std::string asked = toString(requirement);

        if (result.status != nexus::SolverStatus::Success ||
            result.steps.empty()) {

            std::cout
                << "# UNRESOLVED: " << asked << "\n";

            unresolved += 1;
            continue;
        }

        // The first step is what satisfied the requirement itself;
        // everything after it is a dependency.
        const std::string selected = result.steps.front().selected;

        // Emit the package name, not the internal id. Ids carry an
        // architecture suffix so that multilib builds stay distinct
        // inside the resolver -- but "kate:amd64" is not something
        // dnf or apt will accept, and a generated list that cannot be
        // fed to a package manager is not a generated list.
        std::string package = selected;

        for (const Component& component : universe) {
            if (component.id() == selected) {
                package = component.name();
                break;
            }
        }

        // Hardware capabilities are facts about the machine, not
        // things to install.
        if (package == "system-hardware") {
            std::cout << "# (hardware) " << asked << "\n";
            continue;
        }

        std::string padded = package;

        while (padded.size() < 32) {
            padded.push_back(' ');
        }

        std::cout << padded << "# " << asked << "\n";
    }

    if (unresolved > 0) {
        std::cerr
            << unresolved
            << " requirement(s) could not be resolved; the list above "
            << "is incomplete.\n";

        return 1;
    }

    return 0;
}

int commandProfile(
    const nexus::AliasTable& aliases,
    const std::vector<Component>& installed,
    const std::vector<Component>& universe,
    bool haveAvailable,
    const std::string& action,
    const std::string& name,
    const std::string& directory,
    const std::string& architecture
) {
    const auto loaded =
        nexus::system::parseProfileDirectory(directory);

    for (const std::string& problem : loaded.problems) {
        std::cerr << "Warning: " << problem << "\n";
    }

    if (loaded.profiles.empty()) {
        std::cerr
            << "No profiles found in " << directory << "\n"
            << "Use --profiles <dir> to point somewhere else.\n";
        return 1;
    }

    if (action == "list") {
        std::cout << "Profiles in " << directory << ":\n\n";

        for (const nexus::Profile& profile : loaded.profiles) {
            std::cout
                << "    " << profile.name << "\n"
                << "        " << profile.description << "\n"
                << "        " << profile.requirements.size()
                << " requirement(s)\n";
        }

        return 0;
    }

    nexus::Composition composition;

    if (!findComposition(loaded.profiles, name, composition)) {
        return 1;
    }

    if (composition.refused) {
        std::cerr << composition.refusal << "\n";
        return 1;
    }

    const nexus::Profile* chosen = &composition.profile;

    reportComposition(composition);

    if (action == "show") {
        std::cout
            << "Profile:      " << chosen->name << "\n"
            << "Description:  " << chosen->description << "\n"
            << "Architecture: "
            << (chosen->architecture.empty()
                    ? "(native)" : chosen->architecture)
            << "\n";

        if (!chosen->additionalArchitectures.empty()) {
            std::cout << "Also needs:   ";

            for (const std::string& extra :
                 chosen->additionalArchitectures) {

                std::cout << extra << " ";
            }

            std::cout << "\n";
        }

        std::cout << "\nRequires:\n";

        for (const nexus::Requirement& requirement :
             chosen->requirements) {

            std::cout << "    " << toString(requirement) << "\n";
        }

        if (!chosen->preferred.empty()) {
            std::cout << "\nPrefers:\n";

            for (const auto& [capability, component] :
                 chosen->preferred) {

                std::cout
                    << "    " << capability
                    << " -> " << component << "\n";
            }
        }

        return 0;
    }

    if (action != "check") {
        std::cerr
            << "Unknown profile action: " << action << "\n"
            << "Use list, show or check.\n";
        return 2;
    }

    nexus::Profile profile = *chosen;

    if (!architecture.empty()) {
        profile.architecture = architecture;
    }

    auto report =
        nexus::checkProfile(
            profile, buildSolver(installed), aliases);
    countInstalledFlatpaks(profile, report, installedFlatpakApps());

    // When archive data is loaded, a missing requirement can be
    // re-checked against everything available. That turns "you do not
    // have steam" into "steam is available and would bring N
    // components", which is the answer somebody setting up a machine
    // actually wants.
    nexus::ProfileReport possible;

    if (haveAvailable) {
        possible = nexus::checkProfile(
            profile, buildSolver(universe), aliases);
    }

    std::cout
        << "Profile:     " << report.profile << "\n"
        << "             " << report.description << "\n"
        << "Satisfied:   " << report.satisfied
        << " of " << report.items.size() << "\n";

    if (!report.appliedPreferences.empty()) {
        std::cout << "\nApplied for this machine:\n";

        for (const std::string& applied : report.appliedPreferences) {
            std::cout << "    " << applied << "\n";
        }
    }

    if (!report.inactivePreferences.empty()) {
        std::cout << "\nNot applicable here:\n";

        for (const std::string& idle : report.inactivePreferences) {
            std::cout << "    " << idle << "\n";
        }
    }

    std::cout << "\n";

    for (const nexus::ProfileItem& item : report.items) {
        if (item.satisfied) {
            std::cout
                << "  [ok]      " << item.requirement << "\n"
                << "            met by " << item.provided.front();
            if (item.provided.size() > 1) {
                std::cout << " (+" << (item.provided.size() - 1)
                          << " dependencies)";
            }
            std::cout << "\n";
        } else {
            std::cout
                << "  [missing] " << item.requirement << "\n";

            bool explained = false;

            if (haveAvailable) {
                for (const nexus::ProfileItem& option : possible.items) {
                    if (option.requirement != item.requirement) {
                        continue;
                    }

                    if (option.satisfied) {
                        std::cout
                            << "            available: install "
                            << option.provided.front() << " ("
                            << option.provided.size()
                            << " components in total)\n";
                    } else {
                        std::cout
                            << "            not available either: "
                            << option.blockedOn << "\n";
                    }

                    explained = true;
                    break;
                }
            }

            if (!explained) {
                std::cout
                    << "            nothing installed provides this"
                    << "\n";
            }
        }
    }

    if (report.complete()) {
        std::cout
            << "\nThis system already satisfies the "
            << report.profile << " profile.\n";
        return 0;
    }

    std::cout
        << "\n" << report.missing
        << " requirement(s) not met. Nothing has been changed;\n"
        << "this command only reports.\n";

    return 1;
}

}  // namespace nexus_cli
