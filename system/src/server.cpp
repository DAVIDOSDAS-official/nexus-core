#include <nexus/system/server.hpp>
#include <nexus/system/pins.hpp>

#include <algorithm>
#include <sstream>

namespace nexus::system {

bool validServerName(const std::string& name) {
    if (name.empty() || name.size() > 30 || name.front() == '-') {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
    });
}

UnitRun unitRunState(const std::string& isActiveOutput) {
    // One word is expected. Anything around it (a second line, an
    // error message) is not that word.
    std::string state = isActiveOutput;
    while (!state.empty() && (state.back() == '\n' || state.back() == ' ' ||
                              state.back() == '\r' || state.back() == '\t')) {
        state.pop_back();
    }
    if (state == "inactive" || state == "failed") {
        return UnitRun::Stopped;
    }
    for (const char* running : {"active", "activating", "deactivating",
                                "reloading", "refreshing", "maintenance"}) {
        if (state == running) return UnitRun::Running;
    }
    return UnitRun::Unknown;
}

bool startAgainAfterBackup(const std::string& stateBefore) {
    std::string state = stateBefore;
    while (!state.empty() && (state.back() == '\n' || state.back() == ' ' ||
                              state.back() == '\r' || state.back() == '\t')) {
        state.pop_back();
    }
    return state != "inactive";
}

bool validDomainName(const std::string& name) {
    if (name.empty() || name.size() > 253) {
        return false;
    }
    std::size_t labels = 0;
    std::size_t start = 0;
    while (start <= name.size()) {
        const std::size_t dot = std::min(name.find('.', start), name.size());
        const std::string label = name.substr(start, dot - start);
        if (label.empty() || label.size() > 63 ||
            label.front() == '-' || label.back() == '-') {
            return false;
        }
        for (char c : label) {
            const bool fine = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                              (c >= '0' && c <= '9') || c == '-';
            if (!fine) return false;
        }
        ++labels;
        start = dot + 1;
    }
    return labels >= 1;
}

std::string serverUnit(const std::string& name) {
    return "nexus-" + name + ".service";
}

std::string serverQuadletPath(const std::string& name) {
    return "/etc/containers/systemd/nexus-" + name + ".container";
}

std::string serverDataDir(const std::string& name) {
    return "/var/lib/nexus/servers/" + name;
}

std::string minecraftMemory(std::uint64_t totalBytes) {
    const std::uint64_t mib = totalBytes / (1024 * 1024);
    std::uint64_t want = mib * 40 / 100;
    want = want / 256 * 256;
    want = std::clamp<std::uint64_t>(want, 1024, 4096);
    return std::to_string(want) + "M";
}

namespace {

// Common to every Nexus server: started at boot, restarted when it
// stops, and found again by its label.
std::string commonTail() {
    return "\n[Service]\n"
           "Restart=always\n"
           "TimeoutStartSec=900\n"
           "\n[Install]\n"
           "WantedBy=multi-user.target default.target\n";
}

}  // namespace

bool quadletIsHardened(const std::string& quadlet) {
    std::istringstream in(quadlet);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("UserNS=", 0) == 0) return true;
    }
    return false;
}

std::string hardenQuadlet(const std::string& quadlet, const std::string& kind) {
    const std::string section = "[Container]\n";
    const auto at = quadlet.find(section);
    if (at == std::string::npos || quadletIsHardened(quadlet)) return quadlet;

    std::string text = quadlet;
    // Folders the server writes to: owned by its uids, not root's.
    for (std::size_t pos = 0; (pos = text.find(":/data:Z\n", pos)) != std::string::npos;) {
        text.replace(pos, 9, ":/data:Z,U\n");
        pos += 11;
    }
    const bool dataVolume = text.find(":/data:Z,U\n") != std::string::npos;

    std::string lines =
        "# 0.2.7: its own uids; root in here is nobody on the host.\n"
        "UserNS=auto\n"
        "NoNewPrivileges=true\n";
    if (kind == "website") {
        lines +=
            "DropCapability=ALL\n"
            "AddCapability=NET_BIND_SERVICE\n"
            "ReadOnly=true\n"
            "Tmpfs=/config\n";
        // Caddy's own storage: the certificates' folder with a domain,
        // a scratch one without.
        if (!dataVolume) lines += "Tmpfs=/data\n";
    }
    text.insert(at + section.size(), lines);
    return text;
}

SubidPlan containersSubidPlan(const std::string& fileText) {
    SubidPlan plan;
    const long long start = kContainersSubidStart;
    const long long end = start + kContainersSubidCount;  // exclusive
    std::istringstream in(fileText);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("containers:", 0) == 0) return plan;  // there already
        const auto a = line.find(':');
        const auto b = a == std::string::npos ? a : line.find(':', a + 1);
        if (b == std::string::npos) continue;
        try {
            const long long s = std::stoll(line.substr(a + 1, b - a - 1));
            const long long c = std::stoll(line.substr(b + 1));
            if (s < end && start < s + c) {
                plan.conflict = line;
                return plan;
            }
        } catch (...) {
            continue;
        }
    }
    plan.needed = true;
    plan.line = (!fileText.empty() && fileText.back() != '\n' ? "\n" : std::string()) +
                "containers:" + std::to_string(start) + ":" +
                std::to_string(kContainersSubidCount) + "\n";
    return plan;
}

std::string minecraftQuadlet(const MinecraftOptions& options) {
    const std::string port = std::to_string(options.port);
    return hardenQuadlet("# Written by nexus server add minecraft. Remove with\n"
           "# `sudo nexus server remove " + options.name + " --apply`;\n"
           "# the world stays in " + serverDataDir(options.name) + ".\n"
           "[Unit]\n"
           "Description=Nexus server: " + options.name + " (Minecraft)\n"
           "\n[Container]\n"
           "ContainerName=nexus-" + options.name + "\n"
           "Image=" + pinnedImage("docker.io/itzg/minecraft-server:latest") + "\n"
           "Label=nexus.server=" + options.name + "\n"
           "Label=nexus.kind=minecraft\n"
           "Environment=EULA=TRUE\n"
           "Environment=TYPE=VANILLA\n"
           "Environment=MEMORY=" + options.memory + "\n"
           "Volume=" + serverDataDir(options.name) + "/data:/data:Z\n"
           "PublishPort=" + port + ":25565/tcp\n" +
           commonTail(), "minecraft");
}

std::string websiteQuadlet(const WebsiteOptions& options) {
    const std::string site = serverDataDir(options.name) + "/site";
    std::string text =
        "# Written by nexus server add website. Remove with\n"
        "# `sudo nexus server remove " + options.name + " --apply`;\n"
        "# the files stay in " + site + ".\n"
        "[Unit]\n"
        "Description=Nexus server: " + options.name + " (website)\n"
        "\n[Container]\n"
        "ContainerName=nexus-" + options.name + "\n"
        "Image=" + pinnedImage("docker.io/library/caddy:2") + "\n"
        "Label=nexus.server=" + options.name + "\n"
        "Label=nexus.kind=website\n"
        "Volume=" + site + ":/srv:ro,Z\n";

    if (options.domain.empty()) {
        text += "Exec=caddy file-server --root /srv --listen :80\n"
                "PublishPort=" + std::to_string(options.port) + ":80/tcp\n";
    } else {
        // Caddy keeps its certificates in /data; they must survive a
        // restart or it asks for new ones every time (and is limited).
        text += "Volume=" + serverDataDir(options.name) + "/caddy:/data:Z\n"
                "Exec=caddy file-server --root /srv --domain " +
                options.domain + "\n"
                "PublishPort=80:80/tcp\n"
                "PublishPort=443:443/tcp\n";
    }

    return hardenQuadlet(text + commonTail(), "website");
}

}
