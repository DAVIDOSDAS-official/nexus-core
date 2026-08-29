#include <nexus/system/apt_source.hpp>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <utility>

#include <nexus/constraint.hpp>
#include <nexus/requirement.hpp>
#include <nexus/system/control_file.hpp>
#include <nexus/system/dependency_expression.hpp>
#include <nexus/system/dpkg_source.hpp>
#include <nexus/system/version.hpp>

namespace nexus::system {

namespace {

bool endsWith(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() &&
           text.compare(
               text.size() - suffix.size(),
               suffix.size(),
               suffix
           ) == 0;
}

bool haveCommand(const std::string& command) {
    const std::string probe =
        "command -v " + command + " > /dev/null 2>&1";

    return std::system(probe.c_str()) == 0;
}

// Read a whole index file, decompressing if needed.
// Returns false and sets reason when it cannot be read.
bool readIndex(
    const std::string& path,
    std::string& contents,
    std::string& reason
) {
    const std::string tool = decompressorFor(path);

    if (tool.empty()) {
        std::ifstream input(path, std::ios::binary);

        if (!input) {
            reason = "cannot open file";
            return false;
        }

        std::ostringstream buffer;
        buffer << input.rdbuf();
        contents = buffer.str();

        return true;
    }

    if (!haveCommand(tool)) {
        reason =
            "compressed index needs '" + tool + "', which is not "
            "installed";
        return false;
    }

    const std::string command =
        tool + " -dc '" + path + "' 2>/dev/null";

    std::FILE* pipe = popen(command.c_str(), "r");

    if (pipe == nullptr) {
        reason = "could not run " + tool;
        return false;
    }

    std::string output;
    char buffer[65536];
    std::size_t read = 0;

    while ((read = std::fread(buffer, 1, sizeof(buffer), pipe)) > 0) {
        output.append(buffer, read);
    }

    const int status = pclose(pipe);

    if (status != 0 || output.empty()) {
        reason = tool + " produced no output";
        return false;
    }

    contents = std::move(output);

    return true;
}

Component fromStanza(const ControlStanza& stanza) {
    const std::string name = stanza.value("package");

    std::string architecture = stanza.value("architecture");

    if (architecture.empty()) {
        architecture = kArchitectureAll;
    }

    Component component(
        name,
        name,
        stanza.value("version"),
        classifySection(stanza.value("section"))
    );

    component.setArchitecture(architecture);
    component.setMultiArch(parseMultiArch(stanza.value("multi-arch")));
    component.addProvidedCapability(Capability(name));

    for (const DependencyTerm& term :
         parseProvidesField(stanza.value("provides"))) {

        component.addProvidedCapability(
            term.constraint
                ? Capability(term.name, term.constraint->version)
                : Capability(term.name)
        );
    }

    for (const char* field : {"depends", "pre-depends"}) {
        if (!stanza.has(field)) {
            continue;
        }

        for (const DependencyClause& clause :
             parseDependencyField(stanza.value(field))) {

            std::vector<Constraint> options;

            for (const DependencyTerm& term : clause.alternatives) {
                Constraint option = term.constraint
                    ? Constraint(term.name, *term.constraint)
                    : Constraint(term.name);

                option.architecture = term.architecture;

                options.push_back(std::move(option));
            }

            if (!options.empty()) {
                component.addRequirement(Requirement(std::move(options)));
            }
        }
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

    for (const char* field : {"conflicts", "breaks"}) {
        if (!stanza.has(field)) {
            continue;
        }

        for (const DependencyClause& clause :
             parseDependencyField(stanza.value(field))) {

            for (const DependencyTerm& term : clause.alternatives) {
                component.addConflict(
                    term.constraint
                        ? Constraint(term.name, *term.constraint)
                        : Constraint(term.name)
                );
            }
        }
    }

    return component;
}

}

std::string decompressorFor(const std::string& path) {
    if (endsWith(path, ".lz4")) {
        return "lz4";
    }

    if (endsWith(path, ".gz")) {
        return "gzip";
    }

    if (endsWith(path, ".xz")) {
        return "xz";
    }

    if (endsWith(path, ".bz2")) {
        return "bzip2";
    }

    return "";
}

AptSource::AptSource(std::string listsDirectory)
    : listsDirectory_(std::move(listsDirectory)) {
}

const std::string& AptSource::listsDirectory() const {
    return listsDirectory_;
}

AptSourceResult AptSource::load() const {
    AptSourceResult result;

    std::error_code error;

    if (!std::filesystem::is_directory(listsDirectory_, error)) {
        result.filesSkipped.push_back(SkippedIndex{
            listsDirectory_,
            "not a directory"
        });

        return result;
    }

    std::vector<std::string> indexes;

    for (const auto& entry :
         std::filesystem::directory_iterator(listsDirectory_, error)) {

        const std::string path = entry.path().string();
        std::string stem = entry.path().filename().string();

        // Strip a compression suffix before checking the name.
        for (const char* suffix : {".lz4", ".gz", ".xz", ".bz2"}) {
            if (endsWith(stem, suffix)) {
                stem = stem.substr(
                    0,
                    stem.size() - std::string(suffix).size()
                );
                break;
            }
        }

        if (endsWith(stem, "_Packages")) {
            indexes.push_back(path);
        }
    }

    std::sort(indexes.begin(), indexes.end());

    // name:architecture -> position in result.components
    std::map<std::string, std::size_t> best;

    for (const std::string& path : indexes) {
        std::string contents;
        std::string reason;

        if (!readIndex(path, contents, reason)) {
            result.filesSkipped.push_back(SkippedIndex{path, reason});
            continue;
        }

        result.filesRead.push_back(path);

        static const std::set<std::string> wanted{
            "package", "version", "architecture", "multi-arch",
            "section", "provides", "depends", "pre-depends",
            "recommends", "conflicts", "breaks"
        };

        std::istringstream input(contents);

        for (const ControlStanza& stanza :
             parseControlStream(input, wanted)) {
            if (!stanza.has("package")) {
                continue;
            }

            result.stanzasRead += 1;

            Component component = fromStanza(stanza);

            const std::string key =
                component.name() + ":" + component.architecture();

            const auto existing = best.find(key);

            if (existing == best.end()) {
                best[key] = result.components.size();
                result.components.push_back(std::move(component));
                continue;
            }

            // The same package appears in several suites. Keep the
            // highest version; count the rest rather than dropping
            // them silently.
            Component& kept = result.components[existing->second];

            result.versionsSuperseded += 1;

            if (compareVersions(
                    component.version(),
                    kept.version()) > 0) {

                kept = std::move(component);
            }
        }
    }

    return result;
}

std::vector<Component> mergeAvailable(
    const std::vector<Component>& installed,
    const std::vector<Component>& available
) {
    std::vector<Component> merged = installed;

    std::map<std::string, bool> present;

    for (const Component& component : installed) {
        present[component.name() + ":" + component.architecture()] =
            true;
    }

    for (const Component& component : available) {
        const std::string key =
            component.name() + ":" + component.architecture();

        if (present.count(key)) {
            continue;
        }

        merged.push_back(component);
    }

    return merged;
}

}
