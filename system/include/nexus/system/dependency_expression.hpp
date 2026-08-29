#pragma once

#include <optional>
#include <string>
#include <vector>

#include <nexus/constraint.hpp>

namespace nexus::system {

// Version relations live in core: every package ecosystem has them,
// only the comparison algorithm is Debian-specific.
using nexus::VersionConstraint;
using nexus::VersionRelation;

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

std::string toString(const DependencyTerm& term);
std::string toString(const DependencyClause& clause);

}
