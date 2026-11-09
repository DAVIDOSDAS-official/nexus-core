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
