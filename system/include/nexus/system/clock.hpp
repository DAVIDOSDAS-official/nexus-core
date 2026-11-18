#pragma once

#include <optional>
#include <string>

namespace nexus::system {

// The clock and the time zone, for `nexus doctor` (0.1.61).
//
// The pure part: parsing what chrony and timedatectl print, and the
// words for it. Running the commands is the CLI's.

// How far the system clock is from network time, from `chronyc
// tracking`'s "System time" line:
//
//     System time     : 259200.123456789 seconds slow of NTP time
//
// Positive when the clock is behind ("slow"), negative when ahead
// ("fast"). Empty when there is no such line or it cannot be read --
// never 0 for "unknown".
std::optional<double> parseChronyOffset(const std::string& tracking);

// "3 days", "2 hours", "45 seconds": the larger unit only, rounded
// down, for a sentence. The sign is ignored.
std::string describeOffset(double seconds);

// Whether chrony is following a time server, from `chronyc -n
// tracking` (0.2.4). "Leap status : Not synchronised" -- and the
// reference 00000000 that goes with it -- means no server has
// answered: the clock runs on the hardware clock and the oscillator
// alone. Empty when the text says neither (chrony not running).
std::optional<bool> chronyFollowsServer(const std::string& tracking);

// The hardware clock's time, from /sys/class/rtc/rtc0/since_epoch:
// seconds since 1970 as the hardware clock reads, taken as universal
// time (0.2.4). Readable without root. Empty when it is not a number.
std::optional<long long> parseRtcSinceEpoch(const std::string& text);

// What Fedora's installer leaves when no time zone is chosen.
inline constexpr const char* kInstallerDefaultTimezone = "America/New_York";

// True when the zone is the installer's default and nobody said it
// is right (`nexus-timezone` writes the marker when a zone is set or
// kept).
bool timezoneNotChosen(const std::string& zone, bool chosenMarkerExists);

}  // namespace nexus::system
