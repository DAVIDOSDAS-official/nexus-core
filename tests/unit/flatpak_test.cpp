#include <gtest/gtest.h>

#include <nexus/system/flatpak_source.hpp>

using nexus::Source;
using nexus::system::FlatpakSource;
using nexus::system::formatSize;
using nexus::system::parseHumanSize;

namespace {

// Captured from a real machine, tabs and all. The separator and the
// size format are exactly the details a made-up fixture would get
// wrong.
const char* kRemote =
    "ai.loomfy.Loomfy\t1.7.2\tstable\t196.9 MB\n"
    "app.authpass.AuthPass\t1.9.6_1904\tstable\t15.6 MB\n"
    "com.discordapp.Discord\t1.0.156\tstable\t589.9 MB\n"
    "com.nickgirga.webready\t1.0.2\tstable\t532.0 kB\n";

const char* kInstalled =
    "com.discordapp.Discord\t1.0.156\t589.9 MB\n"
    "com.jetbrains.CLion\t2025.3.3\t1.4 MB\n";

}

TEST(FlatpakTest, ParsesTheSizesFlatpakPrints) {
    EXPECT_EQ(parseHumanSize("532.0 kB"), 532000u);
    EXPECT_EQ(parseHumanSize("15.6 MB"), 15600000u);
    EXPECT_EQ(parseHumanSize("1.4 GB"), 1400000000u);
    EXPECT_EQ(parseHumanSize("900 B"), 900u);
    EXPECT_EQ(parseHumanSize(""), 0u);
    EXPECT_EQ(parseHumanSize("not a size"), 0u);
}

TEST(FlatpakTest, FormatsSizesBack) {
    EXPECT_EQ(formatSize(0), "");
    EXPECT_NE(formatSize(589900000).find("MB"), std::string::npos);
    EXPECT_NE(formatSize(1400000000).find("GB"), std::string::npos);
    EXPECT_NE(formatSize(532000).find("kB"), std::string::npos);
}

TEST(FlatpakTest, ReadsApplications) {
    const auto result = FlatpakSource::parse(kRemote, kInstalled);

    ASSERT_TRUE(result.error.empty());
    EXPECT_EQ(result.components.size(), 4u);
    EXPECT_EQ(result.remoteApps, 4u);
    EXPECT_EQ(result.installedApps, 2u);

    EXPECT_EQ(result.components[0].id(), "ai.loomfy.Loomfy");
    EXPECT_EQ(result.components[0].version(), "1.7.2");
}

// Which is what makes them offerable alongside the base repositories
// rather than instead of them.
TEST(FlatpakTest, ApplicationsAreMarkedAsComingFromFlatpak) {
    const auto result = FlatpakSource::parse(kRemote, kInstalled);

    ASSERT_FALSE(result.components.empty());

    for (const auto& component : result.components) {
        EXPECT_EQ(component.source(), Source::Flatpak);
        EXPECT_TRUE(isIsolated(component.source()));
    }
}

// A Flatpak bundles what it needs. Carrying no dependencies is the
// shape of the thing, not a gap in the reading.
TEST(FlatpakTest, ApplicationsCarryNoDependencies) {
    const auto result = FlatpakSource::parse(kRemote, kInstalled);

    ASSERT_FALSE(result.components.empty());

    for (const auto& component : result.components) {
        EXPECT_TRUE(component.requirements().empty());
    }
}

TEST(FlatpakTest, TheSameApplicationFromTwoRemotesAppearsOnce) {
    const auto result = FlatpakSource::parse(
        "com.discordapp.Discord\t1.0.156\tstable\t589.9 MB\n"
        "com.discordapp.Discord\t1.0.156\tstable\t589.9 MB\n",
        ""
    );

    EXPECT_EQ(result.components.size(), 1u);
}

TEST(FlatpakTest, ApplicationsRunAnywhereTheRuntimeDoes) {
    const auto result = FlatpakSource::parse(kRemote, "");

    ASSERT_FALSE(result.components.empty());
    EXPECT_EQ(result.components[0].architecture(), "all");
}

TEST(FlatpakTest, HandlesEmptyOutput) {
    const auto result = FlatpakSource::parse("", "");

    EXPECT_TRUE(result.components.empty());
    EXPECT_EQ(result.remoteApps, 0u);
}

// Looking for a plain space assumes one. glib formats sizes with a
// non-breaking space in some locales, which left the unit unread and
// printed every Flatpak as "0 kB to fetch" -- a wrong number that
// looked like a formatting choice.
TEST(FlatpakTest, TheUnitIsFoundWhateverSeparatesIt) {
    // U+00A0, as UTF-8.
    const std::string nbsp = "\xc2\xa0";

    EXPECT_EQ(parseHumanSize("196.9" + nbsp + "MB"), 196900000u);
    EXPECT_EQ(parseHumanSize("532.0" + nbsp + "kB"), 532000u);

    // And no separator at all.
    EXPECT_EQ(parseHumanSize("15.6MB"), 15600000u);

    // A bare number is bytes.
    EXPECT_EQ(parseHumanSize("206358937"), 206358937u);
}
