#pragma once

#include <string>
#include <vector>

namespace nexus::system {

enum class ServiceState {
    Enabled,     // starts at boot
    Disabled,    // present, will not start
    Static,      // no enable/disable of its own; pulled in by others
    Masked,      // cannot start, deliberately
    Generated,   // produced at runtime from something else
    Alias,       // another name for a unit
    Indirect,
    Unknown
};

struct Service {
    std::string name;
    ServiceState state = ServiceState::Unknown;
    std::string preset;

    // Whether it is running now. Only known when systemd is running;
    // unit files can be read from disk without it.
    bool active = false;
    bool failed = false;

    // Stuck starting.
    //
    // A unit with Restart=always never reaches "failed": systemd
    // retries it forever, so it is permanently activating instead.
    // Checking only for failure never sees it, and a service
    // respawning every five seconds since boot is as broken as one
    // that gave up.
    bool restarting = false;

    // What installed it, when that can be told. This is what makes a
    // service part of the model rather than a fact about systemd: a
    // service belongs to a component, and removing the component
    // takes the service with it.
    std::string owner;

    // Starts at boot, which is the question people mean by "what
    // runs on this machine".
    bool startsAtBoot() const {
        return state == ServiceState::Enabled;
    }
};

struct ServicesResult {
    std::vector<Service> services;

    std::size_t enabled = 0;
    std::size_t failedCount = 0;
    std::size_t restartingCount = 0;

    bool systemdRunning = false;

    std::string error;
};

// What this machine runs, by asking systemd.
//
// Unit files are read from disk, so this works on a machine where
// systemd is installed but not running -- a container, an image being
// built, a chroot. Whether something is running *now* needs a running
// systemd, and is reported as unknown rather than guessed when there
// is not one.
ServicesResult readServices();

// Fill in which component installed each unit.
//
// This is what makes a service part of the model rather than a fact
// about systemd. A service belongs to a component: removing the
// component takes the service with it, and a service whose component
// nobody asked for is a service running for no stated reason.
//
// One query for everything rather than one per service. On 261 units
// that is the difference between a command and a wait.
void attachOwners(ServicesResult& result, bool useRpm = false);

// Exposed for testing against captured output.
void applyDpkgOwners(ServicesResult& result, const std::string& text);

// Exposed for testing against captured output.
ServicesResult parseUnitFiles(const std::string& text);

void applyFailedUnits(ServicesResult& result, const std::string& text);

void applyRestartingUnits(
    ServicesResult& result,
    const std::string& text
);

std::string toString(ServiceState state);

ServiceState parseServiceState(const std::string& text);

}
