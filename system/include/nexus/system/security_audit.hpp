#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace nexus::system {

// `nexus security` (0.1.75): the honest core of "Nexus antivirus".
//
// On Linux the real risks are rarely a virus file. They are a setting
// left open, a service listening to the network, an update not taken,
// something set to start by itself. This reads those, says green /
// yellow / red with one fix per line, and changes nothing. One
// low-breakage tier of kernel settings can be switched on with
// `nexus security harden --apply` and off again.
//
// The pure part: the hardening table and the parsers. Reading the
// machine is the CLI's.

struct HardeningSetting {
    std::string key;     // kernel.kptr_restrict
    int wanted;          // value written by harden
    bool atLeast;        // true: any value >= wanted is fine
    std::string why;     // one line
    int alsoFine = -1;   // another value that is just as good (rp_filter: strict 1)
};

// The default tier: settings Kicksecure and secureblue apply without
// breaking a desktop. Not ptrace or user-namespace limits -- those
// break debuggers, browsers' sandboxes or Flatpak for some people, so
// they are not "invisible".
const std::vector<HardeningSetting>& hardeningTable();

inline constexpr const char* kHardeningConf = "/etc/sysctl.d/90-nexus-hardening.conf";

// Which settings are weaker than the table wants, given what
// /proc/sys says (key -> value; a key missing from the map is skipped).
std::vector<HardeningSetting> weakSettings(const std::map<std::string, int>& now);

// The file `harden --apply` writes, for the given settings.
std::string hardeningFile(const std::vector<HardeningSetting>& settings);

// `ss -tulnH` -> what listens to the network (not only to this
// machine): "tcp 0.0.0.0:80", "udp [::]:5353". Loopback is left out.
std::vector<std::string> parseListeners(const std::string& ssOutput);

// Accounts with an empty password field in /etc/shadow (anyone can log
// in as them where passwords are accepted). "!", "*", "!!" are locked.
std::vector<std::string> emptyPasswordAccounts(const std::string& shadow);

// Days from "2026-11-08T20:00:19Z" to now (epoch seconds); empty if
// unreadable.
std::optional<long> daysSince(const std::string& isoDate, long nowEpoch);

// 0.1.76, after an outside review said where the audit claimed more
// than it checked.

// `firewall-cmd --get-active-zones`: a zone name at the start of a
// line, its interfaces and sources indented below it.
std::vector<std::string> parseActiveZones(const std::string& output);

// A port list that opens everything above 1024 (Fedora Workstation's
// zone does: 1025-65535/tcp and udp). Returns the range found, or "".
std::string wideOpenRange(const std::string& ports);

// Whether /etc/containers/policy.json makes Nexus's images need a
// signature made with the given key: a sigstoreSigned rule naming the
// key, under the Nexus scope. A text check, not a JSON parser -- it
// looks for the scope, then the rule and the key before the next
// scope begins.
bool policyRequiresKey(const std::string& policyJson, const std::string& scope,
                       const std::string& keyPath);

// `firewall-cmd [--permanent] --info-zone=Z` (0.1.77): the zone as it
// runs now, or as saved. The second outside review: the target was
// read from the saved config and the services and ports from the
// running one, and the two can differ.
struct ZoneInfo {
    std::string target;     // default, ACCEPT, DROP, %%REJECT%%
    std::string services;
    std::string ports;
    std::string interfaces;
    std::string sources;
};
ZoneInfo parseZoneInfo(const std::string& output);

// A zone that lets everything in matters by what is in it (the Asus,
// 9 November: "trusted" was FAIL without saying for what). Interfaces
// of the machine's own containers and VMs are one thing, a VPN tunnel
// another, a real network or an address range something else again.
enum class TrustedReach { Nothing, Containers, Tunnel, Network };

// An address on one of this machine's interfaces: `ip -o -f inet addr
// show` gives "3: podman0    inet 10.88.0.1/16 brd ...".
struct LocalNetwork {
    std::string interface;
    std::string cidr;   // 10.88.0.1/16
};
std::vector<LocalNetwork> parseIpAddresses(const std::string& output);

// Sources are judged too (the Asus, 9 November: podman's network,
// 10.88.0.0/16, is put in "trusted" by netavark so containers reach the
// host -- 0.1.78 called that a real network). A source is the
// machine's own containers when it is the network of a container
// bridge here, or podman's or docker's default network.
TrustedReach trustedReach(const ZoneInfo& zone, const std::vector<LocalNetwork>& local = {});

}  // namespace nexus::system
