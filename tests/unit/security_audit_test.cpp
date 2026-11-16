#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <unistd.h>

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

// The Asus, 9 November: zone "trusted" was FAIL without saying for what.
TEST(SecurityAuditTest, ATrustedZoneIsJudgedByWhatIsInIt) {
    ZoneInfo z;
    EXPECT_EQ(trustedReach(z), TrustedReach::Nothing);
    z.interfaces = "docker0 podman1 virbr0";
    EXPECT_EQ(trustedReach(z), TrustedReach::Containers);
    z.interfaces = "docker0 proton0";
    EXPECT_EQ(trustedReach(z), TrustedReach::Tunnel);
    z.interfaces = "wlo1";
    EXPECT_EQ(trustedReach(z), TrustedReach::Network);
    z.interfaces = "";
    z.sources = "192.168.0.0/24";
    EXPECT_EQ(trustedReach(z), TrustedReach::Network);
    EXPECT_EQ(parseZoneInfo("trusted (active)\n  target: ACCEPT\n  interfaces: pvpnksintrf0 proton0\n").interfaces,
              "pvpnksintrf0 proton0");
}

// The Asus, 9 November: podman's network is in "trusted" as a source.
TEST(SecurityAuditTest, PodmansNetworkAsASourceIsContainers) {
    ZoneInfo z;
    z.sources = "10.88.0.0/16";
    EXPECT_EQ(trustedReach(z), TrustedReach::Containers);
    z.sources = "192.168.0.0/24";
    EXPECT_EQ(trustedReach(z), TrustedReach::Network);
    // A bridge here, on a network of its own.
    const auto local = parseIpAddresses(
        "1: lo    inet 127.0.0.1/8 scope host lo\n"
        "3: wlo1    inet 192.168.0.13/24 brd 192.168.0.255 scope global wlo1\n"
        "7: podman1    inet 10.89.0.1/24 brd 10.89.0.255 scope global podman1\n");
    ASSERT_EQ(local.size(), 3u);
    EXPECT_EQ(local[2].interface, "podman1");
    z.sources = "10.89.0.0/24";
    EXPECT_EQ(trustedReach(z, local), TrustedReach::Containers);
    // The same range as the Wi-Fi is the real network.
    z.sources = "192.168.0.0/24";
    EXPECT_EQ(trustedReach(z, local), TrustedReach::Network);
}

// A more exact scope under Nexus's own wins for those images: it must
// require the key too.
TEST(SecurityAuditTest, AMoreExactScopeWithoutTheKeyIsNotEnforced) {
    const std::string key = "/etc/pki/containers/nexus-core.pub";
    const std::string policy =
        R"({"default":[{"type":"reject"}],"transports":{"docker":{)"
        R"("ghcr.io/davidosdas-official": [{"type":"sigstoreSigned","keyPath":"/etc/pki/containers/nexus-core.pub"}],)"
        R"("ghcr.io/davidosdas-official/nexus-core": [{"type":"insecureAcceptAnything"}]}}})";
    EXPECT_FALSE(policyRequiresKey(policy, "ghcr.io/davidosdas-official", key));
    // The words present but not as the rule's type: no.
    const std::string words =
        R"({"transports":{"docker":{"ghcr.io/davidosdas-official": [{"type":"insecureAcceptAnything",)"
        R"("note":"sigstoreSigned /etc/pki/containers/nexus-core.pub"}]}}})";
    EXPECT_FALSE(policyRequiresKey(words, "ghcr.io/davidosdas-official", key));
    EXPECT_FALSE(policyRequiresKey("not json", "ghcr.io/davidosdas-official", key));
}

// ---------------------------------------------------------------------
// "Starts by itself" (0.2.0, ledger R4-L2): a made-up home folder.

namespace {

namespace fs = std::filesystem;

struct FakeHome {
    fs::path root;      // holds the home and what lies outside it
    fs::path home;
    FakeHome() {
        static int made = 0;
        root = fs::path(::testing::TempDir()) /
               ("nexus-home-" + std::to_string(::getpid()) + "-" + std::to_string(++made));
        home = root / "home" / "person";
        fs::create_directories(home);
    }
    ~FakeHome() {
        std::error_code ignored;
        fs::remove_all(root, ignored);
    }
    void file(const std::string& relative, const std::string& text = "x\n") const {
        fs::create_directories((home / relative).parent_path());
        std::ofstream(home / relative) << text;
    }
};

bool has(const std::vector<std::string>& list, const std::string& one) {
    return std::find(list.begin(), list.end(), one) != list.end();
}

std::string allOf(const std::vector<std::string>& lines) {
    std::string text;
    for (const auto& line : lines) text += line + "\n";
    return text;
}

}  // namespace

TEST(SecurityAuditTest, AnEmptyHomeStartsNothingAndSaysWhatWasNotLookedAt) {
    const FakeHome h;
    const auto found = findAutoStarts(h.home.string());
    EXPECT_TRUE(found.starts.empty());
    EXPECT_TRUE(found.shellFiles.empty());
    EXPECT_TRUE(found.notRead.empty());

    bool nothing = false;
    const auto lines = autoStartLines(found, nothing);
    EXPECT_TRUE(nothing);
    const std::string text = allOf(lines);
    EXPECT_NE(lines.front().find("nothing set up"), std::string::npos) << text;
    // Every time, also when all is well: what "ok" does not cover.
    EXPECT_NE(text.find("not looked at:"), std::string::npos) << text;
    EXPECT_NE(text.find("whole system"), std::string::npos) << text;
    EXPECT_NE(text.find("cron"), std::string::npos) << text;
    EXPECT_NE(text.find("other accounts"), std::string::npos) << text;
}

// Until 0.1.99 only autostart/*.desktop and systemd/user/*.service and
// *.timer were seen: every other line of this test read "ok".
TEST(SecurityAuditTest, StartsByItselfLooksInEveryPlace) {
    const FakeHome h;
    // The two places it always looked in.
    h.file(".config/autostart/chat.desktop");
    h.file(".config/systemd/user/sync.service");
    h.file(".config/systemd/user/sync.timer");
    // What it did not see.
    h.file(".config/systemd/user/agent.socket");
    h.file(".config/systemd/user/watch.path");
    h.file(".config/systemd/user/ssh-agent.service.d/override.conf");
    h.file(".config/systemd/user/ssh-agent.service.d/notes.txt");   // not a drop-in
    fs::create_directories(h.home / ".config/systemd/user/default.target.wants");
    fs::create_symlink("/usr/lib/systemd/user/far.service",
                       h.home / ".config/systemd/user/default.target.wants/far.service");
    h.file(".local/share/systemd/user/hidden.service");
    h.file(".config/autostart-scripts/run.sh");
    h.file(".config/plasma-workspace/env/path.sh");
    h.file(".config/plasma-workspace/shutdown/bye.sh");
    h.file(".config/environment.d/10-preload.conf");
    // Not something that starts: left out.
    h.file(".config/autostart/readme.txt");
    h.file(".config/systemd/user/notes.md");
    h.file(".config/environment.d/readme");

    const auto found = findAutoStarts(h.home.string());
    for (const char* want : {
             "chat.desktop", "sync.service", "sync.timer", "agent.socket", "watch.path",
             "ssh-agent.service.d/override.conf", "default.target.wants/far.service",
             ".local/share/systemd/user/hidden.service", "autostart-scripts/run.sh",
             "plasma-workspace/env/path.sh", "plasma-workspace/shutdown/bye.sh",
             "environment.d/10-preload.conf"}) {
        EXPECT_TRUE(has(found.starts, want)) << want;
    }
    EXPECT_EQ(found.starts.size(), 12u);
    EXPECT_TRUE(std::is_sorted(found.starts.begin(), found.starts.end()));
    EXPECT_TRUE(found.notRead.empty());

    bool nothing = true;
    const std::string text = allOf(autoStartLines(found, nothing));
    EXPECT_FALSE(nothing);
    for (const auto& name : found.starts) {
        EXPECT_NE(text.find(name), std::string::npos) << name << "\n" << text;
    }
    EXPECT_NE(text.find("(check you know each one)"), std::string::npos) << text;
    EXPECT_NE(text.find("not looked at:"), std::string::npos) << text;
}

TEST(SecurityAuditTest, ShellFilesAreNamedNotJudged) {
    const FakeHome h;
    h.file(".bashrc");
    h.file(".bash_profile");
    h.file(".profile");
    h.file(".zshrc");
    h.file(".config/fish/config.fish");
    h.file(".config/fish/conf.d/extra.fish");
    fs::create_directories(h.home / ".zshenv");   // a folder is not a shell file

    const auto found = findAutoStarts(h.home.string());
    EXPECT_TRUE(found.starts.empty());
    for (const char* want : {".bashrc", ".bash_profile", ".profile", ".zshrc",
                             ".config/fish/config.fish", ".config/fish/conf.d/extra.fish"}) {
        EXPECT_TRUE(has(found.shellFiles, want)) << want;
    }
    EXPECT_FALSE(has(found.shellFiles, ".zshenv"));

    // Nearly everyone has a .bashrc: it is said, and it is still "ok".
    bool nothing = false;
    const auto lines = autoStartLines(found, nothing);
    EXPECT_TRUE(nothing);
    const std::string text = allOf(lines);
    EXPECT_NE(text.find("runs in every new shell or at login: .bashrc"), std::string::npos) << text;
    EXPECT_NE(text.find("(named, not judged)"), std::string::npos) << text;
}

// Under sudo this runs as root in a folder the person controls. A link
// in place of a folder must not make root read out some other folder.
TEST(SecurityAuditTest, ALinkLeadingOutOfTheHomeIsNotFollowed) {
    const FakeHome h;
    const fs::path outside = h.root / "roots-own";
    fs::create_directories(outside / "inner.service.d");
    std::ofstream(outside / "secret.desktop") << "x\n";
    std::ofstream(outside / "secret.service") << "x\n";
    std::ofstream(outside / "inner.service.d" / "secret.conf") << "x\n";

    fs::create_directories(h.home / ".config/systemd/user");
    fs::create_symlink(outside, h.home / ".config/autostart");
    fs::create_symlink(outside, h.home / ".config/autostart-scripts");
    // A unit folder that is real, with a drop-in folder that is a link out.
    fs::create_symlink(outside / "inner.service.d",
                       h.home / ".config/systemd/user/inner.service.d");
    h.file(".config/systemd/user/own.service");

    const auto found = findAutoStarts(h.home.string());
    EXPECT_EQ(found.starts, std::vector<std::string>{"own.service"});
    const std::string said = allOf(found.notRead);
    EXPECT_EQ(said.find("secret"), std::string::npos) << said;
    EXPECT_NE(said.find("~/.config/autostart (a link that leads outside your home folder)"),
              std::string::npos) << said;
    EXPECT_NE(said.find("~/.config/autostart-scripts (a link that leads outside"),
              std::string::npos) << said;
    EXPECT_NE(said.find("~/.config/systemd/user/inner.service.d (a link; not followed)"),
              std::string::npos) << said;

    // Not read is not "ok".
    bool nothing = true;
    const std::string text = allOf(autoStartLines(found, nothing));
    EXPECT_FALSE(nothing);
    EXPECT_NE(text.find("not read: ~/.config/autostart"), std::string::npos) << text;
    EXPECT_EQ(text.find("secret"), std::string::npos) << text;
}

// A link that stays inside the home (a dotfiles folder) is the
// person's own arrangement, and is read.
TEST(SecurityAuditTest, ALinkThatStaysInsideTheHomeIsRead) {
    const FakeHome h;
    h.file("dotfiles/autostart/mail.desktop");
    fs::create_directories(h.home / ".config");
    fs::create_symlink(h.home / "dotfiles/autostart", h.home / ".config/autostart");

    const auto found = findAutoStarts(h.home.string());
    EXPECT_EQ(found.starts, std::vector<std::string>{"mail.desktop"});
    EXPECT_TRUE(found.notRead.empty());
}

TEST(SecurityAuditTest, AFolderWithTooManyNamesIsReadInPartAndSaid) {
    const FakeHome h;
    fs::create_directories(h.home / ".config/autostart-scripts");
    for (std::size_t i = 0; i < kAutoStartFolderLimit + 25; ++i) {
        std::ofstream(h.home / ".config/autostart-scripts" / ("s" + std::to_string(i))) << "";
    }
    const auto found = findAutoStarts(h.home.string());
    EXPECT_EQ(found.starts.size(), kAutoStartFolderLimit);
    const std::string said = allOf(found.notRead);
    EXPECT_NE(said.find("~/.config/autostart-scripts (only its first"), std::string::npos) << said;

    // Forty are shown, and the rest are counted, not dropped.
    bool nothing = true;
    const std::string text = allOf(autoStartLines(found, nothing));
    EXPECT_FALSE(nothing);
    EXPECT_NE(text.find("... and " + std::to_string(kAutoStartFolderLimit - 40) + " more"),
              std::string::npos) << text;
}

TEST(SecurityAuditTest, AFileNameCannotSteerTheTerminal) {
    const FakeHome h;
    h.file(".config/autostart/a\x1b[2K\x1b[1Aok.desktop");
    h.file(".config/autostart-scripts/b\nc");
    bool nothing = false;
    const std::string first =
        autoStartLines(findAutoStarts(h.home.string()), nothing).front();
    EXPECT_EQ(first.find('\x1b'), std::string::npos);
    EXPECT_EQ(first.find('\n'), std::string::npos);
    EXPECT_NE(first.find("ok.desktop"), std::string::npos) << first;
}

TEST(SecurityAuditTest, AHomeThatIsNotThereIsSaidNotOk) {
    for (const char* home : {"", "relative/home", "/nonexistent/nexus-test-home"}) {
        const auto found = findAutoStarts(home);
        EXPECT_FALSE(found.notRead.empty()) << home;
        bool nothing = true;
        autoStartLines(found, nothing);
        EXPECT_FALSE(nothing) << home;
    }
}

// ---------------------------------------------------------------------
// Firewall suggestions (0.2.0, ledger item 11).

TEST(SecurityAuditTest, FirewallFixesChangeSavedAndRunningAndReloadNothing) {
    for (const char* change : {"--remove-port=1025-65535/tcp", "--remove-service=ssh",
                               "--remove-service=kdeconnect", "--remove-source=<range>",
                               "--change-interface=<name>"}) {
        const std::string fix = firewallFix("FedoraWorkstation", change);
        // Until 0.1.99: "... --permanent ... && sudo firewall-cmd --reload",
        // which drops every rule that exists only in the running firewall.
        EXPECT_EQ(fix.find("--reload"), std::string::npos) << fix;
        EXPECT_EQ(fix.find("--complete-reload"), std::string::npos) << fix;
        EXPECT_EQ(fix,
                  std::string("sudo firewall-cmd --permanent --zone=FedoraWorkstation ") + change +
                  " && sudo firewall-cmd --zone=FedoraWorkstation " + change);
    }
}

TEST(SecurityAuditTest, TheOneReloadSaysWhatItDrops) {
    const std::string back = firewallBackToSaved();
    EXPECT_EQ(back.rfind("sudo firewall-cmd --reload", 0), 0u) << back;
    EXPECT_NE(back.find("back to the saved rules"), std::string::npos) << back;
    EXPECT_NE(back.find("dropped"), std::string::npos) << back;
    EXPECT_NE(back.find("VPN"), std::string::npos) << back;
    // And the way to keep them instead.
    EXPECT_NE(back.find("--runtime-to-permanent"), std::string::npos) << back;
}
