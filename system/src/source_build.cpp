#include <nexus/system/source_build.hpp>

#include <sstream>

#include <nexus/system/control_file.hpp>
#include <nexus/system/dependency_expression.hpp>
#include <nexus/system/process.hpp>

namespace nexus::system {

bool sourcePackagesAvailable() {
    if (!commandExists("apt-cache")) {
        return false;
    }

    // Asking for a source package that certainly exists is a more
    // reliable test than reading the configuration, which can be
    // spread across several files and formats.
    const ProcessResult probe =
        runCommand("apt-cache showsrc apt", false);

    return probe.ok && !probe.text.empty();
}

SourceBuildInfo parseSourceStanza(
    const std::string& package,
    const std::string& text
) {
    SourceBuildInfo info;

    info.package = package;

    std::istringstream input(text);

    for (const ControlStanza& stanza : parseControlStream(input)) {
        if (!stanza.has("package")) {
            continue;
        }

        info.sourcePackage = stanza.value("package");
        info.version = stanza.value("version");

        // Build-Depends and Build-Depends-Indep are both needed to
        // build; the second only for architecture-independent parts,
        // and leaving it out understates what a build requires.
        for (const char* field :
             {"build-depends", "build-depends-indep"}) {

            const std::string body = stanza.value(field);

            if (body.empty()) {
                continue;
            }

            for (const DependencyClause& clause :
                 parseDependencyField(body)) {

                std::vector<Constraint> options;

                for (const DependencyTerm& term : clause.alternatives) {
                    Constraint option = term.constraint
                        ? Constraint(term.name, *term.constraint)
                        : Constraint(term.name);

                    option.architecture = term.architecture;

                    options.push_back(std::move(option));
                }

                if (!options.empty()) {
                    info.buildDependencies.push_back(
                        Requirement(std::move(options)));
                }
            }
        }

        info.sourcesAvailable = true;

        // The first stanza is the one asked for; later ones are other
        // versions in other suites.
        break;
    }

    return info;
}

SourceBuildInfo readSourceBuild(const std::string& package) {
    SourceBuildInfo info;

    info.package = package;

    if (!sourcePackagesAvailable()) {
        info.error =
            "This system is not configured to fetch source packages.";

        return info;
    }

    const ProcessResult shown =
        runCommand("apt-cache showsrc '" + package + "'", false);

    if (!shown.ok || shown.text.empty()) {
        info.error =
            "No source package for '" + package + "'.";

        return info;
    }

    return parseSourceStanza(package, shown.text);
}

}
