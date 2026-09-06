#include <nexus/system/pacman_source.hpp>

#include <nexus/system/process.hpp>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>

#include <nexus/constraint.hpp>
#include <nexus/requirement.hpp>
#include <nexus/system/rpm_source.hpp>
#include <nexus/system/rpm_version.hpp>

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

bool isFieldName(const std::string& line) {
    return line.size() >= 3 && line.front() == '%' && line.back() == '%';
}

}

int comparePacmanConstraint(
    const std::string& provided,
    const std::string& required
) {
    return compareRpmConstraint(provided, required);
}

Constraint parsePacmanDependency(const std::string& text) {
    const std::string body = trim(text);

    // The operator is written against the name with no spaces:
    // glibc>=2.38. Longest first, so >= is not read as >.
    static const struct {
        const char* symbol;
        VersionRelation relation;
    } operators[] = {
        {">=", VersionRelation::LaterOrEqual},
        {"<=", VersionRelation::EarlierOrEqual},
        {"=",  VersionRelation::Exactly},
        {">",  VersionRelation::Later},
        {"<",  VersionRelation::Earlier},
    };

    for (const auto& candidate : operators) {
        const std::size_t at = body.find(candidate.symbol);

        if (at == std::string::npos || at == 0) {
            continue;
        }

        Constraint constraint(body.substr(0, at));

        const std::string version =
            body.substr(at + std::string(candidate.symbol).size());

        if (!version.empty()) {
            constraint.version =
                VersionConstraint{candidate.relation, version};
        }

        return constraint;
    }

    // A description may follow a colon in optional dependencies.
    const std::size_t colon = body.find(':');

    return Constraint(
        colon == std::string::npos ? body : trim(body.substr(0, colon))
    );
}

PacmanSourceResult parsePacmanDatabase(
    const std::string& text,
    Source source
) {
    PacmanSourceResult result;

    std::istringstream input(text);
    std::string line;

    std::string field;

    std::string name;
    std::string version;
    std::string architecture;
    std::uint64_t downloadSize = 0;
    std::uint64_t installedSize = 0;

    std::vector<std::string> depends;
    std::vector<std::string> provides;
    std::vector<std::string> conflicts;

    const auto finish = [&]() {
        if (name.empty()) {
            return;
        }

        result.packagesRead += 1;

        Component component(
            name, name, version, ComponentType::Application);

        component.setSource(source);
        component.setDownloadSize(downloadSize);
        component.setInstalledSize(installedSize);

        component.setArchitecture(
            architecture == "any"
                ? std::string(kArchitectureAll)
                : normaliseRpmArchitecture(architecture));

        component.addProvidedCapability(Capability(name));

        for (const std::string& entry : provides) {
            const Constraint constraint =
                parsePacmanDependency(entry);

            component.addProvidedCapability(
                constraint.version
                    ? Capability(constraint.capability,
                                 constraint.version->version)
                    : Capability(constraint.capability));
        }

        for (const std::string& entry : depends) {
            component.addRequirement(
                Requirement(parsePacmanDependency(entry)));
        }

        for (const std::string& entry : conflicts) {
            component.addConflict(parsePacmanDependency(entry));
        }

        result.components.push_back(std::move(component));

        name.clear();
        version.clear();
        architecture.clear();
        downloadSize = 0;
        installedSize = 0;
        depends.clear();
        provides.clear();
        conflicts.clear();
    };

    while (std::getline(input, line)) {
        const std::string body = trim(line);

        if (isFieldName(body)) {
            // A new package begins where its filename does.
            if (body == "%FILENAME%") {
                finish();
            }

            field = body;
            continue;
        }

        if (body.empty()) {
            continue;
        }

        if (field == "%NAME%") {
            name = body;
        } else if (field == "%VERSION%") {
            version = body;
        } else if (field == "%ARCH%") {
            architecture = body;
        } else if (field == "%CSIZE%") {
            downloadSize = std::strtoull(body.c_str(), nullptr, 10);
        } else if (field == "%ISIZE%") {
            installedSize = std::strtoull(body.c_str(), nullptr, 10);
        } else if (field == "%DEPENDS%") {
            depends.push_back(body);
        } else if (field == "%PROVIDES%") {
            provides.push_back(body);
        } else if (field == "%CONFLICTS%") {
            conflicts.push_back(body);
        }
    }

    finish();

    return result;
}

PacmanSourceResult readPacmanDatabase(
    const std::string& path,
    Source source
) {
    PacmanSourceResult result;

    if (!commandExists("tar")) {
        result.error = "tar is not available";
        return result;
    }

    // tar's stderr is kept. Discarding it turns every failure into
    // "held nothing readable", which says nothing about whether the
    // file is missing, truncated, an error page, or compressed with
    // something else -- and those need different answers.
    const std::string errors =
        "/tmp/nexus-pacman-" + std::to_string(::getpid());

    // -a rather than -z: Arch has shipped gzip databases for years,
    // but assuming the compression is how a reader ends up working on
    // one repository and failing on the next.
    const std::string command =
        "tar -xaOf '" + path + "' 2>" + errors;

    const ProcessResult ran = runCommand(command, false);

    if (!ran.ran) {
        result.error = "could not run tar on " + path;
        return result;
    }

    const std::string& text = ran.text;

    if (text.empty()) {
        result.error = path + ": ";

        std::ifstream why(errors);
        std::string message;

        if (why && std::getline(why, message) && !message.empty()) {
            result.error += message;
        } else {
            result.error += "tar produced nothing and said nothing";
        }

        std::error_code removal;
        std::filesystem::remove(errors, removal);

        return result;
    }

    std::error_code removal;
    std::filesystem::remove(errors, removal);

    return parsePacmanDatabase(text, source);
}

}
