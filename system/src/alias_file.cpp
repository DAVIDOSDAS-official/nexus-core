#include <nexus/system/alias_file.hpp>

#include <fstream>
#include <istream>

#include <nexus/system/control_file.hpp>
#include <nexus/system/dependency_expression.hpp>

namespace nexus::system {

AliasParseResult parseAliasStream(std::istream& input) {
    AliasParseResult result;

    for (const ControlStanza& stanza : parseControlStream(input)) {
        const std::string capability = stanza.value("capability");

        if (capability.empty()) {
            continue;
        }

        const std::string body = stanza.value("resolves-to");

        if (body.empty()) {
            result.problems.push_back(
                capability + ": no Resolves-To entries"
            );
            continue;
        }

        std::vector<Constraint> alternatives;

        for (const DependencyClause& clause :
             parseDependencyField(body)) {

            for (const DependencyTerm& term : clause.alternatives) {
                Constraint option = term.constraint
                    ? Constraint(term.name, *term.constraint)
                    : Constraint(term.name);

                option.architecture = term.architecture;

                alternatives.push_back(std::move(option));
            }
        }

        if (alternatives.empty()) {
            result.problems.push_back(
                capability + ": Resolves-To parsed to nothing"
            );
            continue;
        }

        result.table.add(capability, std::move(alternatives));
    }

    return result;
}

AliasParseResult parseAliasFile(const std::string& path) {
    std::ifstream input(path);

    AliasParseResult result;

    if (!input) {
        result.problems.push_back("Cannot open alias file: " + path);
        return result;
    }

    return parseAliasStream(input);
}

}
