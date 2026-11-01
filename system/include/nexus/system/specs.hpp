#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace nexus::system {

// `nexus specs` -- what this machine is, in one screen: the Nexus
// logo in colour (so the terminal's colours can be seen to work), the
// hardware, and the things only Nexus knows about itself (signed
// image, encryption, firewall, NTSync, add-ons).
//
// Replaces reaching for fastfetch/neofetch, and adds what those
// cannot know. Everything is read from /proc, /sys and os-release, so
// nothing is installed and nothing is run that need not be.
//
// This file is the pure part: it parses file contents and formats
// them. Reading the live machine, the colours and the few things that
// need a command (resolution, the image signature) are the CLI's.

struct DiskUse {
    std::string mount;           // "/", "/home"
    std::uint64_t usedBytes = 0;
    std::uint64_t totalBytes = 0;
};

struct SpecsInfo {
    // Identity
    std::string host;            // hostname
    std::string model;           // "ASUS VivoBook ..." (DMI), or ""
    std::string nexusVersion;    // NEXUS_VERSION (filled by the CLI)
    std::string edition;         // VARIANT_ID: kde, minimal, ...
    std::string osPretty;        // PRETTY_NAME: the Fedora base
    std::string kernel;          // 6.18.3-...
    std::string uptime;          // "3 hours, 12 mins"
    std::string desktop;         // KDE, LXQt, ... (or "")
    std::string resolution;      // "1920x1080" (or "")
    std::string shell;           // bash, zsh (or "")

    // CPU
    std::string cpuModel;
    int cpuCores = 0;

    // GPU: one line each, e.g. "Intel Iris Xe (i915)"
    std::vector<std::string> gpus;

    // Memory, MiB
    std::uint64_t ramUsedMiB = 0;
    std::uint64_t ramTotalMiB = 0;

    std::vector<DiskUse> disks;

    std::string battery;         // "72%, discharging" (or "")

    // What only Nexus knows
    std::string secureBoot;      // "on", "off", "unknown"
    std::string encryption;      // "on (LUKS)", "off", "unknown"
    std::string firewall;        // "on", "off", "unknown"
    bool ntsync = false;         // the gaming add-on's kernel module
    std::vector<std::string> addons;
    std::string signature;       // "signed", "unverified", ""
    std::string lastUpdate;      // "2026-10-28" (or "")

    // Never silently empty: a reading that failed and a thing that is
    // genuinely absent must not look the same.
    std::vector<std::string> unreadable;
};

// --- Pure parsers: file contents in, values out --------------------

// The first "model name" (x86) from /proc/cpuinfo; "" if none.
std::string parseCpuModel(const std::string& cpuinfo);

// How many lines begin with "processor" -- the thread count.
int parseCpuCores(const std::string& cpuinfo);

// MemTotal and MemAvailable from /proc/meminfo, in kibibytes. A field
// that is missing is left at 0.
void parseMemKiB(const std::string& meminfo,
                 std::uint64_t& totalKiB,
                 std::uint64_t& availKiB);

// The whole seconds from the first field of /proc/uptime.
long parseUptimeSeconds(const std::string& uptime);

// "3 days, 4 hours, 5 mins", largest two units that are non-zero;
// "up less than a minute" below 60 s.
std::string formatUptime(long seconds);

// The value of KEY=... from an os-release file, quotes stripped; ""
// if the key is absent.
std::string parseOsReleaseValue(const std::string& osRelease,
                                const std::string& key);

// --- Formatting ----------------------------------------------------

// The whole thing as one JSON object, for scripts and bug reports.
std::string specsJson(const SpecsInfo& info);

}
