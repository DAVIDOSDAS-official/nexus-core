#include <gtest/gtest.h>

#include <algorithm>

#include <nexus/composition.hpp>

using nexus::compose;
using nexus::Composition;
using nexus::ConditionalPreference;
using nexus::Constraint;
using nexus::Profile;
using nexus::Requirement;

namespace {

Profile profileOf(
    const std::string& name,
    const std::vector<std::string>& requires_
) {
    Profile profile;

    profile.name = name;
    profile.description = name + " description";

    for (const std::string& capability : requires_) {
        profile.requirements.push_back(
            Requirement(Constraint(capability)));
    }

    return profile;
}

bool asks(const Profile& profile, const std::string& capability) {
    for (const Requirement& requirement : profile.requirements) {
        for (const Constraint& option : requirement.alternatives) {
            if (option.capability == capability) {
                return true;
            }
        }
    }

    return false;
}

}

TEST(CompositionTest, OneProfileComposesToItself) {
    const auto composed =
        compose({profileOf("gaming", {"steam", "vulkan-driver"})});

    EXPECT_EQ(composed.profile.requirements.size(), 2u);
    EXPECT_EQ(composed.sources.size(), 1u);
    EXPECT_TRUE(composed.shared.empty());
}

// Somebody who picks school and gaming wants a spreadsheet and Steam.
TEST(CompositionTest, TakesTheUnionOfRequirements) {
    const auto composed = compose({
        profileOf("school", {"spreadsheet", "web-browser"}),
        profileOf("gaming", {"steam", "vulkan-driver"})
    });

    EXPECT_EQ(composed.profile.requirements.size(), 4u);
    EXPECT_TRUE(asks(composed.profile, "spreadsheet"));
    EXPECT_TRUE(asks(composed.profile, "steam"));
}

// Which is why the combined total is smaller than the sum.
TEST(CompositionTest, SharedRequirementsAppearOnceAndAreReported) {
    const auto composed = compose({
        profileOf("school", {"web-browser", "text-editor"}),
        profileOf("basic", {"web-browser", "file-manager"})
    });

    EXPECT_EQ(composed.profile.requirements.size(), 3u);
    ASSERT_EQ(composed.shared.size(), 1u);
    EXPECT_EQ(composed.shared[0], "web-browser");
}

TEST(CompositionTest, PreferencesMerge) {
    Profile first = profileOf("a", {"audio-server"});
    first.preferred["audio-server"] = "pipewire";

    Profile second = profileOf("b", {"web-browser"});
    second.preferred["web-browser"] = "firefox";

    const auto composed = compose({first, second});

    EXPECT_EQ(composed.profile.preferred.size(), 2u);
    EXPECT_TRUE(composed.clashes.empty());
}

// Somebody has to win, and the person combining them deserves to know
// it happened.
TEST(CompositionTest, APreferenceClashIsReportedNotHidden) {
    Profile first = profileOf("minimalism", {"terminal-emulator"});
    first.preferred["terminal-emulator"] = "konsole";

    Profile second = profileOf("tiling", {"terminal-emulator"});
    second.preferred["terminal-emulator"] = "foot";

    const auto composed = compose({first, second});

    // The first named wins.
    EXPECT_EQ(
        composed.profile.preferred.at("terminal-emulator"), "konsole");

    ASSERT_EQ(composed.clashes.size(), 1u);
    EXPECT_EQ(composed.clashes[0].capability, "terminal-emulator");
    EXPECT_EQ(composed.clashes[0].chosen, "konsole");
    EXPECT_EQ(composed.clashes[0].overridden, "foot");
    EXPECT_EQ(composed.clashes[0].chosenBy, "minimalism");
    EXPECT_EQ(composed.clashes[0].overriddenBy, "tiling");
}

TEST(CompositionTest, AgreementIsNotAClash) {
    Profile first = profileOf("a", {"audio-server"});
    first.preferred["audio-server"] = "pipewire";

    Profile second = profileOf("b", {"audio-server"});
    second.preferred["audio-server"] = "pipewire";

    EXPECT_TRUE(compose({first, second}).clashes.empty());
}

TEST(CompositionTest, ArchitecturesAccumulate) {
    Profile plain = profileOf("basic", {"web-browser"});
    plain.architecture = "amd64";

    Profile gaming = profileOf("gaming", {"steam"});
    gaming.additionalArchitectures = {"i386"};

    const auto composed = compose({plain, gaming});

    EXPECT_EQ(composed.profile.architecture, "amd64");
    ASSERT_EQ(composed.profile.additionalArchitectures.size(), 1u);
    EXPECT_EQ(composed.profile.additionalArchitectures[0], "i386");
}

TEST(CompositionTest, ConditionalPreferencesCombineWithoutDuplicates) {
    Profile first = profileOf("a", {"vulkan"});
    first.conditionalPreferences.push_back(
        ConditionalPreference{"gpu-vendor-amd", "mesa"});

    Profile second = profileOf("b", {"vulkan"});
    second.conditionalPreferences.push_back(
        ConditionalPreference{"gpu-vendor-amd", "mesa"});
    second.conditionalPreferences.push_back(
        ConditionalPreference{"gpu-vendor-nvidia", "nvidia-driver"});

    const auto composed = compose({first, second});

    EXPECT_EQ(composed.profile.conditionalPreferences.size(), 2u);
}

TEST(CompositionTest, DescriptionsAreJoined) {
    const auto composed = compose({
        profileOf("one", {"a"}),
        profileOf("two", {"b"})
    });

    EXPECT_NE(
        composed.profile.description.find("one description"),
        std::string::npos);
    EXPECT_NE(
        composed.profile.description.find("two description"),
        std::string::npos);
}

TEST(CompositionTest, ComposingNothingGivesAnEmptyProfile) {
    const auto composed = compose({});

    EXPECT_TRUE(composed.profile.requirements.empty());
    EXPECT_TRUE(composed.sources.empty());
}
