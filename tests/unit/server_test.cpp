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

// 0.1.87 (audit): --domain goes onto Caddy's command line. "--browse"
// is made of the same characters as a domain name.
TEST(ServerTest, ADomainNameCannotBeAnOption) {
    using nexus::system::validDomainName;
    EXPECT_TRUE(validDomainName("example.org"));
    EXPECT_TRUE(validDomainName("my-site.example.co.uk"));
    EXPECT_TRUE(validDomainName("localhost"));
    EXPECT_TRUE(validDomainName("xn--bcher-kva.example"));
    EXPECT_FALSE(validDomainName(""));
    EXPECT_FALSE(validDomainName("--browse"));
    EXPECT_FALSE(validDomainName("-x.example.org"));
    EXPECT_FALSE(validDomainName("example.-org"));
    EXPECT_FALSE(validDomainName("example-.org"));
    EXPECT_FALSE(validDomainName(".example.org"));
    EXPECT_FALSE(validDomainName("example..org"));
    EXPECT_FALSE(validDomainName("example.org."));
    EXPECT_FALSE(validDomainName("exa mple.org"));
    EXPECT_FALSE(validDomainName("example.org\nExec=sh"));
    EXPECT_FALSE(validDomainName("a/b.org"));
    EXPECT_FALSE(validDomainName(std::string(64, 'a') + ".org"));
    EXPECT_FALSE(validDomainName(std::string(250, 'a') + ".org"));
}

// 0.1.88 (review of patch 113): a backup is taken, and a server
// removed, only once it is seen to be stopped.
TEST(ServerTest, OnlyAStoppedUnitCountsAsStopped) {
    using nexus::system::UnitRun;
    using nexus::system::unitRunState;
    EXPECT_EQ(unitRunState("inactive"), UnitRun::Stopped);
    EXPECT_EQ(unitRunState("inactive\n"), UnitRun::Stopped);
    EXPECT_EQ(unitRunState("failed"), UnitRun::Stopped);
    EXPECT_EQ(unitRunState("active"), UnitRun::Running);
    EXPECT_EQ(unitRunState("activating"), UnitRun::Running);
    EXPECT_EQ(unitRunState("deactivating"), UnitRun::Running);   // still going down
    EXPECT_EQ(unitRunState("reloading"), UnitRun::Running);
    EXPECT_EQ(unitRunState("maintenance"), UnitRun::Running);
    // Not known to be stopped is not stopped.
    EXPECT_EQ(unitRunState(""), UnitRun::Unknown);
    EXPECT_EQ(unitRunState("unknown"), UnitRun::Unknown);
    EXPECT_EQ(unitRunState("Failed to connect to bus: No such file or directory"), UnitRun::Unknown);
    EXPECT_EQ(unitRunState("inactive\nactive"), UnitRun::Unknown);
    EXPECT_EQ(unitRunState("INACTIVE"), UnitRun::Unknown);
    EXPECT_EQ(unitRunState("inactive (dead)"), UnitRun::Unknown);
}

TEST(ServerTest, OnlyAServerStoppedOnPurposeStaysStopped) {
    using nexus::system::startAgainAfterBackup;
    EXPECT_FALSE(startAgainAfterBackup("inactive"));
    EXPECT_FALSE(startAgainAfterBackup("inactive\n"));
    EXPECT_TRUE(startAgainAfterBackup("active"));
    EXPECT_TRUE(startAgainAfterBackup("activating"));
    EXPECT_TRUE(startAgainAfterBackup("failed"));     // crashed: not on purpose
    EXPECT_TRUE(startAgainAfterBackup(""));           // no answer: as before 0.1.87
}
