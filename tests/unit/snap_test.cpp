#include <gtest/gtest.h>

#include <algorithm>

#include <nexus/system/snap_source.hpp>

using nexus::Source;
using nexus::system::parseSnapList;
using nexus::system::snapOwningUnit;

namespace {

// Captured from a real machine. Whitespace-aligned rather than
// tab-separated, and Notes carries several flags separated by commas
// without spaces.
const char* kList =
    "Name               Version                         Rev    Tracking       Publisher     Notes\n"
    "bare               1.0                             5      latest/stable  canonical**   base\n"
    "core22             20260410                        2437   latest/stable  canonical**   base\n"
    "gnome-42-2204      0+git.4982e7b-sdk0+git.69b626a  263    latest/stable  canonical**   -\n"
    "piper-tts          1.2.0                           1      latest/stable  someone       -\n";

bool has(
    const nexus::system::SnapSourceResult& result,
    const std::string& name
) {
    return std::any_of(
        result.components.begin(),
        result.components.end(),
        [&name](const nexus::Component& component) {
            return component.name() == name;
        }
    );
}

}

TEST(SnapTest, ReadsInstalledSnaps) {
    const auto result = parseSnapList(kList);

    ASSERT_TRUE(result.error.empty());
    EXPECT_EQ(result.components.size(), 4u);
    EXPECT_TRUE(has(result, "piper-tts"));
    EXPECT_TRUE(has(result, "core22"));
}

TEST(SnapTest, TheHeaderIsNotASnap) {
    EXPECT_FALSE(has(parseSnapList(kList), "Name"));
}

// "You have 12 snaps" should not include four that came with the
// others.
TEST(SnapTest, BasesAreCountedApartFromApplications) {
    const auto result = parseSnapList(kList);

    EXPECT_EQ(result.infrastructure, 2u);
    EXPECT_EQ(result.installed, 2u);
}

// --all lists superseded revisions. They are on disk and they are not
// what is in use.
TEST(SnapTest, DisabledRevisionsAreSkipped) {
    const auto result = parseSnapList(
        std::string(kList) +
        "core22             20260225                        2411   "
        "latest/stable  canonical**   base,disabled\n");

    // Still one core22, not two.
    std::size_t core22 = 0;

    for (const auto& component : result.components) {
        if (component.name() == "core22") {
            core22 += 1;
        }
    }

    EXPECT_EQ(core22, 1u);
}

TEST(SnapTest, SnapsAreMarkedAsComingFromSnap) {
    for (const auto& component : parseSnapList(kList).components) {
        EXPECT_EQ(component.source(), Source::Snap);
        EXPECT_TRUE(isIsolated(component.source()));
    }
}

// A snap unit is named snap.<snap>.<app>.service, so it says who owns
// it and no query is needed.
TEST(SnapTest, AUnitNamesItsOwningSnap) {
    EXPECT_EQ(
        snapOwningUnit("snap.piper-tts.piper.service"), "piper-tts");
    EXPECT_EQ(
        snapOwningUnit("snap.docker.dockerd.service"), "docker");

    // Anything else is not a snap unit.
    EXPECT_TRUE(snapOwningUnit("ssh.service").empty());
    EXPECT_TRUE(snapOwningUnit("snap.service").empty());
    EXPECT_TRUE(snapOwningUnit("").empty());
}

TEST(SnapTest, HandlesEmptyOutput) {
    EXPECT_TRUE(parseSnapList("").components.empty());
}
