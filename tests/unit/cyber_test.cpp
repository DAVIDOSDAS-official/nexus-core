#include <gtest/gtest.h>

#include <nexus/system/cyber.hpp>

using namespace nexus::system;

TEST(CyberTest, TiersMapToKaliBundles) {
    ASSERT_EQ(cyberTiers().size(), 3u);
    EXPECT_EQ(cyberTier("small")->metapackage, "kali-tools-top10");
    EXPECT_EQ(cyberTier("medium")->metapackage, "kali-linux-default");
    EXPECT_EQ(cyberTier("full")->metapackage, "kali-linux-everything");
    EXPECT_FALSE(cyberTier("huge").has_value());
}

TEST(CyberTest, ToolNamesThatMayGoIntoACommand) {
    EXPECT_TRUE(safeToolName("nmap"));
    EXPECT_TRUE(safeToolName("msfconsole"));
    EXPECT_TRUE(safeToolName("john.bin"));
    EXPECT_TRUE(safeToolName("python3.12"));
    EXPECT_FALSE(safeToolName(""));
    EXPECT_FALSE(safeToolName("-rf"));
    EXPECT_FALSE(safeToolName("nmap; rm -rf ~"));
    EXPECT_FALSE(safeToolName("../bin/sh"));
    EXPECT_FALSE(safeToolName("$(id)"));
}

TEST(CyberTest, FindsTheContainerInDistroboxList) {
    const std::string list =
        "ID           | NAME                 | STATUS             | IMAGE\n"
        "1a2b3c4d5e6f | fedora-dev           | Exited (0) 2 days  | registry.fedoraproject.org/fedora-toolbox:44\n"
        "3f2a9b8c7d6e | nexus-cyber          | Up 2 minutes       | docker.io/kalilinux/kali-rolling:latest\n";
    const auto st = parseDistroboxList(list, "nexus-cyber");
    EXPECT_TRUE(st.exists);
    EXPECT_TRUE(st.running);
    EXPECT_EQ(st.image, "docker.io/kalilinux/kali-rolling:latest");

    const auto other = parseDistroboxList(list, "fedora-dev");
    EXPECT_TRUE(other.exists);
    EXPECT_FALSE(other.running);

    // a name that only contains ours is not ours
    EXPECT_FALSE(parseDistroboxList("x | nexus-cyber-old | Up | y\n", "nexus-cyber").exists);
    EXPECT_FALSE(parseDistroboxList("", "nexus-cyber").exists);
}
