#include <nexus/system/container.hpp>

#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <nexus/system/process.hpp>

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

ContainerResult execute(const std::string& command) {
    ContainerResult result;

    const ProcessResult ran = runCommand(command);

    if (!ran.ran) {
        result.error = "could not run distrobox";
        return result;
    }

    result.output = ran.lines;
    result.exitCode = ran.exitCode;
    result.ok = ran.ok;

    return result;
}

}

bool ContainerInfo::running() const {
    return status.find("Up") != std::string::npos ||
           status.find("running") != std::string::npos;
}

std::string containerImageFor(const std::string& distribution) {
    if (distribution == "arch") {
        return "docker.io/library/archlinux:latest";
    }

    if (distribution == "fedora") {
        return "registry.fedoraproject.org/fedora-toolbox:latest";
    }

    if (distribution == "debian") {
        return "docker.io/library/debian:stable";
    }

    if (distribution == "ubuntu") {
        return "docker.io/library/ubuntu:latest";
    }

    return "";
}

std::string containerNameFor(const std::string& distribution) {
    return "nexus-" + distribution;
}

bool Containers::available() {
    return commandExists("distrobox");
}

std::vector<ContainerInfo> Containers::parseList(
    const std::string& text
) {
    std::vector<ContainerInfo> containers;

    std::istringstream input(text);
    std::string line;

    bool first = true;

    while (std::getline(input, line)) {
        // The header is printed even when there are none, so an empty
        // list is a header and nothing else.
        if (first) {
            first = false;
            continue;
        }

        if (trim(line).empty()) {
            continue;
        }

        std::vector<std::string> fields;
        std::string current;

        for (char character : line) {
            if (character == '|') {
                fields.push_back(trim(current));
                current.clear();
                continue;
            }

            current.push_back(character);
        }

        fields.push_back(trim(current));

        if (fields.size() < 2 || fields[1].empty()) {
            continue;
        }

        ContainerInfo info;

        info.id = fields[0];
        info.name = fields[1];

        if (fields.size() > 2) {
            info.status = fields[2];
        }

        if (fields.size() > 3) {
            info.image = fields[3];
        }

        containers.push_back(std::move(info));
    }

    return containers;
}

std::vector<ContainerInfo> Containers::list() {
    if (!available()) {
        return {};
    }

    const ContainerResult listed = execute("distrobox list");

    std::string text;

    for (const std::string& line : listed.output) {
        text += line;
        text += "\n";
    }

    return parseList(text);
}

ContainerResult Containers::ensure(
    const std::string& name,
    const std::string& image
) {
    ContainerResult result;

    if (!available()) {
        result.error = "distrobox is not installed";
        return result;
    }

    if (image.empty()) {
        result.error = "no image known for that distribution";
        return result;
    }

    for (const ContainerInfo& info : list()) {
        if (info.name == name) {
            // Left alone deliberately: somebody may have put things
            // in theirs, and recreating it would throw those away.
            result.ok = true;
            result.output.push_back(
                "container " + name + " already exists");

            return result;
        }
    }

    const ContainerResult created = execute(
        "distrobox create --yes --name '" + name +
        "' --image '" + image + "'");

    if (!created.ok) {
        return created;
    }

    // Warm it before anything is asked of it.
    //
    // distrobox does its one-time setup -- sudo, mounts, the user,
    // the host integration -- on the first enter. Running an install
    // as that first enter means a setup failure is reported as the
    // package failing to install, which is the wrong diagnosis and
    // sends somebody looking in the wrong place. It also fails once
    // and then works on retry, which is the most confusing behaviour
    // available.
    ContainerResult ready = execute(
        "distrobox enter --name '" + name + "' -- true");

    if (!ready.ok) {
        ready.error =
            "the container was created but its first-run setup "
            "failed";
    }

    for (const std::string& line : created.output) {
        ready.output.insert(ready.output.begin(), line);
    }

    return ready;
}

ContainerResult Containers::run(
    const std::string& name,
    const std::string& command
) {
    if (!available()) {
        ContainerResult result;
        result.error = "distrobox is not installed";
        return result;
    }

    return execute(
        "distrobox enter --name '" + name + "' -- " + command);
}

Containers::Binaries Containers::binariesOf(
    const std::string& name,
    const std::string& distribution,
    const std::string& package
) {
    Binaries binaries;

    // Each package manager has its own way of listing what a package
    // owns, and each is authoritative for its own packages.
    std::string query;

    if (distribution == "arch") {
        query = "pacman -Ql " + package;
    } else if (distribution == "fedora") {
        query = "rpm -ql " + package;
    } else {
        query = "dpkg -L " + package;
    }

    // Read through podman: distrobox truncates non-interactive
    // output, and by a different amount each time.
    const ContainerResult listed = Containers::query(name, query);

    binaries.queried = listed.ok;
    binaries.linesSeen = listed.output.size();

    if (!listed.output.empty()) {
        binaries.firstLine = listed.output.front();
    }

    for (const std::string& line : listed.output) {
        // pacman prefixes each line with the package name.
        std::string path = line;

        const std::size_t space = path.find(' ');

        if (distribution == "arch" && space != std::string::npos) {
            path = path.substr(space + 1);
        }

        path = trim(path);

        // Directories are listed too, and a trailing slash is how
        // they announce themselves.
        if (path.empty() || path.back() == '/') {
            continue;
        }

        const bool executable =
            path.rfind("/usr/bin/", 0) == 0 ||
            path.rfind("/usr/sbin/", 0) == 0 ||
            path.rfind("/bin/", 0) == 0;

        if (executable) {
            binaries.paths.push_back(path);
        }
    }

    return binaries;
}

ContainerResult Containers::query(
    const std::string& name,
    const std::string& command
) {
    ContainerResult result;

    if (!commandExists("podman")) {
        result.error = "podman is not installed";
        return result;
    }

    return execute(
        "podman exec '" + name + "' sh -c \"" + command + "\"");
}

ContainerResult Containers::exportBinary(
    const std::string& name,
    const std::string& binaryPath
) {
    return run(
        name,
        "distrobox-export --bin '" + binaryPath +
        "' --export-path \"$HOME/.local/bin\"");
}

}
