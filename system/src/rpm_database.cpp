#include <nexus/system/rpm_database.hpp>

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <utility>
#include <vector>

#include <nexus/constraint.hpp>
#include <nexus/requirement.hpp>
#include <nexus/system/rpm_source.hpp>

namespace nexus::system {

namespace {

// One tab-separated record per line, tagged by kind, so a single rpm
// invocation yields every package and all of its dependencies.
const char* kFormat =
    "PKG\\t%{NAME}\\t%{EPOCH}\\t%{VERSION}\\t%{RELEASE}\\t%{ARCH}\\n"
    "[PRV\\t%{PROVIDENAME}\\t%{PROVIDEFLAGS:depflags}"
    "\\t%{PROVIDEVERSION}\\n]"
    "[REQ\\t%{REQUIRENAME}\\t%{REQUIREFLAGS:depflags}"
    "\\t%{REQUIREVERSION}\\t%{REQUIREFLAGS:deptype}\\n]"
    "[CON\\t%{CONFLICTNAME}\\t%{CONFLICTFLAGS:depflags}"
    "\\t%{CONFLICTVERSION}\\n]"
    "[FIL\\t%{FILENAMES}\\n]";

std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;

    for (char character : line) {
        if (character == '\t') {
            fields.push_back(current);
            current.clear();
            continue;
        }

        current.push_back(character);
    }

    fields.push_back(current);

    return fields;
}

std::string field(
    const std::vector<std::string>& fields,
    std::size_t index
) {
    return index < fields.size() ? fields[index] : std::string{};
}

// rpm prints "(none)" for an absent epoch rather than nothing.
bool absent(const std::string& value) {
    return value.empty() || value == "(none)";
}

// depflags may render as symbols or as the two-letter names used in
// repodata, depending on rpm version. Both are accepted rather than
// betting on one.
bool relationOf(const std::string& flags, VersionRelation& relation) {
    if (flags == "=" || flags == "EQ") {
        relation = VersionRelation::Exactly;
        return true;
    }

    if (flags == "<" || flags == "LT") {
        relation = VersionRelation::Earlier;
        return true;
    }

    if (flags == "<=" || flags == "LE") {
        relation = VersionRelation::EarlierOrEqual;
        return true;
    }

    if (flags == ">" || flags == "GT") {
        relation = VersionRelation::Later;
        return true;
    }

    if (flags == ">=" || flags == "GE") {
        relation = VersionRelation::LaterOrEqual;
        return true;
    }

    return false;
}

bool isRpmlib(const std::string& name) {
    return name.rfind("rpmlib(", 0) == 0;
}

// A rich dependency: "(a if b)", "(a or b)", "(a and b)".
bool isBoolean(const std::string& name) {
    return !name.empty() && name.front() == '(';
}

}

RpmDatabase::RpmDatabase(std::string root)
    : root_(std::move(root)) {
}

bool RpmDatabase::available() {
    return std::system("rpm --version > /dev/null 2>&1") == 0;
}

RpmDatabaseResult RpmDatabase::parse(const std::string& queryOutput) {
    RpmDatabaseResult result;

    std::istringstream input(queryOutput);
    std::string line;

    bool open = false;
    Component component("", "", "", ComponentType::Application);

    const auto finish = [&]() {
        if (!open) {
            return;
        }

        result.components.push_back(std::move(component));
        open = false;
    };

    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }

        if (line.empty()) {
            continue;
        }

        const std::vector<std::string> fields = split(line);
        const std::string& kind = fields[0];

        if (kind == "PKG") {
            finish();

            const std::string name = field(fields, 1);
            const std::string epoch = field(fields, 2);
            const std::string version = field(fields, 3);
            const std::string release = field(fields, 4);
            const std::string architecture = field(fields, 5);

            std::string full;

            if (!absent(epoch)) {
                full += epoch + ":";
            }

            full += version;

            if (!release.empty()) {
                full += "-" + release;
            }

            component = Component(
                name,
                name,
                full,
                ComponentType::Application
            );

            component.setArchitecture(
                normaliseRpmArchitecture(architecture)
            );

            component.addProvidedCapability(Capability(name));

            result.packagesRead += 1;
            open = true;

            continue;
        }

        if (!open) {
            continue;
        }

        const std::string name = field(fields, 1);
        const std::string flags = field(fields, 2);
        const std::string version = field(fields, 3);

        if (name.empty()) {
            continue;
        }

        if (kind == "PRV") {
            component.addProvidedCapability(
                version.empty()
                    ? Capability(name)
                    : Capability(name, version)
            );

            continue;
        }

        if (kind == "REQ") {
            // Requirements on rpm's own features are satisfied by the
            // rpm binary, never by a package. Left in, every package
            // on the system would look unsatisfiable.
            if (isRpmlib(name)) {
                result.rpmlibRequirements += 1;
                continue;
            }

            if (isBoolean(name)) {
                result.booleanRequirements += 1;
                continue;
            }

            Constraint constraint(name);

            VersionRelation relation;

            if (!flags.empty() && !version.empty() &&
                relationOf(flags, relation)) {
                constraint.version =
                    VersionConstraint{relation, version};
            }

            Requirement requirement(std::move(constraint));

            requirement.pre =
                field(fields, 4).find("pre") != std::string::npos;

            component.addRequirement(std::move(requirement));

            continue;
        }

        if (kind == "FIL") {
            // A path is a capability: rpm lets a requirement name a
            // file, satisfied by whichever package ships it.
            component.addProvidedCapability(Capability(name));
            result.fileProvides += 1;
            continue;
        }

        if (kind == "CON") {
            Constraint constraint(name);

            VersionRelation relation;

            if (!flags.empty() && !version.empty() &&
                relationOf(flags, relation)) {
                constraint.version =
                    VersionConstraint{relation, version};
            }

            component.addConflict(std::move(constraint));
        }
    }

    finish();

    return result;
}

RpmDatabaseResult RpmDatabase::load() const {
    RpmDatabaseResult result;

    if (!available()) {
        result.error = "rpm is not installed";
        return result;
    }

    std::string command = "rpm";

    if (!root_.empty()) {
        command += " --root '" + root_ + "'";
    }

    command += " -qa --qf '";
    command += kFormat;
    command += "' 2>/dev/null";

    std::FILE* pipe = popen(command.c_str(), "r");

    if (pipe == nullptr) {
        result.error = "could not run rpm";
        return result;
    }

    std::string output;
    char buffer[65536];
    std::size_t read = 0;

    while ((read = std::fread(buffer, 1, sizeof(buffer), pipe)) > 0) {
        output.append(buffer, read);
    }

    pclose(pipe);

    if (output.empty()) {
        result.error = "rpm returned nothing";
        return result;
    }

    return parse(output);
}

}
