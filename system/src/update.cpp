#include <nexus/system/update.hpp>

#include <sstream>

namespace nexus::system {

namespace {

std::string trim(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1);
}

bool startsDeployment(const std::string& line) {
    // "● image" for the booted one, "  image" (two spaces) for others.
    return line.rfind("\xe2\x97\x8f ", 0) == 0 ||
           (line.size() > 2 && line[0] == ' ' && line[1] == ' ' &&
            line[2] != ' ');
}

}

SystemUpdate parseSystemUpdate(const std::string& statusText) {
    SystemUpdate out;
    std::istringstream lines(statusText);
    std::string line;
    bool inUpdate = false;
    bool inDeployments = false;
    std::string deploymentVersion;

    while (std::getline(lines, line)) {
        const std::string text = trim(line);

        if (line.rfind("AutomaticUpdates:", 0) == 0) {
            const std::string value = trim(line.substr(17));
            const auto semicolon = value.find(';');
            out.policy = trim(value.substr(0, semicolon));
            if (semicolon != std::string::npos) {
                out.lastCheck = trim(value.substr(semicolon + 1));
            }
            continue;
        }
        if (line.rfind("Deployments:", 0) == 0) {
            inDeployments = true;
            continue;
        }
        if (!inDeployments) {
            continue;
        }

        if (startsDeployment(line)) {
            inUpdate = false;
            deploymentVersion.clear();
            continue;
        }

        if (text == "AvailableUpdate:") {
            inUpdate = true;
            out.available = true;
            continue;
        }

        const auto colon = text.find(": ");
        if (colon == std::string::npos) {
            continue;
        }
        const std::string key = text.substr(0, colon);
        const std::string value = trim(text.substr(colon + 2));

        if (inUpdate) {
            if (key == "Version") {
                const auto paren = value.find(" (");
                out.version = value.substr(0, paren);
                if (paren != std::string::npos) {
                    out.date = value.substr(paren + 2);
                    if (!out.date.empty() && out.date.back() == ')') {
                        out.date.pop_back();
                    }
                }
            } else if (key == "Diff") {
                out.diff = value;
            }
            continue;
        }

        if (key == "Version") {
            deploymentVersion = value.substr(0, value.find(" ("));
        } else if (key == "Staged" && value == "yes") {
            out.staged = true;
            out.stagedVersion = deploymentVersion;
        }
    }

    return out;
}

std::vector<AppUpdate> parseFlatpakUpdates(const std::string& text) {
    std::vector<AppUpdate> out;
    std::istringstream lines(text);
    std::string line;

    while (std::getline(lines, line)) {
        std::istringstream words(line);
        AppUpdate app;
        words >> app.id;
        words >> app.version;
        // An application id has at least two dots (org.example.App);
        // anything else is a header or a message, not an update.
        int dots = 0;
        for (char c : app.id) {
            dots += c == '.';
        }
        if (dots >= 2) {
            out.push_back(app);
        }
    }

    return out;
}

std::string updateSummary(const SystemUpdate& system,
                          const std::vector<AppUpdate>& apps) {
    std::string out;
    if (system.available) {
        out = "Nexus " + (system.version.empty() ? std::string("update")
                                                 : system.version);
    }
    if (!apps.empty()) {
        if (!out.empty()) {
            out += " and ";
        }
        out += std::to_string(apps.size()) +
               (apps.size() == 1 ? " app update" : " app updates");
    }
    return out;
}

}
