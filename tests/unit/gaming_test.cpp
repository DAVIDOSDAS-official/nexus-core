#include <gtest/gtest.h>

#include <nexus/system/gaming.hpp>

using namespace nexus::system;

TEST(GamingTest, FreshIntelLaptopGetsBoth) {
    GamingState s;
    s.ntsyncModule = true;
    s.splitLock = 1;
    const auto plan = gamingPlan(s, "");
    ASSERT_EQ(plan.size(), 3u);
    EXPECT_EQ(plan[0], GamingFix::NtsyncNow);
    EXPECT_EQ(plan[1], GamingFix::NtsyncAtStart);
    EXPECT_EQ(plan[2], GamingFix::SplitLockOff);
}

TEST(GamingTest, OnlyOneWhenAsked) {
    GamingState s;
    s.ntsyncModule = true;
    s.splitLock = 1;
    EXPECT_EQ(gamingPlan(s, "split-lock"), std::vector<GamingFix>{GamingFix::SplitLockOff});
    EXPECT_EQ(gamingPlan(s, "ntsync").size(), 2u);
}

TEST(GamingTest, NothingWhereTheMachineHasNoKnob) {
    GamingState s;                 // AMD, or an Intel CPU without split-lock detection
    s.ntsyncModule = true; s.ntsyncDevice = true; s.ntsyncAtStart = true;
    EXPECT_TRUE(gamingPlan(s, "").empty());
}

TEST(GamingTest, AlreadyOffIsLeftAlone) {
    GamingState s;
    s.splitLock = 0;               // someone set it already
    EXPECT_TRUE(gamingPlan(s, "split-lock").empty());
}

TEST(GamingTest, SysctlValues) {
    EXPECT_EQ(parseSysctlInt("1\n"), 1);
    EXPECT_EQ(parseSysctlInt("0"), 0);
    EXPECT_EQ(parseSysctlInt(" 1048576\n"), 1048576);
    EXPECT_FALSE(parseSysctlInt("").has_value());
    EXPECT_FALSE(parseSysctlInt("yes").has_value());
    EXPECT_FALSE(parseSysctlInt("1 2").has_value());
}
