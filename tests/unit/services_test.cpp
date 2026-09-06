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
