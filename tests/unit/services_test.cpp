#include <gtest/gtest.h>

#include <algorithm>

#include <nexus/system/services.hpp>

using nexus::system::applyFailedUnits;
using nexus::system::parseServiceState;
using nexus::system::parseUnitFiles;
using nexus::system::ServiceState;
using nexus::system::ServicesResult;

namespace {

// Captured from systemctl rather than invented: the column layout and
// the trailing summary line are exactly what a made-up fixture gets
// wrong.
const char* kUnitFiles =
    "UNIT FILE                                    STATE    PRESET\n"
    "apt-daily.service                            static   -\n"
    "console-getty.service                        disabled disabled\n"
    "cron.service                                 enabled  enabled\n"
    "ssh.service                                  enabled  enabled\n"
    "autovt@.service                              alias    -\n"
    "apache2.service                              masked   disabled\n"
    "systemd-tmpfiles-clean.timer                 static   -\n"
    "\n"
    "7 unit files listed.\n";

bool has(const ServicesResult& result, const std::string& name) {
    return std::any_of(
        result.services.begin(),
        result.services.end(),
        [&name](const nexus::system::Service& service) {
            return service.name == name;
        }
    );
}

}

TEST(ServicesTest, ReadsStates) {
    EXPECT_EQ(parseServiceState("enabled"), ServiceState::Enabled);
    EXPECT_EQ(parseServiceState("enabled-runtime"),
              ServiceState::Enabled);
    EXPECT_EQ(parseServiceState("disabled"), ServiceState::Disabled);
    EXPECT_EQ(parseServiceState("static"), ServiceState::Static);
    EXPECT_EQ(parseServiceState("masked"), ServiceState::Masked);
    EXPECT_EQ(parseServiceState("nonsense"), ServiceState::Unknown);
}

TEST(ServicesTest, ReadsUnitFiles) {
    const auto result = parseUnitFiles(kUnitFiles);

    EXPECT_TRUE(has(result, "cron.service"));
    EXPECT_TRUE(has(result, "ssh.service"));
    EXPECT_TRUE(has(result, "apache2.service"));
}

// The header names the columns and the summary counts them; neither
// is a unit.
TEST(ServicesTest, TheHeaderAndSummaryAreNotUnits) {
    const auto result = parseUnitFiles(kUnitFiles);

    EXPECT_FALSE(has(result, "UNIT"));
    EXPECT_FALSE(has(result, "7"));
    EXPECT_EQ(result.services.size(), 6u);
}

// Timers and sockets are real, and they are a different question.
TEST(ServicesTest, OnlyServicesAreServices) {
    const auto result = parseUnitFiles(kUnitFiles);

    EXPECT_FALSE(has(result, "systemd-tmpfiles-clean.timer"));
}

// "Enabled" is the question people mean by what this machine runs:
// not what is installed, and not what happens to be running this
// minute.
TEST(ServicesTest, CountsWhatStartsAtBoot) {
    const auto result = parseUnitFiles(kUnitFiles);

    EXPECT_EQ(result.enabled, 2u);

    for (const auto& service : result.services) {
        if (service.name == "cron.service") {
            EXPECT_TRUE(service.startsAtBoot());
        }

        if (service.name == "apt-daily.service") {
            EXPECT_FALSE(service.startsAtBoot());
        }
    }
}

TEST(ServicesTest, MaskedIsNotEnabled) {
    const auto result = parseUnitFiles(kUnitFiles);

    for (const auto& service : result.services) {
        if (service.name == "apache2.service") {
            EXPECT_EQ(service.state, ServiceState::Masked);
            EXPECT_FALSE(service.startsAtBoot());
        }
    }
}

TEST(ServicesTest, MarksFailedUnits) {
    ServicesResult result = parseUnitFiles(kUnitFiles);

    applyFailedUnits(
        result,
        "● ssh.service    loaded failed failed OpenBSD Secure Shell\n");

    EXPECT_EQ(result.failedCount, 1u);

    for (const auto& service : result.services) {
        if (service.name == "ssh.service") {
            EXPECT_TRUE(service.failed);
        }
    }
}

// A unit that failed but has no unit file on disk is still a fact
// about the machine.
TEST(ServicesTest, AFailedUnitWithNoFileIsStillReported) {
    ServicesResult result = parseUnitFiles(kUnitFiles);

    const std::size_t before = result.services.size();

    applyFailedUnits(
        result, "● ghost.service loaded failed failed Something\n");

    EXPECT_EQ(result.services.size(), before + 1);
    EXPECT_TRUE(has(result, "ghost.service"));
}

TEST(ServicesTest, HandlesEmptyOutput) {
    const auto result = parseUnitFiles("");

    EXPECT_TRUE(result.services.empty());
    EXPECT_EQ(result.enabled, 0u);
}

// A service belongs to a component: removing the component takes the
// service with it, and a service whose component nobody asked for is
// a service running for no stated reason.
TEST(ServicesTest, LinksServicesToWhatInstalledThem) {
    ServicesResult result = parseUnitFiles(kUnitFiles);

    nexus::system::applyDpkgOwners(
        result,
        "cron: /usr/lib/systemd/system/cron.service\n"
        "openssh-server: /usr/lib/systemd/system/ssh.service\n"
        "dpkg-query: no path found matching pattern "
        "/etc/systemd/system/cron.service\n");

    for (const auto& service : result.services) {
        if (service.name == "cron.service") {
            EXPECT_EQ(service.owner, "cron");
        }

        if (service.name == "ssh.service") {
            EXPECT_EQ(service.owner, "openssh-server");
        }

        if (service.name == "apt-daily.service") {
            EXPECT_TRUE(service.owner.empty());
        }
    }
}

// dpkg prints what it knows and complains about the rest, both
// together, so the shape of the line is what separates them.
TEST(ServicesTest, ComplaintsAreNotOwners) {
    ServicesResult result = parseUnitFiles(kUnitFiles);

    nexus::system::applyDpkgOwners(
        result,
        "dpkg-query: no path found matching pattern "
        "/lib/systemd/system/cron.service\n");

    for (const auto& service : result.services) {
        EXPECT_TRUE(service.owner.empty());
    }
}

// A diversion lists several packages for one path; the first shipped
// it.
TEST(ServicesTest, TheFirstOwnerWins) {
    ServicesResult result = parseUnitFiles(kUnitFiles);

    nexus::system::applyDpkgOwners(
        result,
        "cron: /usr/lib/systemd/system/cron.service\n"
        "other: /lib/systemd/system/cron.service\n");

    for (const auto& service : result.services) {
        if (service.name == "cron.service") {
            EXPECT_EQ(service.owner, "cron");
        }
    }
}

TEST(ServicesTest, AnUnownedServiceStaysUnowned) {
    ServicesResult result = parseUnitFiles(kUnitFiles);

    nexus::system::applyDpkgOwners(result, "");

    for (const auto& service : result.services) {
        EXPECT_TRUE(service.owner.empty());
    }
}

// A unit with Restart=always never reaches "failed": systemd retries
// it forever, so it is permanently activating instead. Checking only
// for failure never sees it, and a service respawning every five
// seconds since boot is as broken as one that gave up.
TEST(ServicesTest, AUnitStuckRestartingIsReported) {
    ServicesResult result = parseUnitFiles(kUnitFiles);

    nexus::system::applyRestartingUnits(
        result,
        "sentinel.service loaded activating auto-restart "
        "Sentinel Cyber Defense\n");

    EXPECT_EQ(result.restartingCount, 1u);
    EXPECT_EQ(result.failedCount, 0u);
    EXPECT_TRUE(has(result, "sentinel.service"));

    for (const auto& service : result.services) {
        if (service.name == "sentinel.service") {
            EXPECT_TRUE(service.restarting);
            EXPECT_FALSE(service.failed);
        }
    }
}

TEST(ServicesTest, RestartingAndFailedAreCountedSeparately) {
    ServicesResult result = parseUnitFiles(kUnitFiles);

    nexus::system::applyFailedUnits(
        result, "ssh.service loaded failed failed Shell\n");
    nexus::system::applyRestartingUnits(
        result, "cron.service loaded activating auto-restart Cron\n");

    EXPECT_EQ(result.failedCount, 1u);
    EXPECT_EQ(result.restartingCount, 1u);
}
