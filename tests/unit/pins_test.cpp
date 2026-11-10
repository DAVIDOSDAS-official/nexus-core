#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <string>

#include <nexus/system/pins.hpp>

using nexus::system::parsePins;
using nexus::system::pinnedImage;

namespace {
const std::string kDigest = "sha256:" + std::string(64, 'a');
}

TEST(PinsTest, ReadsTagAndDigestPairs) {
    const auto pins = parsePins("# header\n"
                                "docker.io/library/caddy:2 " + kDigest + "\n"
                                "\n"
                                "docker.io/x:1 sha256:short\n"                // malformed digest
                                "docker.io/y:1@" + kDigest + " " + kDigest + "\n"   // already a digest
                                "docker.io/z:1 " + kDigest + " extra\n");      // extra field
    ASSERT_EQ(pins.size(), 1u);
    EXPECT_EQ(pins.at("docker.io/library/caddy:2"), kDigest);
    EXPECT_TRUE(parsePins("docker.io/a:1 SHA256:" + std::string(64, 'A')).empty());
}

TEST(PinsTest, APinnedTagGetsItsDigestOthersStayAsTheyAre) {
    const std::string path = ::testing::TempDir() + "nexus-pins-test";
    std::ofstream(path) << "docker.io/library/caddy:2 " << kDigest << "\n";
    EXPECT_EQ(pinnedImage("docker.io/library/caddy:2", path), "docker.io/library/caddy:2@" + kDigest);
    EXPECT_EQ(pinnedImage("docker.io/library/debian:stable", path), "docker.io/library/debian:stable");
    std::remove(path.c_str());
    // No list (a local build): the tag.
    EXPECT_EQ(pinnedImage("docker.io/library/caddy:2", path), "docker.io/library/caddy:2");
}
