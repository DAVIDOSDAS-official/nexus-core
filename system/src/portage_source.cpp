#include <nexus/system/portage_source.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

namespace nexus::system {

namespace {

std::string trim(const std::string& text) {
    const std::size_t begin = text.find_first_not_of(" \t\r\n");

    if (begin == std::string::npos) {
        return "";
    }

    const std::size_t end = text.find_last_not_of(" \t\r\n");

    return text.substr(begin, end - begin + 1);
}

std::vector<std::string> tokens(const std::string& text) {
    std::vector<std::string> out;

    std::istringstream input(text);
    std::string token;

    while (input >> token) {
        out.push_back(token);
    }

    return out;
}

}

bool isLiveEbuild(const std::string& version) {
    // 9999, and by convention 99999999 and so on.
    if (version.size() < 4) {
        return false;
    }

    return version.find_first_not_of('9') == std::string::npos;
}

std::pair<std::string, std::string> splitNameVersion(
    const std::string& text
) {
    // A version begins at a hyphen followed by a digit. Names contain
    // hyphens too -- openpgp-keys-bennoschulenberg -- so splitting at
    // the first or last one is wrong in both directions.
    for (std::size_t index = 0; index + 1 < text.size(); ++index) {
        if (text[index] != '-') {
            continue;
        }

        if (std::isdigit(static_cast<unsigned char>(text[index + 1]))
                == 0) {
            continue;
        }

        return {text.substr(0, index), text.substr(index + 1)};
    }

    return {text, ""};
}

std::set<std::string> defaultUseFlags(const std::string& iuse) {
    std::set<std::string> on;

    for (const std::string& flag : tokens(iuse)) {
        // "+spell" is on by default, "spell" is off, "-spell" is off
        // and cannot be enabled from a profile.
        if (!flag.empty() && flag.front() == '+') {
            on.insert(flag.substr(1));
        }
    }

    return on;
}

int comparePortageVersions(
    const std::string& left,
    const std::string& right
) {
    // Portage compares numerically per component, so 9.10 is later
    // than 9.9 -- which a string comparison gets backwards. Suffixes
    // like -r1 order after the version they revise.
    const auto split = [](const std::string& text) {
        std::vector<long> parts;
        std::string current;

        for (char character : text) {
            if (std::isdigit(
                    static_cast<unsigned char>(character)) != 0) {
                current.push_back(character);
                continue;
            }

            if (!current.empty()) {
                parts.push_back(std::stol(current));
                current.clear();
            }
        }

        if (!current.empty()) {
            parts.push_back(std::stol(current));
        }

        return parts;
    };

    const std::vector<long> ours = split(left);
    const std::vector<long> theirs = split(right);

    for (std::size_t index = 0;
         index < std::max(ours.size(), theirs.size());
         ++index) {

        const long a = index < ours.size() ? ours[index] : 0;
        const long b = index < theirs.size() ? theirs[index] : 0;

        if (a != b) {
            return a < b ? -1 : 1;
        }
    }

    return 0;
}

Constraint parsePortageAtom(const std::string& text) {
    std::string atom = trim(text);

    if (atom.empty()) {
        return Constraint("");
    }

    // A leading ! is a block, not a dependency.
    if (atom.front() == '!') {
        return Constraint("");
    }

    VersionRelation relation = VersionRelation::LaterOrEqual;
    bool versioned = false;

    if (atom.rfind(">=", 0) == 0) {
        relation = VersionRelation::LaterOrEqual;
        versioned = true;
        atom = atom.substr(2);
    } else if (atom.rfind("<=", 0) == 0) {
        relation = VersionRelation::EarlierOrEqual;
        versioned = true;
        atom = atom.substr(2);
    } else if (atom.front() == '>') {
        relation = VersionRelation::Later;
        versioned = true;
        atom = atom.substr(1);
    } else if (atom.front() == '<') {
        relation = VersionRelation::Earlier;
        versioned = true;
        atom = atom.substr(1);
    } else if (atom.front() == '=' || atom.front() == '~') {
        // ~ means "this version, any revision", which is the same
        // precision rule rpm and pacman have.
        relation = VersionRelation::Exactly;
        versioned = true;
        atom = atom.substr(1);
    }

    // USE dependencies in square brackets say how the dependency must
    // be built, not which one it is.
    const std::size_t bracket = atom.find('[');

    if (bracket != std::string::npos) {
        atom = atom.substr(0, bracket);
    }

    // A slot dependency is a compatibility declaration, not part of
    // the name.
    const std::size_t colon = atom.find(':');

    if (colon != std::string::npos) {
        atom = atom.substr(0, colon);
    }

    if (!versioned) {
        return Constraint(atom);
    }

    const auto [name, version] = splitNameVersion(atom);

    if (version.empty()) {
        return Constraint(atom);
    }

    Constraint constraint(name);

    constraint.version = VersionConstraint{relation, version};

    return constraint;
}

namespace {

// Walk a dependency string, which nests.
void collect(
    const std::string& text,
    const std::set<std::string>& useOn,
    std::vector<Requirement>& into,
    PortageSourceResult& result
) {
    const std::vector<std::string> parts = tokens(text);

    std::size_t index = 0;

    while (index < parts.size()) {
        const std::string& part = parts[index];

        // flag? ( ... ) and || ( ... ) both open a group.
        const bool conditional =
            part.size() > 1 && part.back() == '?';
        const bool anyOf = part == "||";

        if ((conditional || anyOf) && index + 1 < parts.size() &&
            parts[index + 1] == "(") {

            // Find the matching close.
            std::size_t depth = 0;
            std::size_t end = index + 1;

            for (; end < parts.size(); ++end) {
                if (parts[end] == "(") {
                    depth += 1;
                } else if (parts[end] == ")") {
                    depth -= 1;

                    if (depth == 0) {
                        break;
                    }
                }
            }

            std::string inner;

            for (std::size_t at = index + 2; at < end; ++at) {
                inner += parts[at];
                inner += " ";
            }

            if (anyOf) {
                // Any of these will do: one requirement, several
                // ways to meet it.
                std::vector<Constraint> alternatives;

                for (const std::string& atom : tokens(inner)) {
                    if (atom == "(" || atom == ")") {
                        continue;
                    }

                    const Constraint constraint =
                        parsePortageAtom(atom);

                    if (!constraint.capability.empty()) {
                        alternatives.push_back(constraint);
                    }
                }

                if (!alternatives.empty()) {
                    into.push_back(
                        Requirement(std::move(alternatives)));
                }
            } else {
                std::string flag = part.substr(0, part.size() - 1);

                const bool negated =
                    !flag.empty() && flag.front() == '!';

                if (negated) {
                    flag = flag.substr(1);
                }

                const bool active =
                    useOn.count(flag) > 0 ? !negated : negated;

                result.conditional += 1;

                if (active) {
                    collect(inner, useOn, into, result);
                }
            }

            index = end + 1;
            continue;
        }

        if (part == "(" || part == ")") {
            index += 1;
            continue;
        }

        const Constraint constraint = parsePortageAtom(part);

        if (!constraint.capability.empty()) {
            into.push_back(Requirement(constraint));
        }

        index += 1;
    }
}

}

PortageSourceResult parsePortageEntry(
    const std::string& category,
    const std::string& nameVersion,
    const std::string& text
) {
    PortageSourceResult result;

    const auto [name, version] = splitNameVersion(nameVersion);

    if (name.empty()) {
        return result;
    }

    std::map<std::string, std::string> fields;

    std::istringstream input(text);
    std::string line;

    while (std::getline(input, line)) {
        const std::size_t equals = line.find('=');

        if (equals == std::string::npos) {
            continue;
        }

        fields[line.substr(0, equals)] = line.substr(equals + 1);
    }

    const std::string qualified = category + "/" + name;

    Component component(
        qualified, qualified, version, ComponentType::Application);

    component.setSource(Source::Base);
    component.setArchitecture(kArchitectureAll);

    // Both spellings: profiles ask for "nano", portage calls it
    // "app-editors/nano", and something has to bridge them.
    component.addProvidedCapability(Capability(qualified));
    component.addProvidedCapability(Capability(name));

    const std::set<std::string> useOn =
        defaultUseFlags(fields["IUSE"]);

    std::vector<Requirement> requirements;

    // RDEPEND is what it needs to run. DEPEND and BDEPEND are what it
    // needs to build, which on Gentoo everybody also needs -- but
    // they are a different question and mixing them makes every
    // package look like it needs a compiler.
    collect(fields["RDEPEND"], useOn, requirements, result);

    for (Requirement& requirement : requirements) {
        component.addRequirement(std::move(requirement));
    }

    if (!fields["REQUIRED_USE"].empty()) {
        result.gaps["required-use constraints"] += 1;
    }

    if (isLiveEbuild(version)) {
        result.liveEbuilds = 1;
    }

    result.components.push_back(std::move(component));
    result.packagesRead = 1;

    return result;
}

PortageSourceResult readPortageTree(
    const std::string& path,
    const std::set<std::string>& categories,
    bool keepEveryVersion
) {
    PortageSourceResult result;

    std::error_code error;

    if (!std::filesystem::is_directory(path, error)) {
        result.error = path + " is not a portage md5-cache directory";
        return result;
    }

    for (const auto& categoryEntry :
         std::filesystem::directory_iterator(path, error)) {

        if (!categoryEntry.is_directory(error)) {
            continue;
        }

        const std::string category =
            categoryEntry.path().filename().string();

        if (!categories.empty() && categories.count(category) == 0) {
            continue;
        }

        for (const auto& entry :
             std::filesystem::directory_iterator(
                 categoryEntry.path(), error)) {

            if (!entry.is_regular_file(error)) {
                continue;
            }

            std::ifstream file(entry.path());

            if (!file) {
                continue;
            }

            std::ostringstream buffer;

            buffer << file.rdbuf();

            const auto one = parsePortageEntry(
                category,
                entry.path().filename().string(),
                buffer.str());

            for (const Component& component : one.components) {
                if (!keepEveryVersion &&
                    isLiveEbuild(component.version())) {
                    continue;
                }

                result.components.push_back(component);
            }

            result.packagesRead += one.packagesRead;
            result.conditional += one.conditional;
            result.liveEbuilds += one.liveEbuilds;

            for (const auto& [kind, count] : one.gaps) {
                result.gaps[kind] += count;
            }
        }
    }

    if (keepEveryVersion) {
        return result;
    }

    // One entry per package, keeping the newest.
    //
    // Compared with portage's own ordering, which is not Debian's:
    // 9.10 is later than 9.9, and a plain string comparison says the
    // opposite.
    std::map<std::string, std::size_t> best;
    std::vector<Component> newest;

    for (const Component& component : result.components) {
        const auto existing = best.find(component.id());

        if (existing == best.end()) {
            best[component.id()] = newest.size();
            newest.push_back(component);
            continue;
        }

        Component& kept = newest[existing->second];

        if (comparePortageVersions(
                component.version(), kept.version()) > 0) {
            kept = component;
        }
    }

    result.components = std::move(newest);

    return result;
}

}
