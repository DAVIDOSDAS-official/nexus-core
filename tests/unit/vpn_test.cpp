#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <sys/stat.h>

#include <nexus/system/vpn.hpp>

using nexus::system::renderTunnel;
using nexus::system::TunnelSettings;
using nexus::system::writeTunnel;

namespace {

std::string scratchPath() {
    return (std::filesystem::temp_directory_path() /
            ("nexus-wg-" + std::to_string(::getpid()) + "-" +
             std::to_string(std::rand()) + ".conf")).string();
}

}

// A config with invented values looks finished and does not work, and
// the failure arrives later looking like something else.
TEST(VpnTest, WhatCannotBeKnownIsRequired) {
    TunnelSettings settings;

    const auto missing = settings.missing();

    EXPECT_EQ(missing.size(), 3u);

    settings.address = "10.0.0.2/24";
    settings.peerPublicKey = "abc=";
    settings.endpoint = "host:51820";

    EXPECT_TRUE(settings.missing().empty());
}

TEST(VpnTest, EachMissingThingSaysWhatItIs) {
    // Naming the field is not enough: somebody who knew what an
    // endpoint was would have passed one.
    for (const std::string& item : TunnelSettings{}.missing()) {
        EXPECT_GT(item.size(), 20u);
        EXPECT_NE(item.find(" - "), std::string::npos);
    }
}

TEST(VpnTest, RendersAConfig) {
    TunnelSettings settings;

    settings.address = "10.0.0.2/24";
    settings.peerPublicKey = "peerkey=";
    settings.endpoint = "vpn.example.com:51820";

    const std::string config = renderTunnel(settings, "privatekey=");

    EXPECT_NE(config.find("[Interface]"), std::string::npos);
    EXPECT_NE(config.find("PrivateKey = privatekey="),
              std::string::npos);
    EXPECT_NE(config.find("[Peer]"), std::string::npos);
    EXPECT_NE(config.find("Endpoint = vpn.example.com:51820"),
              std::string::npos);
}

TEST(VpnTest, OptionalPartsAreOmittedRatherThanBlank) {
    TunnelSettings settings;

    settings.address = "10.0.0.2/24";
    settings.peerPublicKey = "k=";
    settings.endpoint = "h:1";

    const std::string config = renderTunnel(settings, "p=");

    // A blank DNS line is not the same as no DNS line, and wg-quick
    // reads the difference.
    EXPECT_EQ(config.find("DNS ="), std::string::npos);
    EXPECT_EQ(config.find("PersistentKeepalive"), std::string::npos);
}

// A secret written with the wrong permissions is worse than one not
// written, because it looks like it worked.
TEST(VpnTest, TheFileIsPrivate) {
    const std::string path = scratchPath();

    const auto result = writeTunnel(path, "secret", false);

    ASSERT_TRUE(result.ok) << result.error;

    struct stat information {};

    ASSERT_EQ(::stat(path.c_str(), &information), 0);

    EXPECT_EQ(information.st_mode & (S_IRWXG | S_IRWXO), 0u);
    EXPECT_NE(information.st_mode & S_IRUSR, 0u);

    std::error_code error;
    std::filesystem::remove(path, error);
}

TEST(VpnTest, AnExistingConfigIsNotReplaced) {
    const std::string path = scratchPath();

    ASSERT_TRUE(writeTunnel(path, "first", false).ok);

    const auto second = writeTunnel(path, "second", false);

    EXPECT_FALSE(second.ok);
    EXPECT_NE(second.error.find("already exists"), std::string::npos);

    std::ifstream input(path);
    std::string contents;

    std::getline(input, contents);

    EXPECT_EQ(contents, "first");

    std::error_code error;
    std::filesystem::remove(path, error);
}

TEST(VpnTest, OverwriteIsPossibleWhenAsked) {
    const std::string path = scratchPath();

    ASSERT_TRUE(writeTunnel(path, "first", false).ok);
    EXPECT_TRUE(writeTunnel(path, "second", true).ok);

    std::error_code error;
    std::filesystem::remove(path, error);
}

TEST(VpnTest, AnUnwritablePathFailsRatherThanPretending) {
    const auto result =
        writeTunnel("/proc/nexus-cannot-exist/wg0.conf", "x", false);

    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.empty());
}
