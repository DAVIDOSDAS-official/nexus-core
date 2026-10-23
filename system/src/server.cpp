#include <nexus/system/server.hpp>

#include <algorithm>

namespace nexus::system {

bool validServerName(const std::string& name) {
    if (name.empty() || name.size() > 30 || name.front() == '-') {
        return false;
    }
    return std::all_of(name.begin(), name.end(), [](char c) {
        return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-';
    });
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

std::string minecraftQuadlet(const MinecraftOptions& options) {
    const std::string port = std::to_string(options.port);
    return "# Written by nexus server add minecraft. Remove with\n"
           "# `sudo nexus server remove " + options.name + " --apply`;\n"
           "# the world stays in " + serverDataDir(options.name) + ".\n"
           "[Unit]\n"
           "Description=Nexus server: " + options.name + " (Minecraft)\n"
           "\n[Container]\n"
           "ContainerName=nexus-" + options.name + "\n"
           "Image=docker.io/itzg/minecraft-server:latest\n"
           "Label=nexus.server=" + options.name + "\n"
           "Label=nexus.kind=minecraft\n"
           "Environment=EULA=TRUE\n"
           "Environment=TYPE=VANILLA\n"
           "Environment=MEMORY=" + options.memory + "\n"
           "Volume=" + serverDataDir(options.name) + "/data:/data:Z\n"
           "PublishPort=" + port + ":25565/tcp\n" +
           commonTail();
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
        "Image=docker.io/library/caddy:2\n"
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

    return text + commonTail();
}

}
