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

static const char* kRunning =
    "State: idle\n"
    "Deployments:\n"
    "\xe2\x97\x8f ostree-image-signed:docker://ghcr.io/x/nexus-core:minimal\n"
    "                   Digest: sha256:7551\n"
    "                  Version: 44.20261002.0 (2026-10-04T13:19:59Z)\n"
    "\n"
    "  ostree-image-signed:docker://ghcr.io/x/nexus-core:minimal\n"
    "                   Digest: sha256:3706\n"
    "                  Version: 44.20261002.0 (2026-10-04T11:24:47Z)\n";

// skopeo inspect as the Asus printed it, 4 October (shortened).
static const char* kInspect = R"({
    "Name": "ghcr.io/x/nexus-core",
    "Digest": "sha256:9999",
    "Created": "2026-10-05T14:18:53.528859441Z",
    "Labels": {
        "containers.bootc": "1",
        "nexus.version": "0.1.32",
        "org.opencontainers.image.version": "44.20261002.0"
    },
    "Layers": ["sha256:bb9f"]
})";

TEST(UpdateTest, ReadsWhatTheMachineRuns) {
    const auto u = parseSystemUpdate(kRunning);
    EXPECT_EQ(u.origin, "ghcr.io/x/nexus-core:minimal");
    EXPECT_EQ(u.digest, "sha256:7551");
    EXPECT_FALSE(u.staged);
}

TEST(UpdateTest, ImageReferenceDropsTheTransport) {
    EXPECT_EQ(imageReference("ostree-image-signed:docker://ghcr.io/a/b:c"),
              "ghcr.io/a/b:c");
    EXPECT_EQ(imageReference("ostree-unverified-registry:docker://q/r:s"),
              "q/r:s");
    EXPECT_EQ(imageReference("fedora:fedora/44/x86_64/silverblue"), "");
}

TEST(UpdateTest, ANewDigestOnTheRegistryIsAnUpdate) {
    auto u = parseSystemUpdate(kRunning);
    const auto remote = parseImageInspect(kInspect);
    ASSERT_TRUE(remote.ok);
    EXPECT_EQ(remote.nexusVersion, "0.1.32");
    compareWithRegistry(u, remote);
    EXPECT_TRUE(u.available);
    EXPECT_EQ(u.version, "0.1.32");
    EXPECT_EQ(updateSummary(u, {}), "Nexus 0.1.32");
}

TEST(UpdateTest, TheSameDigestIsUpToDate) {
    auto u = parseSystemUpdate(kRunning);
    RemoteImage same;
    same.ok = true;
    same.digest = "sha256:7551";
    compareWithRegistry(u, same);
    EXPECT_FALSE(u.available);
}

TEST(UpdateTest, AStagedDigestIsNotOfferedTwice) {
    auto u = parseSystemUpdate(
        "Deployments:\n"
        "  ostree-image-signed:docker://ghcr.io/x/y:z\n"
        "                   Staged: yes\n"
        "                   Digest: sha256:9999\n"
        "\xe2\x97\x8f ostree-image-signed:docker://ghcr.io/x/y:z\n"
        "                   Digest: sha256:7551\n");
    EXPECT_TRUE(u.staged);
    EXPECT_EQ(u.stagedDigest, "sha256:9999");
    compareWithRegistry(u, parseImageInspect(kInspect));
    EXPECT_FALSE(u.available);
}

TEST(UpdateTest, NoLabelStillSaysSomethingReadable) {
    auto u = parseSystemUpdate(kRunning);
    RemoteImage remote;
    remote.ok = true;
    remote.digest = "sha256:8888";
    compareWithRegistry(u, remote);
    EXPECT_EQ(updateSummary(u, {}), "A new Nexus version");
    EXPECT_FALSE(parseImageInspect("not json").ok);
}

TEST(UpdateTest, ReadsTheLinesTheShopReads) {
    const auto u = parseUpdateLines(
        "running\t0.1.38\t9 October\n"
        "staged\t0.1.39\n"
        "system\tnew\t0.1.39, rebuilt with Fedora's latest updates\t"
        "10 October\t12 upgraded\n"
        "app\torg.videolan.VLC\t3.0.21\n"
        "app\tcom.obsproject.Studio\t\n"
        "future\tsomething a newer nexus prints\n"
        "\n");
    EXPECT_EQ(u.running, "0.1.38");
    EXPECT_EQ(u.runningDay, "9 October");
    EXPECT_EQ(u.staged, "0.1.39");
    EXPECT_EQ(u.system, "new");
    EXPECT_EQ(u.version, "0.1.39, rebuilt with Fedora's latest updates");
    EXPECT_EQ(u.day, "10 October");
    EXPECT_EQ(u.diff, "12 upgraded");
    ASSERT_EQ(u.apps.size(), 2u);
    EXPECT_EQ(u.apps[0].id, "org.videolan.VLC");
    EXPECT_EQ(u.apps[0].version, "3.0.21");
    EXPECT_EQ(u.apps[1].version, "");
}

TEST(UpdateTest, LinesFromAMachineThatIsNotImageBased) {
    const auto u = parseUpdateLines("system\tnot-image\t\t\t\n");
    EXPECT_EQ(u.system, "not-image");
    EXPECT_TRUE(u.apps.empty());
    EXPECT_EQ(parseUpdateLines("").system, "");
}
