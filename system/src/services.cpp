#include <nexus/system/services.hpp>

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

}

std::string toString(ServiceState state) {
    switch (state) {
        case ServiceState::Enabled:   return "enabled";
        case ServiceState::Disabled:  return "disabled";
        case ServiceState::Static:    return "static";
        case ServiceState::Masked:    return "masked";
        case ServiceState::Generated: return "generated";
        case ServiceState::Alias:     return "alias";
        case ServiceState::Indirect:  return "indirect";
        case ServiceState::Unknown:   return "unknown";
    }

    return "unknown";
}

ServiceState parseServiceState(const std::string& text) {
    if (text == "enabled" || text == "enabled-runtime") {
        return ServiceState::Enabled;
    }

    if (text == "disabled") {
        return ServiceState::Disabled;
    }

    if (text == "static") {
        return ServiceState::Static;
    }

    if (text == "masked" || text == "masked-runtime") {
        return ServiceState::Masked;
    }

    if (text == "generated") {
        return ServiceState::Generated;
    }

    if (text == "alias") {
        return ServiceState::Alias;
    }

    if (text == "indirect") {
        return ServiceState::Indirect;
    }

    return ServiceState::Unknown;
}

ServicesResult parseUnitFiles(const std::string& text) {
    ServicesResult result;

    std::istringstream input(text);
    std::string line;

    bool first = true;

    while (std::getline(input, line)) {
        // The header names the columns; it is not a unit.
        if (first) {
            first = false;

            if (line.rfind("UNIT FILE", 0) == 0) {
                continue;
            }
        }

        // systemctl ends with a summary line and a blank one.
        if (line.empty() ||
            line.find("unit files listed") != std::string::npos) {
            continue;
        }

        const auto fields = columns(line);

        if (fields.size() < 2) {
            continue;
        }

        // Only services. Timers and sockets are real but they are a
        // different question.
        if (fields[0].size() < 8 ||
            fields[0].compare(
                fields[0].size() - 8, 8, ".service") != 0) {
            continue;
        }

        Service service;

        service.name = fields[0];
        service.state = parseServiceState(fields[1]);

        if (fields.size() > 2) {
            service.preset = fields[2];
        }

        if (service.startsAtBoot()) {
            result.enabled += 1;
        }

        result.services.push_back(std::move(service));
    }

    return result;
}

void applyFailedUnits(
    ServicesResult& result,
    const std::string& text
) {
    std::istringstream input(text);
    std::string line;

    while (std::getline(input, line)) {
        const auto fields = columns(line);

        if (fields.empty()) {
            continue;
        }

        // The failure marker sits in the first column on some
        // versions, so the unit name is whichever field ends in
        // .service.
        std::string name;

        for (const std::string& field : fields) {
            if (field.size() > 8 &&
                field.compare(field.size() - 8, 8, ".service") == 0) {
                name = field;
                break;
            }
        }

        if (name.empty()) {
            continue;
        }

        bool found = false;

        for (Service& service : result.services) {
            if (service.name == name) {
                service.failed = true;
                service.active = false;
                found = true;
                break;
            }
        }

        if (!found) {
            Service service;

            service.name = name;
            service.failed = true;

            result.services.push_back(std::move(service));
        }

        result.failedCount += 1;
    }
}

ServicesResult readServices() {
    ServicesResult result;

    if (!commandExists("systemctl")) {
        result.error = "systemctl is not available";
        return result;
    }

    const ProcessResult units = runCommand(
        "systemctl list-unit-files --type=service --no-pager "
        "--no-legend", false);

    if (!units.ran || units.text.empty()) {
        result.error = "systemd reported no unit files";
        return result;
    }

    result = parseUnitFiles(units.text);

    // Whether anything is running now needs a running systemd. On a
    // machine without one the unit files are still the truth about
    // what would run.
    const ProcessResult running = runCommand(
        "systemctl is-system-running", false);

    result.systemdRunning =
        running.ran && !running.text.empty() &&
        running.text.find("offline") == std::string::npos;

    if (result.systemdRunning) {
        const ProcessResult failed = runCommand(
            "systemctl list-units --type=service --state=failed "
            "--no-pager --no-legend", false);

        if (failed.ran) {
            applyFailedUnits(result, failed.text);
        }
    }

    return result;
}

}
