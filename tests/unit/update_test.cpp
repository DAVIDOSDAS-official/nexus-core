#include <gtest/gtest.h>

#include <nexus/system/update.hpp>

using namespace nexus::system;

// The shape rpm-ostree prints for a container-image origin with an
// update found by the automatic check. Field order and padding vary
// between versions, so the tests vary them too.
static const char* kAvailable =
    "State: idle\n"
    "AutomaticUpdates: check; rpm-ostreed-automatic.timer: last run 3h ago\n"
    "Deployments:\n"
    "\xe2\x97\x8f ostree-image-signed:docker://ghcr.io/x/nexus-core:minimal\n"
    "                   Digest: sha256:aaa\n"
    "                  Version: 44.20261004.0 (2026-10-04T10:00:00Z)\n"
    "          LayeredPackages: aircrack-ng gobuster\n"
    "          AvailableUpdate:\n"
    "                  Version: 44.20261005.0 (2026-10-05T09:00:00Z)\n"
    "                   Digest: sha256:bbb\n"
    "                     Diff: 12 upgraded, 1 added\n"
    "\n"
    "  ostree-image-signed:docker://ghcr.io/x/nexus-core:minimal\n"
    "                  Version: 44.20261001.0 (2026-10-01T10:00:00Z)\n";

TEST(UpdateTest, ReadsTheAvailableUpdateNotTheRunningVersion) {
    const auto u = parseSystemUpdate(kAvailable);
    EXPECT_TRUE(u.available);
    EXPECT_EQ(u.version, "44.20261005.0");
    EXPECT_EQ(u.date, "2026-10-05T09:00:00Z");
    EXPECT_EQ(u.diff, "12 upgraded, 1 added");
    EXPECT_EQ(u.policy, "check");
    EXPECT_EQ(u.lastCheck, "rpm-ostreed-automatic.timer: last run 3h ago");
    EXPECT_FALSE(u.staged);
}

TEST(UpdateTest, NothingNewIsNothingNew) {
    const auto u = parseSystemUpdate(
        "State: idle\n"
        "AutomaticUpdates: disabled\n"
        "Deployments:\n"
        "\xe2\x97\x8f ostree-image-signed:docker://x\n"
        "                  Version: 44.1 (2026-10-04T10:00:00Z)\n");
    EXPECT_FALSE(u.available);
    EXPECT_EQ(u.policy, "disabled");
    EXPECT_TRUE(u.lastCheck.empty());
    EXPECT_EQ(updateSummary(u, {}), "");
}

TEST(UpdateTest, SeesAStagedDeployment) {
    const auto u = parseSystemUpdate(
        "State: idle\n"
        "Deployments:\n"
        "  ostree-image-signed:docker://x\n"
        "                  Version: 44.2 (2026-10-05T10:00:00Z)\n"
        "                   Staged: yes\n"
        "\xe2\x97\x8f ostree-image-signed:docker://x\n"
        "                  Version: 44.1 (2026-10-04T10:00:00Z)\n");
    EXPECT_TRUE(u.staged);
    EXPECT_EQ(u.stagedVersion, "44.2");
    EXPECT_FALSE(u.available);
}

TEST(UpdateTest, ReadsFlatpakUpdatesAndSkipsTheRest) {
    const auto apps = parseFlatpakUpdates(
        "org.mozilla.firefox\t131.0\n"
        "com.heroicgameslauncher.hgl\n"
        "Looking for updates...\n"
        "\n");
    ASSERT_EQ(apps.size(), 2u);
    EXPECT_EQ(apps[0].id, "org.mozilla.firefox");
    EXPECT_EQ(apps[0].version, "131.0");
    EXPECT_EQ(apps[1].version, "");
}

TEST(UpdateTest, SummarisesForANotification) {
    const auto u = parseSystemUpdate(kAvailable);
    EXPECT_EQ(updateSummary(u, {{"org.a.b", ""}, {"org.c.d", ""}}),
              "Nexus 44.20261005.0 and 2 app updates");
    EXPECT_EQ(updateSummary(SystemUpdate{}, {{"org.a.b", ""}}),
              "1 app update");
}
