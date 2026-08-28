#pragma once

#include <optional>
#include <string>
#include <vector>

namespace nexus::system {

// Debian version relations, as they appear in control files.
enum class VersionRelation {
    Earlier,          // <<
    EarlierOrEqual,   // <=
    Exactly,          // =
    LaterOrEqual,     // >=
    Later             // >>
};

struct VersionConstraint {
    VersionRelation relation;
    std::string version;
};

// A single alternative inside a dependency clause,
// for example: base-passwd (>= 3.6.1)
struct DependencyTerm {
    std::string name;
    std::optional<std::string> architecture;
    std::optional<VersionConstraint> constraint;
};

// One comma-separated element of a Depends field.
//
// More than one alternative means the original field used "|",
// for example: base-passwd (>= 3.6.1) | adduser
struct DependencyClause {
    std::vector<DependencyTerm> alternatives;

    // True when this clause is a single unversioned package name,
    // which is the only shape the current Nexus component model
    // can represent without losing information.
    bool isSimple() const;
};

// Parse a Depends/Recommends/Suggests/Conflicts/Breaks field body.
// Architecture qualifiers ([...]) and build profiles (<...>) are
// stripped; they do not apply to installed-package metadata.
std::vector<DependencyClause> parseDependencyField(
    const std::string& field
);

// Parse a Provides field body. Provides only ever uses "=".
std::vector<DependencyTerm> parseProvidesField(
    const std::string& field
);

std::string toString(VersionRelation relation);
std::string toString(const VersionConstraint& constraint);
std::string toString(const DependencyTerm& term);
std::string toString(const DependencyClause& clause);

}
