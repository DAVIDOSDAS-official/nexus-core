#include <gtest/gtest.h>

#include <nexus/system/server.hpp>

using namespace nexus::system;

TEST(ServerTest, NamesThatArePathsAndUnits) {
    EXPECT_TRUE(validServerName("minecraft"));
    EXPECT_TRUE(validServerName("my-site2"));
    EXPECT_FALSE(validServerName(""));
    EXPECT_FALSE(validServerName("-x"));
    EXPECT_FALSE(validServerName("Minecraft"));
    EXPECT_FALSE(validServerName("../etc"));
    EXPECT_FALSE(validServerName("a b"));
    EXPECT_FALSE(validServerName(std::string(31, 'a')));
}

TEST(ServerTest, PathsFollowTheName) {
    EXPECT_EQ(serverUnit("minecraft"), "nexus-minecraft.service");
    EXPECT_EQ(serverQuadletPath("web"),
              "/etc/containers/systemd/nexus-web.container");
    EXPECT_EQ(serverDataDir("web"), "/var/lib/nexus/servers/web");
}

TEST(ServerTest, MinecraftMemoryLeavesRoomForTheDesktop) {
    // The Asus: MemTotal 3813376 kB.
    EXPECT_EQ(minecraftMemory(3813376ULL * 1024), "1280M");
    EXPECT_EQ(minecraftMemory(2ULL << 30), "1024M");   // floor
    EXPECT_EQ(minecraftMemory(8ULL << 30), "3072M");
    EXPECT_EQ(minecraftMemory(64ULL << 30), "4096M");  // ceiling
}

TEST(ServerTest, MinecraftQuadletCarriesEulaPortAndWorld) {
    MinecraftOptions options;
    options.memory = "1280M";
    options.port = 25570;
    const std::string text = minecraftQuadlet(options);
    EXPECT_NE(text.find("Image=docker.io/itzg/minecraft-server:latest"),
              std::string::npos);
    EXPECT_NE(text.find("Environment=EULA=TRUE"), std::string::npos);
    EXPECT_NE(text.find("Environment=MEMORY=1280M"), std::string::npos);
    EXPECT_NE(text.find("PublishPort=25570:25565/tcp"), std::string::npos);
    EXPECT_NE(text.find("Volume=/var/lib/nexus/servers/minecraft/data:/data:Z"),
              std::string::npos);
    EXPECT_NE(text.find("WantedBy=multi-user.target"), std::string::npos);
    EXPECT_NE(text.find("Restart=always"), std::string::npos);
}

TEST(ServerTest, WebsiteWithoutDomainIsPlainHttpOnItsPort) {
    WebsiteOptions options;
    options.port = 8081;
    const std::string text = websiteQuadlet(options);
    EXPECT_NE(text.find("--listen :80"), std::string::npos);
    EXPECT_NE(text.find("PublishPort=8081:80/tcp"), std::string::npos);
    EXPECT_NE(text.find("/var/lib/nexus/servers/website/site:/srv:ro,Z"),
              std::string::npos);
    EXPECT_EQ(text.find("443"), std::string::npos);
}

TEST(ServerTest, WebsiteWithDomainGetsHttpsAndKeepsCertificates) {
    WebsiteOptions options;
    options.domain = "example.org";
    const std::string text = websiteQuadlet(options);
    EXPECT_NE(text.find("--domain example.org"), std::string::npos);
    EXPECT_NE(text.find("PublishPort=443:443/tcp"), std::string::npos);
    EXPECT_NE(text.find("PublishPort=80:80/tcp"), std::string::npos);
    EXPECT_NE(text.find("/var/lib/nexus/servers/website/caddy:/data:Z"),
              std::string::npos);
}
