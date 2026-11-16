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
    int wanted = 0;      // value written by harden
    bool atLeast = false; // true: any value >= wanted is fine
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

// ---------------------------------------------------------------------
// "Starts by itself" (0.2.0, ledger R4-L2).
//
// Until 0.1.99 this looked in two places, ~/.config/autostart and the
// .service and .timer files of ~/.config/systemd/user, and said "ok"
// about everything else. A .socket or .path unit, a drop-in that
// rewrites another unit's command, a unit switched on by a link in
// default.target.wants, a script KDE runs at login, a line in .bashrc:
// none of them was seen.
//
// What is read is names, never contents: this says what is there, it
// does not judge it. Folders are listed, at most kAutoStartFolderLimit
// names each, and a folder that turns out to be somewhere other than
// under the home folder (a link leading out of it) is not listed at
// all -- under sudo that would be root reading out a folder the person
// could not read themselves.
inline constexpr std::size_t kAutoStartFolderLimit = 500;

struct AutoStarts {
    // What is set up to start without being asked, sorted:
    // "foo.desktop", "sync.timer", "ssh-agent.service.d/override.conf",
    // "default.target.wants/sync.service", "autostart-scripts/x.sh",
    // "plasma-workspace/env/path.sh", "environment.d/10-vars.conf"...
    std::vector<std::string> starts;
    // Shell and login files that exist: ".bashrc", ".profile"... They
    // run in every new shell or at every login. Named, not judged:
    // nearly everyone has a .bashrc.
    std::vector<std::string> shellFiles;
    // Places that are there but were not, or not fully, read, each
    // with why: "~/.config/autostart (a link that leads outside your
    // home folder)".
    std::vector<std::string> notRead;
};

// `home` is the person's home folder. Reads folder listings only.
AutoStarts findAutoStarts(const std::string& home);

// The audit's lines for it, without the "[ ok ]  Starts by itself"
// in front: the first is the finding, the rest follow under it. The
// last always says what was not looked at. `nothingFound` is true
// when `starts` is empty and every place could be read.
std::vector<std::string> autoStartLines(const AutoStarts& found, bool& nothingFound);

// ---------------------------------------------------------------------
// Firewall commands the audit suggests (0.2.0, ledger item 11).
//
// Until 0.1.99 they were "firewall-cmd --permanent <change> &&
// firewall-cmd --reload". --reload throws away every rule that exists
// only in the running firewall: a VPN's, a game's, one the person
// added for this session. Now the change is made twice, in the saved
// rules and in the running ones, and nothing is reloaded.
//
// change: "--remove-port=1025-65535/tcp", "--remove-service=ssh",
// "--change-interface=<name>"... zone: the zone it applies to.
std::string firewallFix(const std::string& zone, const std::string& change);

// The same rule for what Nexus changes itself (0.2.2, ledger item 13):
// `nexus server add | remove | cockpit --apply` saved a port or service
// and then ran `firewall-cmd --reload`, dropping every rule that existed
// only in the running firewall -- a VPN's, a game's -- as root. Now: the
// two argument vectors to run, saved rules first, running ones second.
// No zone: the default zone, for both, as before.
std::vector<std::vector<std::string>> firewallChanges(const std::string& change);

// The one suggestion that is meant to bring the saved rules back, with
// what it costs said beside it.
std::string firewallBackToSaved();

}  // namespace nexus::system
