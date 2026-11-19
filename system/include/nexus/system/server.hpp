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
// What `systemctl is-active <unit>` printed, as the one thing a backup
// or a removal needs to know before it touches a server's files
// (0.1.88, review of patch 113):
//   Stopped  "inactive" or "failed": nothing of it is running
//   Running  "active", and every state on the way in or out of it
//            ("activating", "deactivating", "reloading", "refreshing",
//            "maintenance"): its programs may be writing
//   Unknown  anything else, an empty answer included: not known to be
//            stopped, so treated as not stopped
enum class UnitRun { Stopped, Running, Unknown };
UnitRun unitRunState(const std::string& isActiveOutput);

// Whether a server in this state before a backup is started again
// after it. Not when it was plainly "inactive": someone stopped it.
// One that had failed, was starting, or gave no answer is started, as
// every server was before 0.1.87.
bool startAgainAfterBackup(const std::string& stateBefore);

// A domain name as it goes onto Caddy's command line: labels of letters,
// digits and hyphens, joined by dots; no label starts or ends with a
// hyphen (so it can never read as an option) and none is empty. At
// most 253 characters.
bool validDomainName(const std::string& name);

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

// 0.2.7 (outside audit S-1, ledger item 7; on the Asus 19 Nov the
// website's Caddy ran as root on the host): every server in its own
// user namespace, so root in the container is an unprivileged uid on
// the host, with no gaining of privileges. The website also loses every
// capability but binding a port, and its file system is read-only.
// Folders it writes to are given to its uids (",U").
//
// `kind` is "minecraft" or "website". A Quadlet that has UserNS= already
// comes back unchanged; one with no [Container] section too.
std::string hardenQuadlet(const std::string& quadlet, const std::string& kind);

// Whether a Quadlet has had hardenQuadlet (it has a UserNS= line).
bool quadletIsHardened(const std::string& quadlet);

// UserNS=auto takes its uids from the "containers" lines of /etc/subuid
// and /etc/subgid; Fedora writes none. The range podman's manual uses,
// above every uid a person gets.
inline constexpr long long kContainersSubidStart = 2147483647LL;
inline constexpr long long kContainersSubidCount = 2147483648LL;

struct SubidPlan {
    bool needed = false;   // no "containers:" line yet
    std::string line;      // what to append, with its newline
    std::string conflict;  // a line whose range overlaps: nothing is added
};

// What /etc/subuid or /etc/subgid needs. A file that ends without a
// newline gets one before the line.
SubidPlan containersSubidPlan(const std::string& fileText);

}
