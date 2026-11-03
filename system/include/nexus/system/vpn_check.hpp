#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace nexus::system {

// `nexus vpn check` (0.1.64): is a VPN carrying this machine's traffic,
// does DNS leave outside it, and would traffic stop if it dropped?
//
// Read-only, and nothing is sent anywhere: everything comes from the
// kernel's routing (`ip route get`), the interfaces (`ip -o -d link`),
// systemd-resolved (`resolvectl status`), the firewall (`nft list
// ruleset`, root only) and the VPN apps' own status commands. The
// address websites see is NOT checked -- that needs asking a server.
//
// This file is the pure part: parsing and judging. Running the
// commands is the CLI's, so every rule here is tested.

struct NetLink {
    std::string name;
    bool up = false;
    bool wireguard = false;   // kernel type wireguard
    bool tun = false;         // tun/tap device (OpenVPN, Tailscale)
};

// `ip -o -d link show`
std::vector<NetLink> parseLinks(const std::string& text);

// The device after "dev" in `ip route get <address>`; "" when there is
// no route (e.g. "Network is unreachable").
std::string parseRouteDev(const std::string& text);

struct DnsLink {
    std::string name;
    std::vector<std::string> servers;
    bool defaultRoute = false;   // +DefaultRoute
    bool catchAll = false;       // routing domain "~."
};

struct DnsView {
    bool resolved = false;                 // systemd-resolved answered
    std::vector<std::string> globalServers;
    std::vector<DnsLink> links;
};

// `resolvectl status`
DnsView parseResolvectl(const std::string& text);

// nameserver lines of /etc/resolv.conf
std::vector<std::string> parseResolvConf(const std::string& text);

// Which servers name lookups actually go to: the links with "~." if
// any has it (resolved then sends everything there), otherwise every
// +DefaultRoute link plus the global servers. server -> link ("" for
// global).
std::vector<std::pair<std::string, std::string>> dnsInUse(const DnsView& view);

// What kind of VPN an interface is, by kernel type and name:
// "Mullvad", "Proton VPN", "Tailscale", "WireGuard", "OpenVPN", or ""
// when it is not a tunnel.
std::string tunnelKind(const NetLink& link);

// A firewall rule that blocks traffic leaving outside the tunnel:
// `oifname != "wg0" ... reject|drop` (what wg-quick's documented kill
// switch turns into under nftables), or a VPN app's own table.
bool rulesetBlocksOutside(const std::string& ruleset, const std::string& tunnel);

// `mullvad lockdown-mode get` -> "on" / "off" / "" (unknown)
std::string parseMullvadLockdown(const std::string& text);

// Everything the check looked at.
struct VpnFacts {
    std::vector<NetLink> links;
    std::string route4;                   // device for 1.1.1.1
    std::string route6;                   // device for an IPv6 address
    bool ipv6Reachable = false;
    DnsView dns;
    std::map<std::string, std::string> dnsRoute;   // server -> device
    bool rulesetRead = false;             // root could read nft
    std::string ruleset;
    std::string mullvadLockdown;          // "on" / "off" / ""
    std::optional<bool> tailscaleExitNode;
    std::string configPath;               // /etc/wireguard/<tunnel>.conf if present
};

enum class VpnHealth { Ok, Warning, Problem, Info };

struct VpnLine {
    std::string check;
    VpnHealth health = VpnHealth::Info;
    std::string detail;
    std::string suggestion;
};

struct VpnReport {
    std::string tunnel;       // device carrying IPv4, if a tunnel; else ""
    std::string kind;         // "Mullvad", "WireGuard", ...
    std::vector<VpnLine> lines;
};

VpnReport assessVpn(const VpnFacts& facts);

}  // namespace nexus::system
