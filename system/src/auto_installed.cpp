#include <nexus/system/auto_installed.hpp>

#include <fstream>

#include <nexus/system/control_file.hpp>

namespace nexus::system {

std::set<std::string> readAutoInstalled(const std::string& path) {
    std::set<std::string> automatic;

    std::ifstream input(path);

    if (!input) {
        return automatic;
    }

    for (const ControlStanza& stanza : parseControlStream(input)) {
        if (stanza.value("auto-installed") != "1") {
            continue;
        }

        const std::string name = stanza.value("package");

        if (name.empty()) {
            continue;
        }

        std::string architecture = stanza.value("architecture");

        if (architecture.empty()) {
            architecture = "all";
        }

        automatic.insert(name + ":" + architecture);
    }

    return automatic;
}

}
