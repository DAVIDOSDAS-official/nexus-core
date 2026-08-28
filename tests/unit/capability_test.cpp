#include <gtest/gtest.h>

#include <nexus/capability.hpp>

TEST(CapabilityTest, StoresNameCorrectly) {
    nexus::Capability capability("audio");

    EXPECT_EQ(capability.name(), "audio");
}
