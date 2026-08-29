#include <nexus/constraint.hpp>

namespace nexus {

std::string toString(VersionRelation relation) {
    switch (relation) {
        case VersionRelation::Earlier:
            return "<<";
        case VersionRelation::EarlierOrEqual:
            return "<=";
        case VersionRelation::Exactly:
            return "=";
        case VersionRelation::LaterOrEqual:
            return ">=";
        case VersionRelation::Later:
            return ">>";
    }

    return "?";
}

std::string toString(const VersionConstraint& constraint) {
    return toString(constraint.relation) + " " + constraint.version;
}

std::string toString(const Constraint& constraint) {
    if (constraint.isUnversioned()) {
        return constraint.capability;
    }

    return constraint.capability +
           " (" + toString(*constraint.version) + ")";
}

}
