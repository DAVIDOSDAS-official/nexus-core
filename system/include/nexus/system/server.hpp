#pragma once

#include <cstdint>
#include <string>

namespace nexus::system {

// Servers on this machine (`nexus server`): Minecraft and websites, each
// in its own container, started by systemd.
//
// How they run: one Podman Quadlet file per server,
// /etc/containers/systemd/nexus-<name>.container. systemd turns it
// into nexus-<name>.service at the next daemon-reload, starts it at
// every boot and restarts it when it stops. The container's data lives
// in /var/lib/nexus/servers/<name>, outside the container and outside
// the image, so updates of either leave it alone. Removing a server
// removes the Quadlet file and leaves the data.
//
// Everything here is pure: options in, text out. Writing files,
// opening ports and starting units is the CLI's.

// A name somebody can type and that is safe in a path and a unit name:
// 1-30 of a-z, 0-9 and '-', not starting with '-'.
bool validServerName(const std::string& name);

std::string serverUnit(const std::string& name);         // nexus-<name>.service
std::string serverQuadletPath(const std::string& name);  // /etc/containers/systemd/...
std::string serverDataDir(const std::string& name);      // /var/lib/nexus/servers/<name>

// Memory for a Minecraft server on a machine with totalBytes of RAM:
// about 40% of it, in steps of 256 MB, at least 1024 MB and at most
// 4096 MB -- the rest is for the desktop, which is still running.
// On the Asus (3.6 GB) that is 1280 MB. As "1280M", the form the
// container's MEMORY setting takes.
std::string minecraftMemory(std::uint64_t totalBytes);

struct MinecraftOptions {
    std::string name = "minecraft";
    std::string memory = "1536M";
    int port = 25565;
};

// The Quadlet for itzg/minecraft-server (Java edition). EULA=TRUE is
// written only because the caller has the person's explicit acceptance
// (--accept-eula); this does not decide that.
std::string minecraftQuadlet(const MinecraftOptions& options);

struct WebsiteOptions {
    std::string name = "website";
    int port = 8080;
    // With a domain, Caddy serves it on 80 and 443 and gets an HTTPS
    // certificate for it by itself -- which needs the domain to point
    // at this machine and both ports reachable from the internet.
    std::string domain;
};

// The Quadlet for Caddy serving the files in serverDataDir(name)/site.
std::string websiteQuadlet(const WebsiteOptions& options);

}
