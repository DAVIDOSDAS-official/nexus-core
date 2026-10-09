#pragma once

// nexus update: is there something new, and taking it.
//
// How updates work on Nexus (decided 4 October): the machine checks by
// itself once a day and only says so -- a notification -- and nothing
// is downloaded or changed until the person asks (`sudo nexus update
// --apply`, later a button in the Shop). It never restarts by itself.
//
// The check compares fingerprints: the Digest of the image this
// machine runs (`rpm-ostree status`) against the Digest the registry
// has now (`skopeo inspect`, no password, no download). rpm-ostree's
// own --check was tried first and is not used: on an image-based
// machine it looks at added packages, and it said "No updates
// available" with a new Nexus image on the registry (Asus, 4 October).
// bootc's timer is off: it applies and restarts by itself, and with
// any package added on the machine it fails quietly ("cannot upgrade
// via bootc", Asus, 4 October).
//
// Pure functions over command output, tested without the commands.

#include <string>
#include <vector>

namespace nexus::system {

struct SystemUpdate {
    std::string origin;        // ghcr.io/x/nexus-core:minimal
    std::string digest;        // what the booted deployment runs
    std::string stagedDigest;
    bool available = false;    // the registry has something newer
    std::string version;       // 44.20261005.0
    std::string date;          // 2026-10-05T...
    std::string diff;          // rpm-ostree's one-line summary, if any
    bool staged = false;       // a new deployment waits for a restart
    std::string stagedVersion;
    std::string lastCheck;     // "rpm-ostreed-automatic.timer: last run 4h ago"
    std::string policy;        // "check", "stage", "disabled", ...
};

// Reads `rpm-ostree status` text.
SystemUpdate parseSystemUpdate(const std::string& statusText);

// "ostree-image-signed:docker://ghcr.io/x/y:tag" -> "ghcr.io/x/y:tag".
// Empty when the origin is not a container image.
std::string imageReference(const std::string& origin);

struct RemoteImage {
    bool ok = false;
    std::string digest;
    std::string created;       // 2026-10-04T14:18:53.528859441Z
    std::string nexusVersion;  // the nexus.version label, if there
};

// Reads `skopeo inspect --no-tags docker://...` (JSON).
RemoteImage parseImageInspect(const std::string& json);

// Fills in available / version / date from what the registry has:
// newer when its digest is neither the running one nor the staged one.
void compareWithRegistry(SystemUpdate& system, const RemoteImage& remote);

struct AppUpdate {
    std::string id;       // org.mozilla.firefox
    std::string version;  // may be empty
};

// Reads `flatpak remote-ls --updates --app --columns=application,version`.
std::vector<AppUpdate> parseFlatpakUpdates(const std::string& text);

// One line for a notification, or empty when there is nothing new:
// "Nexus 44.20261005.0 and 2 app updates".
std::string updateSummary(const SystemUpdate& system,
                          const std::vector<AppUpdate>& apps);

// What `nexus update --lines` prints, read back by Nexus Shop. One
// fact per line, tab-separated, first field the kind of line:
//   running <version> <day>
//   staged  <version>
//   system  new|current|unreachable|not-image <version> <day> <diff>
//   app     <id> <version>
// Unknown kinds are skipped, so a newer nexus can add lines without
// breaking an older Shop.
struct UpdateLines {
    std::string running;
    std::string runningDay;
    std::string staged;
    std::string system;   // empty when no system line was printed
    std::string version;
    std::string day;
    std::string diff;
    std::vector<AppUpdate> apps;
};

UpdateLines parseUpdateLines(const std::string& text);

}
