#include <nexus/system/snap_source.hpp>

#include <sstream>

#include <nexus/system/process.hpp>

namespace nexus::system {

namespace {

std::vector<std::string> columns(const std::string& line) {
    std::vector<std::string> fields;

    std::istringstream input(line);
    std::string field;

    while (input >> field) {
        fields.push_back(field);
    }

    return fields;
}

bool notesContain(
    const std::string& notes,
    const std::string& want
) {
    return notes.find(want) != std::string::npos;
}

}

std::string snapOwningUnit(const std::string& unitName) {
    // snap.<snap>.<app>.service
    if (unitName.rfind("snap.", 0) != 0) {
        return "";
    }

    const std::string rest = unitName.substr(5);

    const std::size_t dot = rest.find('.');

    if (dot == std::string::npos) {
        return "";
    }

    return rest.substr(0, dot);
}

SnapSourceResult parseSnapList(const std::string& text) {
    SnapSourceResult result;

    std::istringstream input(text);
    std::string line;

    bool first = true;

    while (std::getline(input, line)) {
        if (first) {
            first = false;

            if (line.rfind("Name", 0) == 0) {
                continue;
            }
        }

        const auto fields = columns(line);

        if (fields.size() < 2) {
            continue;
        }

        const std::string& name = fields[0];
        const std::string& version = fields[1];

        const std::string notes =
            fields.size() > 5 ? fields[5] : std::string{};

        // --all lists superseded revisions too. They are on disk and
        // they are not what is in use.
        if (notesContain(notes, "disabled")) {
            continue;
        }

        Component component(
            name, name, version, ComponentType::Application);

        component.setSource(Source::Snap);

        // A snap runs on whatever its base supports.
        component.setArchitecture(kArchitectureAll);

        component.addProvidedCapability(Capability(name));

        if (notesContain(notes, "base")) {
            result.infrastructure += 1;
        } else {
            result.installed += 1;
        }

        result.components.push_back(std::move(component));
    }

    return result;
}

SnapSourceResult readSnaps() {
    SnapSourceResult result;

    if (!commandExists("snap")) {
        result.error = "snap is not installed";
        return result;
    }

    const ProcessResult listed = runCommand("snap list", false);

    if (!listed.ran) {
        result.error = "could not run snap";
        return result;
    }

    if (listed.text.empty()) {
        result.error = "snap listed nothing";
        return result;
    }

    return parseSnapList(listed.text);
}

}
