#include <gtest/gtest.h>

#include <nexus/system/clock.hpp>

using namespace nexus::system;

// The Asus, 1 November: three days behind with network time "on".
TEST(ClockTest, ChronyOffsetSlowIsPositive) {
    const std::string tracking =
        "Reference ID    : C0A80101 (_gateway)\n"
        "Stratum         : 3\n"
        "System time     : 259200.123456789 seconds slow of NTP time\n"
        "Last offset     : +0.000012345 seconds\n";
    const auto offset = parseChronyOffset(tracking);
    ASSERT_TRUE(offset.has_value());
    EXPECT_NEAR(*offset, 259200.123456789, 1e-6);
}

TEST(ClockTest, ChronyOffsetFastIsNegative) {
    const auto offset = parseChronyOffset(
        "System time     : 0.000004113 seconds fast of NTP time\n");
    ASSERT_TRUE(offset.has_value());
    EXPECT_LT(*offset, 0.0);
    EXPECT_NEAR(*offset, -0.000004113, 1e-12);
}

TEST(ClockTest, ChronyOffsetUnreadableIsEmptyNotZero) {
    EXPECT_FALSE(parseChronyOffset("").has_value());
    EXPECT_FALSE(parseChronyOffset("506 Cannot talk to daemon\n").has_value());
    EXPECT_FALSE(parseChronyOffset(
        "System time     : lots seconds slow of NTP time\n").has_value());
    EXPECT_FALSE(parseChronyOffset(
        "System time     : 1.5 seconds sideways of NTP time\n").has_value());
    EXPECT_FALSE(parseChronyOffset("System time\n").has_value());
}

TEST(ClockTest, DescribeOffsetUsesTheLargestUnit) {
    EXPECT_EQ(describeOffset(259200.4), "3 days");
    EXPECT_EQ(describeOffset(-86400), "1 day");
    EXPECT_EQ(describeOffset(7300), "2 hours");
    EXPECT_EQ(describeOffset(61), "1 minute");
    EXPECT_EQ(describeOffset(45.9), "45 seconds");
    EXPECT_EQ(describeOffset(1), "1 second");
}

TEST(ClockTest, TimezoneNotChosenOnlyForTheInstallerDefault) {
    EXPECT_TRUE(timezoneNotChosen("America/New_York", false));
    // Set or kept with nexus-timezone: said to be right.
    EXPECT_FALSE(timezoneNotChosen("America/New_York", true));
    EXPECT_FALSE(timezoneNotChosen("Europe/Skopje", false));
    EXPECT_FALSE(timezoneNotChosen("", false));
}

// 0.2.4: whether chrony has a time server at all. The Asus, 18 November,
// at start: no server yet, reference 00000000.
TEST(ClockTest, ChronyWithNoServerIsNotFollowing) {
    const std::string tracking =
        "Reference ID    : 00000000 ()\n"
        "Stratum         : 0\n"
        "System time     : 0.000000000 seconds fast of NTP time\n"
        "Leap status     : Not synchronised\n";
    EXPECT_EQ(chronyFollowsServer(tracking), std::optional<bool>(false));
}

TEST(ClockTest, ChronyWithServerIsFollowing) {
    const std::string tracking =
        "Reference ID    : 5DBD5C4A (93.189.92.74)\n"
        "Stratum         : 3\n"
        "System time     : 0.002012345 seconds slow of NTP time\n"
        "Leap status     : Normal\n";
    EXPECT_EQ(chronyFollowsServer(tracking), std::optional<bool>(true));
}

TEST(ClockTest, ChronyUnreadableIsUnknown) {
    EXPECT_FALSE(chronyFollowsServer("").has_value());
    EXPECT_FALSE(chronyFollowsServer("506 Cannot talk to daemon\n").has_value());
    EXPECT_FALSE(chronyFollowsServer("Leap status     : something new\n").has_value());
}

TEST(ClockTest, RtcSinceEpoch) {
    EXPECT_EQ(parseRtcSinceEpoch("1763477165\n"), std::optional<long long>(1763477165));
    EXPECT_FALSE(parseRtcSinceEpoch("").has_value());
    EXPECT_FALSE(parseRtcSinceEpoch("-5\n").has_value());
    EXPECT_FALSE(parseRtcSinceEpoch("12ab\n").has_value());
    EXPECT_FALSE(parseRtcSinceEpoch("99999999999999999999999\n").has_value());
}
