#include <nexus/system/flatpak_source.hpp>

#include <cstdio>
#include <cstdlib>
#include <set>
#include <sstream>

namespace nexus::system {

namespace {

std::vector<std::string> splitTabs(const std::string& line) {
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

std::string run(const std::string& command) {
    std::FILE* pipe = popen((command + " 2>/dev/null").c_str(), "r");

    if (pipe == nullptr) {
        return "";
    }

    std::string output;
    char buffer[65536];
    std::size_t read = 0;

    while ((read = std::fread(buffer, 1, sizeof(buffer), pipe)) > 0) {
        output.append(buffer, read);
    }

    pclose(pipe);

    return output;
}

}

std::uint64_t parseHumanSize(const std::string& text) {
    if (text.empty()) {
        return 0;
    }

    const double amount = std::strtod(text.c_str(), nullptr);

    if (amount <= 0) {
        return 0;
    }

    // The unit follows a space: "196.9 MB".
    const std::size_t space = text.rfind(' ');

    if (space == std::string::npos) {
        return static_cast<std::uint64_t>(amount);
    }

    const std::string unit = text.substr(space + 1);

    // flatpak uses decimal units, and prints kB rather than KB.
    if (unit == "B") {
        return static_cast<std::uint64_t>(amount);
    }

    if (unit == "kB" || unit == "KB") {
        return static_cast<std::uint64_t>(amount * 1000);
    }

    if (unit == "MB") {
        return static_cast<std::uint64_t>(amount * 1000 * 1000);
    }

    if (unit == "GB") {
        return static_cast<std::uint64_t>(amount * 1000 * 1000 * 1000);
    }

    return static_cast<std::uint64_t>(amount);
}

std::string formatSize(std::uint64_t bytes) {
    if (bytes == 0) {
        return "";
    }

    char buffer[32]{};

    if (bytes >= 1000ull * 1000 * 1000) {
        std::snprintf(buffer, sizeof(buffer), "%.1f GB",
                      static_cast<double>(bytes) / 1e9);
    } else if (bytes >= 1000ull * 1000) {
        std::snprintf(buffer, sizeof(buffer), "%.0f MB",
                      static_cast<double>(bytes) / 1e6);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.0f kB",
                      static_cast<double>(bytes) / 1e3);
    }

    return buffer;
}

bool FlatpakSource::available() {
    return std::system("flatpak --version > /dev/null 2>&1") == 0;
}

FlatpakSourceResult FlatpakSource::parse(
    const std::string& remote,
    const std::string& installed
) {
    FlatpakSourceResult result;

    std::set<std::string> here;

    {
        std::istringstream input(installed);
        std::string line;

        while (std::getline(input, line)) {
            if (line.empty()) {
                continue;
            }

            const auto fields = splitTabs(line);

            if (!fields.empty() && !fields[0].empty()) {
                here.insert(fields[0]);
                result.installedApps += 1;
            }
        }
    }

    std::set<std::string> seen;

    std::istringstream input(remote);
    std::string line;

    while (std::getline(input, line)) {
        if (line.empty()) {
            continue;
        }

        const auto fields = splitTabs(line);

        if (fields.size() < 2 || fields[0].empty()) {
            continue;
        }

        const std::string application = fields[0];

        // The same application can be offered by several remotes.
        if (!seen.insert(application).second) {
            continue;
        }

        result.remoteApps += 1;

        Component component(
            application,
            application,
            fields[1],
            ComponentType::Application
        );

        component.setSource(Source::Flatpak);

        // Flatpaks run anywhere the runtime does.
        component.setArchitecture(kArchitectureAll);

        component.addProvidedCapability(Capability(application));

        result.components.push_back(std::move(component));
    }

    return result;
}

FlatpakSourceResult FlatpakSource::load() const {
    FlatpakSourceResult result;

    if (!available()) {
        result.error = "flatpak is not installed";
        return result;
    }

    const std::string remote = run(
        "flatpak remote-ls --app "
        "--columns=application,version,branch,download-size");

    if (remote.empty()) {
        result.error =
            "flatpak listed nothing; no remote may be configured";
        return result;
    }

    const std::string installed = run(
        "flatpak list --app --columns=application,version,size");

    return parse(remote, installed);
}

}
