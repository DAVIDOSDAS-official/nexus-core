#include <nexus/system/dpkg_source.hpp>

#include <utility>

#include <nexus/constraint.hpp>

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
    for (const DependencyClause& clause : clauses) {
        if (clause.alternatives.size() > 1) {
            gaps.push_back(ModelGap{
                componentId,
                ModelGapKind::Alternatives,
                field,
                toString(clause)
            });
        }

        for (const DependencyTerm& term : clause.alternatives) {
            if (term.constraint) {
                gaps.push_back(ModelGap{
                    componentId,
                    ModelGapKind::VersionConstraint,
                    field,
                    toString(term)
                });
            }
        }
    }
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

    for (const ControlStanza& stanza : stanzas) {
        result.stanzasRead += 1;

        const std::string name = stanza.value("package");

        if (name.empty() || !isInstalled(stanza)) {
            result.stanzasSkipped += 1;
            continue;
        }

        Component component(
            name,
            name,
            stanza.value("version"),
            classifySection(stanza.value("section"))
        );

        // A package always provides its own name as a capability.
        // This is what makes "Depends: libc6" resolvable as a
        // capability request rather than a package name lookup.
        component.addProvidedCapability(Capability(name));

        if (stanza.has("provides")) {
            for (const DependencyTerm& term :
                 parseProvidesField(stanza.value("provides"))) {

                component.addProvidedCapability(Capability(term.name));

                if (term.constraint) {
                    result.gaps.push_back(ModelGap{
                        name,
                        ModelGapKind::VersionedProvides,
                        "Provides",
                        toString(term)
                    });
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

                if (clause.isSimple()) {
                    result.representableClauses += 1;
                }

                // The model can only hold a single name, so the first
                // alternative is used. The gap list records that this
                // is a lossy choice.
                if (!clause.alternatives.empty()) {
                    component.addRequiredCapability(
                        Capability(clause.alternatives.front().name)
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
        case ModelGapKind::Alternatives:
            return "alternatives";
        case ModelGapKind::VersionConstraint:
            return "version-constraint";
        case ModelGapKind::VersionedProvides:
            return "versioned-provides";
        case ModelGapKind::PreDependency:
            return "pre-dependency";
    }

    return "unknown";
}
}
