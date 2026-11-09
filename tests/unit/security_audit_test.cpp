#include <gtest/gtest.h>

#include <nexus/system/security_audit.hpp>

using namespace nexus::system;

TEST(SecurityAuditTest, WeakSettingsOnlyWhereKnownAndWeaker) {
    std::map<std::string, int> now = {
        {"kernel.kptr_restrict", 0},          // weak
        {"kernel.dmesg_restrict", 1},         // fine
        {"kernel.unprivileged_bpf_disabled", 2},  // >= 1 is fine
        {"net.ipv4.conf.all.accept_redirects", 1},  // weak (must be exactly 0)
        {"net.ipv4.conf.all.rp_filter", 2},   // loose filter is fine
    };
    const auto weak = weakSettings(now);
    ASSERT_EQ(weak.size(), 2u);
    EXPECT_EQ(weak[0].key, "kernel.kptr_restrict");
    EXPECT_EQ(weak[1].key, "net.ipv4.conf.all.accept_redirects");
}

TEST(SecurityAuditTest, NoPtraceOrUsernsInTheDefaultTier) {
    for (const auto& s : hardeningTable()) {
        EXPECT_EQ(s.key.find("ptrace"), std::string::npos);
        EXPECT_EQ(s.key.find("userns"), std::string::npos);
        EXPECT_EQ(s.key.find("unprivileged_userns"), std::string::npos);
    }
}

TEST(SecurityAuditTest, HardeningFileHasEachSettingAndTheUndo) {
    const auto f = hardeningFile({hardeningTable()[0], hardeningTable()[3]});
    EXPECT_NE(f.find("kernel.kptr_restrict = 1"), std::string::npos);
    EXPECT_NE(f.find("net.ipv4.conf.all.accept_redirects = 0"), std::string::npos);
    EXPECT_NE(f.find("harden off --apply"), std::string::npos);
}

TEST(SecurityAuditTest, ListenersLeaveOutLoopback) {
    const std::string ss =
        "tcp   LISTEN 0 4096 127.0.0.53%lo:53  0.0.0.0:*\n"
        "tcp   LISTEN 0 128  127.0.0.1:631     0.0.0.0:*\n"
        "tcp   LISTEN 0 4096 0.0.0.0:80        0.0.0.0:*\n"
        "tcp   LISTEN 0 4096 [::1]:631         [::]:*\n"
        "udp   UNCONN 0 0    [::]:5353         [::]:*\n"
        "udp   UNCONN 0 0    0.0.0.0%wlo1:68   0.0.0.0:*\n";
    const auto l = parseListeners(ss);
    ASSERT_EQ(l.size(), 2u);              // DHCP client on :68 is not a service
    EXPECT_EQ(l[0], "tcp 0.0.0.0:80");
    EXPECT_EQ(l[1], "udp [::]:5353");
}

TEST(SecurityAuditTest, EmptyPasswordsAreFoundLockedOnesAreNot) {
    const std::string shadow =
        "root:!locked::0:99999:7:::\n"
        "davidosdas:$6$abc$def:20000:0:99999:7:::\n"
        "guest::20000:0:99999:7:::\n"
        "sddm:!!:20000::::::\n"
        "nobody:*:20000::::::\n";
    EXPECT_EQ(emptyPasswordAccounts(shadow), std::vector<std::string>{"guest"});
}

TEST(SecurityAuditTest, DaysSinceABuild) {
    // 2026-11-09T00:00:00Z
    const long now = 1794182400L;
    EXPECT_EQ(daysSince("2026-11-08T20:00:19Z", now), 1);
    EXPECT_EQ(daysSince("2026-10-09T10:00:00Z", now), 31);
    EXPECT_FALSE(daysSince("soon", now).has_value());
}

// Strict reverse-path filtering (1) is fine where it is set; harden
// itself writes loose (2), which does not drop a VPN's packets.
TEST(SecurityAuditTest, ReversePathFilterLooseIsWrittenStrictIsKept) {
    const std::string key = "net.ipv4.conf.all.rp_filter";
    EXPECT_TRUE(weakSettings({{key, 1}}).empty());
    EXPECT_TRUE(weakSettings({{key, 2}}).empty());
    const auto w = weakSettings({{key, 0}});
    ASSERT_EQ(w.size(), 1u);
    EXPECT_EQ(w[0].wanted, 2);
}

TEST(SecurityAuditTest, ActiveZonesAreTheUnindentedLines) {
    const auto z = parseActiveZones("FedoraWorkstation (default)\n  interfaces: wlo1\npublic\n  sources: 10.0.0.0/8\n");
    ASSERT_EQ(z.size(), 2u);
    EXPECT_EQ(z[0], "FedoraWorkstation");
    EXPECT_EQ(z[1], "public");
}

TEST(SecurityAuditTest, AWideOpenPortRangeIsFound) {
    EXPECT_EQ(wideOpenRange("1025-65535/udp 1025-65535/tcp"), "1025-65535/udp");
    EXPECT_EQ(wideOpenRange("8080/tcp 27015-27030/udp"), "");
    EXPECT_EQ(wideOpenRange(""), "");
}

TEST(SecurityAuditTest, ThePolicyMustNameTheKeyUnderTheScope) {
    const std::string key = "/etc/pki/containers/nexus-core.pub";
    const std::string good =
        R"({"default":[{"type":"reject"}],"transports":{"docker":{)"
        R"("ghcr.io/davidosdas-official": [{"type":"sigstoreSigned","keyPath":"/etc/pki/containers/nexus-core.pub"}],)"
        R"("": [{"type":"insecureAcceptAnything"}]}}})";
    EXPECT_TRUE(policyRequiresKey(good, "ghcr.io/davidosdas-official", key));
    // The rule exists but for another scope: not Nexus's.
    const std::string elsewhere =
        R"({"transports":{"docker":{"ghcr.io/davidosdas-official": [{"type":"insecureAcceptAnything"}],)"
        R"("quay.io/x": [{"type":"sigstoreSigned","keyPath":"/etc/pki/containers/nexus-core.pub"}]}}})";
    EXPECT_FALSE(policyRequiresKey(elsewhere, "ghcr.io/davidosdas-official", key));
    EXPECT_FALSE(policyRequiresKey("{}", "ghcr.io/davidosdas-official", key));
}

TEST(SecurityAuditTest, ZoneInfoIsReadFromInfoZone) {
    const auto z = parseZoneInfo(
        "FedoraWorkstation (default, active)\n  target: default\n  icmp-block-inversion: no\n"
        "  interfaces: wlo1\n  services: dhcpv6-client mdns samba-client\n"
        "  ports: 1025-65535/udp 1025-65535/tcp\n  protocols: \n");
    EXPECT_EQ(z.target, "default");
    EXPECT_EQ(z.services, "dhcpv6-client mdns samba-client");
    EXPECT_EQ(z.ports, "1025-65535/udp 1025-65535/tcp");
    EXPECT_EQ(parseZoneInfo("trusted (active)\n  target: ACCEPT\n  ports: \n").target, "ACCEPT");
}
