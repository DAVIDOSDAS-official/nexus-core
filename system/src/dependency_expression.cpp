#include <nexus/system/dependency_expression.hpp>

#include <cctype>

namespace nexus::system {

namespace {

std::string trim(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();

    while (begin < end &&
           std::isspace(static_cast<unsigned char>(text[begin]))) {
        ++begin;
    }

    while (end > begin &&
           std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }

    return text.substr(begin, end - begin);
}

// Remove architecture restrictions "[...]" and build profiles "<...>".
//
// Angle brackets inside parentheses are version relations (<<, <=, >=,
// >>), not build profiles, so parenthesis depth is tracked first.
std::string stripRestrictions(const std::string& text) {
    std::string output;
    int bracketDepth = 0;
    int angleDepth = 0;
    int parenDepth = 0;

    for (char character : text) {
        const bool insideVersion = parenDepth > 0;

        if (character == '(' && bracketDepth == 0 && angleDepth == 0) {
            ++parenDepth;
            output.push_back(character);
            continue;
        }

        if (character == ')' && parenDepth > 0) {
            --parenDepth;
            output.push_back(character);
            continue;
        }

        if (!insideVersion) {
            if (character == '[') {
                ++bracketDepth;
                continue;
            }

            if (character == ']') {
                if (bracketDepth > 0) {
                    --bracketDepth;
                }
                continue;
            }

            if (character == '<') {
                ++angleDepth;
                continue;
            }

            if (character == '>') {
                if (angleDepth > 0) {
                    --angleDepth;
                }
                continue;
            }
        }

        if (bracketDepth == 0 && angleDepth == 0) {
            output.push_back(character);
        }
    }

    return output;
}

std::vector<std::string> split(
    const std::string& text,
    char separator
) {
    std::vector<std::string> parts;
    std::string current;

    for (char character : text) {
        if (character == separator) {
            parts.push_back(current);
            current.clear();
            continue;
        }

        current.push_back(character);
    }

    parts.push_back(current);

    return parts;
}

std::optional<VersionRelation> parseRelation(const std::string& text) {
    if (text == "<<") {
        return VersionRelation::Earlier;
    }

    if (text == "<=") {
        return VersionRelation::EarlierOrEqual;
    }

    if (text == "=") {
        return VersionRelation::Exactly;
    }

    if (text == ">=") {
        return VersionRelation::LaterOrEqual;
    }

    if (text == ">>") {
        return VersionRelation::Later;
    }

    // Deprecated single-angle forms still found in older metadata.
    if (text == "<") {
        return VersionRelation::EarlierOrEqual;
    }

    if (text == ">") {
        return VersionRelation::LaterOrEqual;
    }

    return std::nullopt;
}

DependencyTerm parseTerm(const std::string& text) {
    DependencyTerm term;

    std::string body = trim(text);

    const std::size_t open = body.find('(');

    if (open != std::string::npos) {
        const std::size_t close = body.find(')', open);

        std::string inside = (close == std::string::npos)
            ? body.substr(open + 1)
            : body.substr(open + 1, close - open - 1);

        inside = trim(inside);

        std::size_t cursor = 0;

        while (cursor < inside.size() &&
               !std::isspace(static_cast<unsigned char>(inside[cursor]))) {
            ++cursor;
        }

        const std::string relation = inside.substr(0, cursor);
        const std::string version = trim(inside.substr(cursor));

        if (const auto parsed = parseRelation(relation)) {
            term.constraint = VersionConstraint{*parsed, version};
        }

        body = trim(body.substr(0, open));
    }

    const std::size_t colon = body.find(':');

    if (colon != std::string::npos) {
        term.architecture = trim(body.substr(colon + 1));
        body = trim(body.substr(0, colon));
    }

    term.name = body;

    return term;
}

}

bool DependencyClause::isSimple() const {
    return alternatives.size() == 1 &&
           !alternatives.front().constraint.has_value();
}

std::vector<DependencyClause> parseDependencyField(
    const std::string& field
) {
    std::vector<DependencyClause> clauses;

    const std::string cleaned = stripRestrictions(field);

    for (const std::string& element : split(cleaned, ',')) {
        DependencyClause clause;

        for (const std::string& alternative : split(element, '|')) {
            const std::string body = trim(alternative);

            if (body.empty()) {
                continue;
            }

            clause.alternatives.push_back(parseTerm(body));
        }

        if (!clause.alternatives.empty()) {
            clauses.push_back(std::move(clause));
        }
    }

    return clauses;
}

std::vector<DependencyTerm> parseProvidesField(
    const std::string& field
) {
    std::vector<DependencyTerm> terms;

    for (const DependencyClause& clause : parseDependencyField(field)) {
        for (const DependencyTerm& term : clause.alternatives) {
            terms.push_back(term);
        }
    }

    return terms;
}

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

std::string toString(const DependencyTerm& term) {
    std::string text = term.name;

    if (term.architecture) {
        text += ":" + *term.architecture;
    }

    if (term.constraint) {
        text += " (" + toString(*term.constraint) + ")";
    }

    return text;
}

std::string toString(const DependencyClause& clause) {
    std::string text;

    for (std::size_t index = 0; index < clause.alternatives.size(); ++index) {
        if (index > 0) {
            text += " | ";
        }

        text += toString(clause.alternatives[index]);
    }

    return text;
}

}
