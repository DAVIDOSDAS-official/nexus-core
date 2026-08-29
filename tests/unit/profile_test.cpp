#include <gtest/gtest.h>

#include <sstream>

#include <nexus/profile_check.hpp>
#include <nexus/system/profile_file.hpp>
#include <nexus/system/version.hpp>

using nexus::checkProfile;
using nexus::Component;
using nexus::ComponentType;
using nexus::ConflictDetector;
using nexus::Constraint;
using nexus::Profile;
using nexus::Requirement;
using nexus::Solver;
using nexus::system::parseProfileStream;

namespace {

Solver solverOver(std::vector<Component> components) {
    return Solver(
        std::move(components),
        ConflictDetector(
            [](const std::string& left, const std::string& right) {
                return nexus::system::compareVersions(left, right);
            }
        )
    );
}

Component make(const std::string& id) {
    return Component(id, id, "1.0", ComponentType::Application);
}

nexus::system::ProfileParseResult parse(const std::string& text) {
    std::istringstream input(text);
    return parseProfileStream(input);
}

}

TEST(ProfileFileTest, ParsesAProfile) {
    const auto result = parse(
        "Profile: gaming\n"
        "Description: Games and drivers\n"
        "Architecture: amd64\n"
        "Requires: steam, libgl1\n"
    );

    ASSERT_EQ(result.profiles.size(), 1u);
    EXPECT_EQ(result.profiles[0].name, "gaming");
    EXPECT_EQ(result.profiles[0].description, "Games and drivers");
    EXPECT_EQ(result.profiles[0].architecture, "amd64");
    EXPECT_EQ(result.profiles[0].requirements.size(), 2u);
}

TEST(ProfileFileTest, RequiresSupportsAlternatives) {
    const auto result = parse(
        "Profile: school\n"
        "Requires: libreoffice-writer | abiword\n"
    );

    ASSERT_EQ(result.profiles.size(), 1u);
    ASSERT_EQ(result.profiles[0].requirements.size(), 1u);
    EXPECT_TRUE(result.profiles[0].requirements[0].hasChoice());
}

TEST(ProfileFileTest, RequiresSupportsVersionConditions) {
    const auto result = parse(
        "Profile: dev\n"
        "Requires: gcc (>= 12)\n"
    );

    ASSERT_EQ(result.profiles.size(), 1u);

    const auto& option =
        result.profiles[0].requirements[0].alternatives[0];

    ASSERT_TRUE(option.version.has_value());
    EXPECT_EQ(option.version->version, "12");
}

TEST(ProfileFileTest, ParsesPreferencesAndHardRequirements) {
    const auto result = parse(
        "Profile: desktop\n"
        "Requires: audio\n"
        "Prefers: audio=pipewire\n"
        "Requires-Exactly: java=openjdk-21-jre\n"
    );

    ASSERT_EQ(result.profiles.size(), 1u);
    EXPECT_EQ(result.profiles[0].preferred.at("audio"), "pipewire");
    EXPECT_EQ(
        result.profiles[0].required.at("java"),
        "openjdk-21-jre"
    );
}

TEST(ProfileFileTest, ReadsAdditionalArchitectures) {
    const auto result = parse(
        "Profile: gaming\n"
        "Requires: steam\n"
        "Enables-Architectures: i386\n"
    );

    ASSERT_EQ(result.profiles.size(), 1u);
    ASSERT_EQ(
        result.profiles[0].additionalArchitectures.size(),
        1u
    );
    EXPECT_EQ(result.profiles[0].additionalArchitectures[0], "i386");
}

TEST(ProfileFileTest, ParsesSeveralProfilesFromOneStream) {
    const auto result = parse(
        "Profile: one\n"
        "Requires: a\n"
        "\n"
        "Profile: two\n"
        "Requires: b\n"
    );

    ASSERT_EQ(result.profiles.size(), 2u);
    EXPECT_EQ(result.profiles[0].name, "one");
    EXPECT_EQ(result.profiles[1].name, "two");
}

// A malformed profile must be reported, not silently half-applied.
TEST(ProfileFileTest, ReportsMalformedPreferences) {
    const auto result = parse(
        "Profile: broken\n"
        "Requires: a\n"
        "Prefers: nonsense-without-equals\n"
    );

    ASSERT_EQ(result.profiles.size(), 1u);
    EXPECT_TRUE(result.profiles[0].preferred.empty());
    EXPECT_FALSE(result.problems.empty());
}

TEST(ProfileFileTest, ReportsAProfileThatAsksForNothing) {
    const auto result = parse("Profile: empty\n");

    EXPECT_FALSE(result.problems.empty());
}

TEST(ProfileCheckTest, ReportsAFullySatisfiedProfile) {
    Profile profile;
    profile.name = "base";
    profile.requirements.push_back(Requirement(Constraint("libc6")));
    profile.requirements.push_back(Requirement(Constraint("tar")));

    const auto report = checkProfile(
        profile,
        solverOver({make("libc6"), make("tar")})
    );

    EXPECT_TRUE(report.complete());
    EXPECT_EQ(report.satisfied, 2u);
    EXPECT_EQ(report.missing, 0u);
}

TEST(ProfileCheckTest, NamesEachMissingRequirement) {
    Profile profile;
    profile.name = "gaming";
    profile.requirements.push_back(Requirement(Constraint("steam")));
    profile.requirements.push_back(Requirement(Constraint("libgl1")));

    const auto report =
        checkProfile(profile, solverOver({make("libgl1")}));

    EXPECT_FALSE(report.complete());
    ASSERT_EQ(report.items.size(), 2u);
    EXPECT_FALSE(report.items[0].satisfied);
    EXPECT_NE(
        report.items[0].blockedOn.find("steam"),
        std::string::npos
    );
    EXPECT_TRUE(report.items[1].satisfied);
}

// One missing requirement must not hide the state of the others.
// That is the whole reason requirements are checked separately.
TEST(ProfileCheckTest, OneFailureDoesNotMaskTheRest) {
    Profile profile;
    profile.name = "mixed";
    profile.requirements.push_back(Requirement(Constraint("absent")));
    profile.requirements.push_back(Requirement(Constraint("present")));

    const auto report =
        checkProfile(profile, solverOver({make("present")}));

    EXPECT_EQ(report.satisfied, 1u);
    EXPECT_EQ(report.missing, 1u);
}

TEST(ProfileCheckTest, AnAlternativeIsEnoughToSatisfy) {
    Profile profile;
    profile.name = "school";
    profile.requirements.push_back(Requirement(
        std::vector<Constraint>{
            Constraint("libreoffice-writer"),
            Constraint("abiword")
        }
    ));

    const auto report =
        checkProfile(profile, solverOver({make("abiword")}));

    EXPECT_TRUE(report.complete());
    EXPECT_EQ(report.items[0].provided.front(), "abiword");
}

TEST(ProfileCheckTest, ReportsWhatSatisfiedEachRequirement) {
    Component desktop = make("kde");
    desktop.addRequirement(Requirement(Constraint("audio")));

    Component pipewire = make("pipewire");
    pipewire.addProvidedCapability(nexus::Capability("audio"));

    Profile profile;
    profile.name = "desktop";
    profile.requirements.push_back(Requirement(Constraint("kde")));

    const auto report =
        checkProfile(profile, solverOver({desktop, pipewire}));

    ASSERT_TRUE(report.complete());
    EXPECT_EQ(report.items[0].provided.front(), "kde");
    EXPECT_EQ(report.items[0].provided.size(), 2u);
}
