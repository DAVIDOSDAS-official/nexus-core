#include <nexus/system/dpkg_source.hpp>

#include <map>
#include <utility>

#include <nexus/constraint.hpp>
#include <nexus/requirement.hpp>

namespace nexus::system {

namespace {

bool isInstalled(const ControlStanza& stanza) {
    const std::string status = stanza.value("status");

    // dpkg status is "<want> <error> <state>"; only the state matters.
    return status.size() >= 9 &&
           status.substr(status.size() - 9) == "installed";
}

void recordClauseGaps(
    const std::string& componentId,
    const std::string& field,
    const std::vector<DependencyClause>& clauses,
    std::vector<ModelGap>& gaps
) {
    // Alternatives, version conditions and architecture qualifiers
    // are all represented now. Nothing in a dependency field is
    // discarded, so nothing is recorded here.
    (void)componentId;
    (void)field;
    (void)clauses;
    (void)gaps;
}

}

DpkgSource::DpkgSource(std::string statusPath)
    : statusPath_(std::move(statusPath)) {
}

const std::string& DpkgSource::statusPath() const {
    return statusPath_;
}

DpkgSourceResult DpkgSource::load() const {
    return loadFromStanzas(parseControlFile(statusPath_));
}

DpkgSourceResult DpkgSource::loadFromStanzas(
    const std::vector<ControlStanza>& stanzas
) const {
    DpkgSourceResult result;

    // A package name is normally unique. When the same name is
    // installed for more than one architecture (libc6:amd64 and
    // libc6:i386), the plain name is ambiguous and the id has to be
    // qualified. Names that are unique keep their plain form so that
    // ordinary lookups are unaffected.
    std::map<std::string, int> nameCounts;

    for (const ControlStanza& stanza : stanzas) {
        if (isInstalled(stanza) && !stanza.value("package").empty()) {
            nameCounts[stanza.value("package")] += 1;
        }
    }

    for (const ControlStanza& stanza : stanzas) {
        result.stanzasRead += 1;

        const std::string name = stanza.value("package");

        if (name.empty() || !isInstalled(stanza)) {
            result.stanzasSkipped += 1;
            continue;
        }

        std::string architecture = stanza.value("architecture");

        if (architecture.empty()) {
            architecture = kArchitectureAll;
        }

        const std::string id = (nameCounts[name] > 1)
            ? name + ":" + architecture
            : name;

        Component component(
            id,
            name,
            stanza.value("version"),
            classifySection(stanza.value("section"))
        );

        component.setArchitecture(architecture);
        component.setMultiArch(
            parseMultiArch(stanza.value("multi-arch"))
        );

        // A package always provides its own name as a capability.
        // This is what makes "Depends: libc6" resolvable as a
        // capability request rather than a package name lookup.
        component.addProvidedCapability(Capability(name));

        if (stanza.has("provides")) {
            for (const DependencyTerm& term :
                 parseProvidesField(stanza.value("provides"))) {

                // "Provides: x (= 1.2)" states the version of the
                // virtual capability, which is not the package's own.
                if (term.constraint) {
                    component.addProvidedCapability(Capability(
                        term.name,
                        term.constraint->version
                    ));
                } else {
                    component.addProvidedCapability(
                        Capability(term.name)
                    );
                }
            }
        }

        for (const char* field : {"depends", "pre-depends"}) {
            if (!stanza.has(field)) {
                continue;
            }

            const auto clauses =
                parseDependencyField(stanza.value(field));

            for (const DependencyClause& clause : clauses) {
                result.dependencyClauses += 1;

                // Alternatives and version conditions are both held
                // by the model now, so every clause survives intact.
                result.representableClauses += 1;

                // Every alternative is kept, with its version
                // condition. Nothing is discarded, so nothing needs to
                // be recorded as a guess.
                std::vector<Constraint> options;

                for (const DependencyTerm& term : clause.alternatives) {
                    Constraint option = term.constraint
                        ? Constraint(term.name, *term.constraint)
                        : Constraint(term.name);

                    option.architecture = term.architecture;

                    options.push_back(std::move(option));
                }

                if (!options.empty()) {
                    component.addRequirement(
                        Requirement(std::move(options))
                    );
                }
            }

            recordClauseGaps(name, field, clauses, result.gaps);
        }

        if (stanza.has("pre-depends")) {
            result.gaps.push_back(ModelGap{
                name,
                ModelGapKind::PreDependency,
                "Pre-Depends",
                stanza.value("pre-depends")
            });
        }

        if (stanza.has("recommends")) {
            for (const DependencyClause& clause :
                 parseDependencyField(stanza.value("recommends"))) {

                if (!clause.alternatives.empty()) {
                    component.addRecommendedCapability(
                        Capability(clause.alternatives.front().name)
                    );
                }
            }
        }

        // Conflicts and Breaks are now first-class: the component
        // model can hold them, so they are no longer recorded as gaps.
        for (const char* field : {"conflicts", "breaks"}) {
            if (!stanza.has(field)) {
                continue;
            }

            for (const DependencyClause& clause :
                 parseDependencyField(stanza.value(field))) {

                for (const DependencyTerm& term : clause.alternatives) {
                    if (term.constraint) {
                        component.addConflict(
                            Constraint(term.name, *term.constraint)
                        );
                    } else {
                        component.addConflict(Constraint(term.name));
                    }
                }
            }
        }

        result.components.push_back(std::move(component));
    }

    return result;
}

ComponentType classifySection(const std::string& section) {
    // Sections may be namespaced, e.g. "universe/admin".
    const std::size_t slash = section.rfind('/');

    const std::string name = (slash == std::string::npos)
        ? section
        : section.substr(slash + 1);

    if (name == "kernel") {
        return ComponentType::Kernel;
    }

    if (name == "libs" || name == "libdevel" || name == "oldlibs") {
        return ComponentType::Library;
    }

    if (name == "devel") {
        return ComponentType::Development;
    }

    if (name == "net" || name == "comm") {
        return ComponentType::Networking;
    }

    if (name == "sound") {
        return ComponentType::Audio;
    }

    if (name == "admin" || name == "utils" || name == "shells") {
        return ComponentType::Utility;
    }

    if (name == "gnome" || name == "kde" || name == "xfce") {
        return ComponentType::Desktop;
    }

    if (name == "x11") {
        return ComponentType::DisplayServer;
    }

    return ComponentType::Application;
}

std::string toString(ModelGapKind kind) {
    switch (kind) {
        case ModelGapKind::PreDependency:
            return "pre-dependency";
    }

    return "unknown";
}
}
