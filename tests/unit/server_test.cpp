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

// 0.2.7, outside audit S-1 and ledger item 7: on the Asus (19 Nov) the
// website's Caddy ran as root on the host (podman top: USER root,
// HUSER root).
TEST(ServerTest, EveryServerHasItsOwnUids) {
    MinecraftOptions mc;
    const std::string m = minecraftQuadlet(mc);
    EXPECT_NE(m.find("[Container]\n# 0.2.7"), std::string::npos);
    EXPECT_NE(m.find("\nUserNS=auto\n"), std::string::npos);
    EXPECT_NE(m.find("\nNoNewPrivileges=true\n"), std::string::npos);
    EXPECT_NE(m.find(":/data:Z,U\n"), std::string::npos);
    // Its image changes user at start: capabilities stay.
    EXPECT_EQ(m.find("DropCapability"), std::string::npos);

    WebsiteOptions web;
    const std::string w = websiteQuadlet(web);
    for (const char* line : {"\nUserNS=auto\n", "\nNoNewPrivileges=true\n", "\nDropCapability=ALL\n",
                             "\nAddCapability=NET_BIND_SERVICE\n", "\nReadOnly=true\n",
                             "\nTmpfs=/config\n", "\nTmpfs=/data\n", ":/srv:ro,Z\n"}) {
        EXPECT_NE(w.find(line), std::string::npos) << line;
    }
    web.domain = "example.org";
    const std::string d = websiteQuadlet(web);
    EXPECT_NE(d.find("/caddy:/data:Z,U\n"), std::string::npos);
    EXPECT_EQ(d.find("Tmpfs=/data"), std::string::npos);
}

TEST(ServerTest, HardeningAServerMadeBeforeIsDoneOnce) {
    // As 0.2.6 wrote it.
    const std::string old =
        "[Unit]\nDescription=Nexus server: website (website)\n\n[Container]\n"
        "ContainerName=nexus-website\nImage=docker.io/library/caddy:2\n"
        "Label=nexus.kind=website\nVolume=/var/lib/nexus/servers/website/site:/srv:ro,Z\n"
        "Exec=caddy file-server --root /srv --listen :80\nPublishPort=8080:80/tcp\n"
        "\n[Service]\nRestart=always\n";
    EXPECT_FALSE(quadletIsHardened(old));
    const std::string once = hardenQuadlet(old, "website");
    EXPECT_TRUE(quadletIsHardened(once));
    EXPECT_EQ(hardenQuadlet(once, "website"), once);
    // Everything that was there is still there.
    EXPECT_NE(once.find("PublishPort=8080:80/tcp\n"), std::string::npos);
    EXPECT_NE(once.find("Exec=caddy file-server --root /srv --listen :80\n"), std::string::npos);
    // The hardening is in [Container], not [Service].
    EXPECT_LT(once.find("UserNS=auto"), once.find("[Service]"));
    EXPECT_EQ(hardenQuadlet("not a quadlet\n", "website"), "not a quadlet\n");
}

TEST(ServerTest, ContainersSubidsAreAddedOnceAndNeverOverlap) {
    // Fedora's, as the Asus has it: one line for the person.
    auto plan = containersSubidPlan("davidosdas:524288:65536\n");
    EXPECT_TRUE(plan.needed);
    EXPECT_EQ(plan.line, "containers:2147483647:2147483648\n");
    // No newline at the end: one comes first.
    EXPECT_EQ(containersSubidPlan("davidosdas:524288:65536").line,
              "\ncontainers:2147483647:2147483648\n");
    EXPECT_FALSE(containersSubidPlan("").conflict.size());
    EXPECT_TRUE(containersSubidPlan("").needed);
    // There already.
    EXPECT_FALSE(containersSubidPlan("a:1:2\ncontainers:100000:65536\n").needed);
    // A range that overlaps is left alone, and said.
    plan = containersSubidPlan("big:2000000000:300000000\n");
    EXPECT_FALSE(plan.needed);
    EXPECT_EQ(plan.conflict, "big:2000000000:300000000");
    // Junk lines are skipped, not trusted.
    EXPECT_TRUE(containersSubidPlan("# comment\nx:y:z\n").needed);
}
