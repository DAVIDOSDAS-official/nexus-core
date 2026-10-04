#pragma once

// nexus update: is there something new, and taking it.
//
// How updates work on Nexus (decided 4 October): the machine checks by
// itself once a day and only says so -- a notification -- and nothing
// is downloaded or changed until the person asks (`sudo nexus update
// --apply`, later a button in the Shop). It never restarts by itself.
//
// The check is rpm-ostree's own (AutomaticUpdatePolicy=check): its
// result shows in `rpm-ostree status`, which anybody can read, so
// looking needs no password. bootc's timer is off: it applies and
// restarts, and with any package added on the machine it fails
// quietly ("cannot upgrade via bootc", Asus, 4 October).
//
// Pure functions over command output, tested without the commands.

#include <string>
#include <vector>

namespace nexus::system {

struct SystemUpdate {
    bool available = false;    // an AvailableUpdate block was there
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

}
