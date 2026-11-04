#include <gtest/gtest.h>

#include <nexus/system/vpn_check.hpp>

using namespace nexus::system;

namespace {

const char* kLinks =
    "1: lo: <LOOPBACK,UP,LOWER_UP> mtu 65536 qdisc noqueue state UNKNOWN mode DEFAULT group default qlen 1000\\    link/loopback 00:00:00:00:00:00 brd 00:00:00:00:00:00 promiscuity 0\n"
    "3: wlo1: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 qdisc noqueue state UP mode DORMANT group default qlen 1000\\    link/ether aa:bb:cc:dd:ee:ff brd ff:ff:ff:ff:ff:ff promiscuity 0\n"
    "6: wg0: <POINTOPOINT,NOARP,UP,LOWER_UP> mtu 1420 qdisc noqueue state UNKNOWN mode DEFAULT group default qlen 1000\\    link/none  promiscuity 0 \\    wireguard addrgenmode none\n"
    "7: tailscale0: <POINTOPOINT,MULTICAST,NOARP,UP,LOWER_UP> mtu 1280 qdisc fq_codel state UNKNOWN\\    link/none  promiscuity 0 \\    tun type tun pi off vnet_hdr on persist off\n"
    "8: veth1@if2: <BROADCAST,MULTICAST,UP,LOWER_UP> mtu 1500 state UP\\    link/ether 00:11:22:33:44:55 \\    veth\n"
    "9: tun0: <POINTOPOINT,MULTICAST,NOARP> mtu 1500 state DOWN\\    link/none \\    tun type tun\n";

const char* kResolvedLeak =
    "Global\n"
    "         Protocols: LLMNR=resolve -mDNS -DNSOverTLS DNSSEC=no/unsupported\n"
    "  resolv.conf mode: stub\n"
    "\n"
    "Link 3 (wlo1)\n"
    "    Current Scopes: DNS LLMNR/IPv4 LLMNR/IPv6\n"
    "         Protocols: +DefaultRoute LLMNR=resolve -mDNS -DNSOverTLS DNSSEC=no/unsupported\n"
    "Current DNS Server: 192.168.0.1\n"
    "       DNS Servers: 192.168.0.1\n"
    "\n"
    "Link 6 (wg0)\n"
    "    Current Scopes: none\n"
    "         Protocols: -DefaultRoute LLMNR=resolve -mDNS -DNSOverTLS DNSSEC=no/unsupported\n";

const char* kResolvedTunnel =
    "Global\n"
    "         Protocols: LLMNR=resolve -mDNS -DNSOverTLS\n"
    "\n"
    "Link 3 (wlo1)\n"
    "         Protocols: +DefaultRoute LLMNR=resolve\n"
    "       DNS Servers: 192.168.0.1\n"
    "\n"
    "Link 6 (wg0)\n"
    "         Protocols: +DefaultRoute LLMNR=resolve\n"
    "Current DNS Server: 10.64.0.1\n"
    "       DNS Servers: 10.64.0.1\n"
    "                    fc00:bbbb:bbbb:bb01::1\n"
    "        DNS Domain: ~.\n";

VpnFacts wireguardFacts() {
    VpnFacts f;
    f.links = parseLinks(kLinks);
    f.route4 = "wg0";
    f.ipv6Reachable = true;
    f.route6 = "wg0";
    f.dns = parseResolvectl(kResolvedTunnel);
    f.dnsRoute = {{"10.64.0.1", "wg0"}, {"fc00:bbbb:bbbb:bb01::1", "wg0"}};
    f.rulesetRead = true;
    f.ruleset =
        "table inet filter {\n"
        "  chain output {\n"
        "    oifname != \"wg0\" meta mark != 0x0000ca6c fib daddr type != local counter packets 0 bytes 0 reject\n"
        "  }\n"
        "}\n";
    f.configPath = "/etc/wireguard/wg0.conf";
    return f;
}

const VpnLine& line(const VpnReport& r, const std::string& check) {
    for (const auto& l : r.lines) if (l.check == check) return l;
    static VpnLine none{"(missing)", VpnHealth::Info, "", ""};
    return none;
}

}  // namespace

TEST(VpnCheckTest, ParsesLinksTypesAndState) {
    const auto links = parseLinks(kLinks);
    ASSERT_EQ(links.size(), 6u);
    EXPECT_EQ(links[2].name, "wg0");
    EXPECT_TRUE(links[2].wireguard);
    EXPECT_TRUE(links[2].up);
    EXPECT_TRUE(links[3].tun);
    EXPECT_EQ(links[4].name, "veth1");          // "@if2" dropped
    EXPECT_FALSE(links[5].up);                    // tun0 is down
    EXPECT_EQ(tunnelKind(links[1]), "");          // wlo1
    EXPECT_EQ(tunnelKind(links[2]), "WireGuard");
    EXPECT_EQ(tunnelKind(links[3]), "Tailscale");
    EXPECT_EQ(tunnelKind(links[5]), "OpenVPN");
}

TEST(VpnCheckTest, KnowsTheProvidersByName) {
    NetLink m{"wg0-mullvad", true, true, false};
    NetLink p{"proton0", true, true, false};
    NetLink ks{"pvpnksintrf0", true, false, false};
    EXPECT_EQ(tunnelKind(m), "Mullvad");
    EXPECT_EQ(tunnelKind(p), "Proton VPN");
    EXPECT_EQ(tunnelKind(ks), "");   // Proton's kill-switch helper, not a tunnel
}

TEST(VpnCheckTest, RouteDevice) {
    EXPECT_EQ(parseRouteDev("1.1.1.1 dev wg0 table 51820 src 10.64.1.2 uid 1000 \n    cache \n"), "wg0");
    EXPECT_EQ(parseRouteDev("1.1.1.1 via 192.168.0.1 dev wlo1 src 192.168.0.10 uid 1000\n"), "wlo1");
    EXPECT_EQ(parseRouteDev("RTNETLINK answers: Network is unreachable\n"), "");
}

TEST(VpnCheckTest, ResolvectlCatchAllWins) {
    const auto view = parseResolvectl(kResolvedTunnel);
    ASSERT_TRUE(view.resolved);
    ASSERT_EQ(view.links.size(), 2u);
    EXPECT_TRUE(view.links[1].catchAll);
    ASSERT_EQ(view.links[1].servers.size(), 2u);   // IPv6 continuation line
    const auto used = dnsInUse(view);
    ASSERT_EQ(used.size(), 2u);
    EXPECT_EQ(used[0].first, "10.64.0.1");
    EXPECT_EQ(used[0].second, "wg0");
}

TEST(VpnCheckTest, ResolvectlWithoutCatchAllUsesDefaultRouteLinks) {
    const auto used = dnsInUse(parseResolvectl(kResolvedLeak));
    ASSERT_EQ(used.size(), 1u);
    EXPECT_EQ(used[0].first, "192.168.0.1");
}

TEST(VpnCheckTest, ResolvConfAndStubIgnored) {
    DnsView v;
    v.globalServers = parseResolvConf("# generated\nnameserver 127.0.0.53\nnameserver 9.9.9.9\noptions edns0\n");
    const auto used = dnsInUse(v);
    ASSERT_EQ(used.size(), 1u);
    EXPECT_EQ(used[0].first, "9.9.9.9");
}

TEST(VpnCheckTest, AllGoodWireGuard) {
    const auto r = assessVpn(wireguardFacts());
    EXPECT_EQ(r.tunnel, "wg0");
    EXPECT_EQ(line(r, "VPN").health, VpnHealth::Ok);
    EXPECT_EQ(line(r, "IPv6").health, VpnHealth::Ok);
    EXPECT_EQ(line(r, "DNS").health, VpnHealth::Ok);
    EXPECT_EQ(line(r, "Kill switch").health, VpnHealth::Ok);
}

TEST(VpnCheckTest, IPv6LeakIsAProblem) {
    auto f = wireguardFacts();
    f.route6 = "wlo1";
    const auto r = assessVpn(f);
    EXPECT_EQ(line(r, "IPv6").health, VpnHealth::Problem);
    EXPECT_NE(line(r, "IPv6").suggestion.find("::/0"), std::string::npos);
}

TEST(VpnCheckTest, NoIPv6RouteIsNoLeak) {
    auto f = wireguardFacts();
    f.ipv6Reachable = false; f.route6.clear();
    EXPECT_EQ(line(assessVpn(f), "IPv6").health, VpnHealth::Ok);
}

TEST(VpnCheckTest, DnsLeakIsAProblem) {
    auto f = wireguardFacts();
    f.dns = parseResolvectl(kResolvedLeak);
    f.dnsRoute = {{"192.168.0.1", "wlo1"}};
    const auto r = assessVpn(f);
    EXPECT_EQ(line(r, "DNS").health, VpnHealth::Problem);
    EXPECT_NE(line(r, "DNS").detail.find("192.168.0.1 through wlo1"), std::string::npos);
}

TEST(VpnCheckTest, KillSwitchOffAndUnreadable) {
    auto f = wireguardFacts();
    f.ruleset = "table inet firewalld {\n chain x {\n  oifname \"wlo1\" accept\n }\n}\n";
    EXPECT_EQ(line(assessVpn(f), "Kill switch").health, VpnHealth::Warning);
    f.rulesetRead = false;
    const auto report = assessVpn(f);
    const auto& ks = line(report, "Kill switch");
    EXPECT_EQ(ks.health, VpnHealth::Info);
    EXPECT_EQ(ks.suggestion, "sudo nexus vpn check");
}

TEST(VpnCheckTest, MullvadLockdown) {
    EXPECT_EQ(parseMullvadLockdown("Block traffic when the VPN is disconnected: on\n"), "on");
    EXPECT_EQ(parseMullvadLockdown("Block traffic when the VPN is disconnected: off\n"), "off");
    EXPECT_EQ(parseMullvadLockdown("Error: daemon not running"), "");

    VpnFacts f;
    f.links = {{"wlo1", true, false, false}, {"wg0-mullvad", true, true, false}};
    f.route4 = "wg0-mullvad";
    f.mullvadLockdown = "off";
    const auto r = assessVpn(f);
    EXPECT_EQ(r.kind, "Mullvad");
    EXPECT_EQ(line(r, "Kill switch").suggestion, "mullvad lockdown-mode set on");
}

TEST(VpnCheckTest, NoVpnAndTailscaleWithoutExitNode) {
    VpnFacts f;
    f.links = parseLinks(kLinks);
    f.route4 = "wlo1";
    // wg0 and tailscale0 are up but carry nothing: a warning
    auto r = assessVpn(f);
    EXPECT_EQ(r.tunnel, "");
    EXPECT_EQ(line(r, "VPN").health, VpnHealth::Warning);

    // only tailscale up: normal, said as information
    f.links = {{"wlo1", true, false, false}, {"tailscale0", true, false, true}};
    r = assessVpn(f);
    EXPECT_EQ(line(r, "VPN").health, VpnHealth::Info);
    EXPECT_NE(line(r, "VPN").detail.find("no exit node"), std::string::npos);

    // nothing at all
    f.links = {{"wlo1", true, false, false}};
    r = assessVpn(f);
    EXPECT_NE(line(r, "VPN").detail.find("None active"), std::string::npos);
    EXPECT_EQ(r.lines.size(), 1u);
}

// The Asus, 3 November: Proton's advanced kill switch is pvpnksintrf1,
// and the check said "Off".
TEST(VpnCheckTest, ProtonKillSwitchAnyNumberAndWithoutRoot) {
    VpnFacts f;
    f.links = {{"wlo1", true, false, false}, {"pvpnksintrf1", true, false, false},
               {"proton0", true, true, false}};
    f.route4 = "proton0";
    f.rulesetRead = false;                     // no root needed
    const auto r = assessVpn(f);
    EXPECT_EQ(r.kind, "Proton VPN");
    EXPECT_EQ(line(r, "Kill switch").health, VpnHealth::Ok);
}

TEST(VpnCheckTest, ProtonDisconnectedKillSwitchBlocking) {
    VpnFacts f;
    f.links = {{"wlo1", true, false, false}, {"pvpnksintrf1", true, false, false}};
    f.route4 = "pvpnksintrf1";
    const auto r = assessVpn(f);
    ASSERT_EQ(r.lines.size(), 1u);
    EXPECT_EQ(r.lines[0].health, VpnHealth::Ok);
    EXPECT_NE(r.lines[0].detail.find("blocking"), std::string::npos);
}

// The Asus, 4 November: IPv6 routed into Proton's kill-switch
// interface was reported as a LEAK. It is blocked.
TEST(VpnCheckTest, IPv6IntoProtonsBlockerIsBlockedNotLeaked) {
    VpnFacts f;
    f.links = {{"wlo1", true, false, false}, {"pvpnksintrf1", true, false, false},
               {"proton0", true, true, false}};
    f.route4 = "proton0";
    f.ipv6Reachable = true;
    f.route6 = "pvpnksintrf1";
    const auto r = assessVpn(f);
    EXPECT_EQ(line(r, "IPv6").health, VpnHealth::Ok);
    EXPECT_NE(line(r, "IPv6").detail.find("Blocked"), std::string::npos);
}
