#include <nexus/system/profile_file.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <istream>
#include <sstream>

#include <nexus/system/control_file.hpp>
#include <nexus/system/dependency_expression.hpp>

namespace nexus::system {

namespace {

std::string trim(const std::string& text) {
    const std::size_t begin = text.find_first_not_of(" \t\n");

    if (begin == std::string::npos) {
        return "";
    }

    const std::size_t end = text.find_last_not_of(" \t\n");

    return text.substr(begin, end - begin + 1);
}

std::vector<std::string> splitList(const std::string& text) {
    std::vector<std::string> parts;
    std::string current;

    for (char character : text) {
        if (character == ',') {
            parts.push_back(trim(current));
            current.clear();
            continue;
        }

        current.push_back(character);
    }

    const std::string last = trim(current);

    if (!last.empty()) {
        parts.push_back(last);
    }

    return parts;
}

// Reads "capability=component, capability=component".
void readMapping(
    const std::string& field,
    const std::string& value,
    const std::string& profileName,
    std::map<std::string, std::string>& target,
    std::vector<std::string>& problems
) {
    for (const std::string& entry : splitList(value)) {
        const std::size_t equals = entry.find('=');

        if (equals == std::string::npos) {
            problems.push_back(
                profileName + ": " + field + " entry '" + entry +
                "' is not in capability=component form"
            );
            continue;
        }

        const std::string capability = trim(entry.substr(0, equals));
        const std::string component = trim(entry.substr(equals + 1));

        if (capability.empty() || component.empty()) {
            problems.push_back(
                profileName + ": " + field + " entry '" + entry +
                "' has an empty side"
            );
            continue;
        }

        target[capability] = component;
    }
}

Profile fromStanza(
    const ControlStanza& stanza,
    std::vector<std::string>& problems
) {
    Profile profile;

    profile.name = stanza.value("profile");
    profile.description = stanza.value("description");
    profile.architecture = stanza.value("architecture");
    profile.exclusive = stanza.value("exclusive") == "yes";
    profile.insteadUse = stanza.value("instead-use");

    if (profile.name.empty()) {
        problems.push_back("A profile stanza has no Profile: field");
        return profile;
    }

    for (const std::string& architecture :
         splitList(stanza.value("enables-architectures"))) {

        if (!architecture.empty()) {
            profile.additionalArchitectures.push_back(architecture);
        }
    }

    // Requires uses the same grammar as a Depends field, so
    // alternatives and version conditions work exactly as they do
    // everywhere else.
    for (const DependencyClause& clause :
         parseDependencyField(stanza.value("requires"))) {

        std::vector<Constraint> options;

        for (const DependencyTerm& term : clause.alternatives) {
            Constraint option = term.constraint
                ? Constraint(term.name, *term.constraint)
                : Constraint(term.name);

            option.architecture = term.architecture;

            options.push_back(std::move(option));
        }

        if (!options.empty()) {
            profile.requirements.push_back(
                Requirement(std::move(options))
            );
        }
    }

    if (profile.requirements.empty()) {
        problems.push_back(
            profile.name + ": no Requires: entries, so the profile "
            "asks for nothing"
        );
    }

    readMapping(
        "Prefers",
        stanza.value("prefers"),
        profile.name,
        profile.preferred,
        problems
    );

    // Prefers-When: <capability> -> <component>, <capability> -> <component>
    for (const std::string& entry :
         splitList(stanza.value("prefers-when"))) {

        const std::size_t arrow = entry.find("->");

        if (arrow == std::string::npos) {
            problems.push_back(
                profile.name + ": Prefers-When entry '" + entry +
                "' is not in capability -> component form"
            );
            continue;
        }

        ConditionalPreference conditional;

        conditional.when = trim(entry.substr(0, arrow));
        conditional.prefer = trim(entry.substr(arrow + 2));

        if (conditional.when.empty() || conditional.prefer.empty()) {
            problems.push_back(
                profile.name + ": Prefers-When entry '" + entry +
                "' has an empty side"
            );
            continue;
        }

        profile.conditionalPreferences.push_back(conditional);
    }

    // Flatpak: <capability>=<application id>, ...
    readMapping(
        "Flatpak",
        stanza.value("flatpak"),
        profile.name,
        profile.flatpak,
        problems
    );

    readMapping(
        "Requires-Exactly",
        stanza.value("requires-exactly"),
        profile.name,
        profile.required,
        problems
    );

    return profile;
}

}

ProfileParseResult parseProfileStream(std::istream& input) {
    ProfileParseResult result;

    for (const ControlStanza& stanza : parseControlStream(input)) {
        if (!stanza.has("profile")) {
            continue;
        }

        Profile profile = fromStanza(stanza, result.problems);

        if (!profile.name.empty()) {
            result.profiles.push_back(std::move(profile));
        }
    }

    return result;
}

ProfileParseResult parseProfileFile(const std::string& path) {
    std::ifstream input(path);

    ProfileParseResult result;

    if (!input) {
        result.problems.push_back("Cannot open profile file: " + path);
        return result;
    }

    return parseProfileStream(input);
}

ProfileParseResult parseProfileDirectory(const std::string& path) {
    ProfileParseResult result;

    std::error_code error;

    if (!std::filesystem::is_directory(path, error)) {
        result.problems.push_back(
            "Not a profile directory: " + path
        );
        return result;
    }

    std::vector<std::string> files;

    for (const auto& entry :
         std::filesystem::directory_iterator(path, error)) {

        if (entry.path().extension() == ".profile") {
            files.push_back(entry.path().string());
        }
    }

    std::sort(files.begin(), files.end());

    for (const std::string& file : files) {
        ProfileParseResult one = parseProfileFile(file);

        for (Profile& profile : one.profiles) {
            result.profiles.push_back(std::move(profile));
        }

        for (std::string& problem : one.problems) {
            result.problems.push_back(std::move(problem));
        }
    }

    return result;
}

}
