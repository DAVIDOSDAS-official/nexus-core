#include <gtest/gtest.h>
#include <nexus/hardware/hardware.hpp>

#include <nexus/options.hpp>
#include <nexus/source.hpp>
#include <nexus/system/version.hpp>

using nexus::Capability;
using nexus::Component;
using nexus::ComponentType;
using nexus::ConflictDetector;
using nexus::findOptions;
using nexus::Solver;
using nexus::Source;

namespace {

ConflictDetector detector() {
    return ConflictDetector(
        [](const std::string& left, const std::string& right) {
            return nexus::system::compareVersions(left, right);
        }
    );
}

Component make(
    const std::string& id,
    const std::string& provides,
    Source source
) {
    Component component(id, id, "1.0", ComponentType::Application);

    component.addProvidedCapability(Capability(id));
    component.addProvidedCapability(Capability(provides));
    component.setSource(source);

    return component;
}

}

TEST(SourceTest, EverythingIsFromTheDistributionByDefault) {
    const Component component(
        "tree", "tree", "1.0", ComponentType::Application);

    EXPECT_EQ(component.source(), Source::Base);
}

// The ones that do not share a root filesystem are why "packages from
// several distributions" is achievable at all.
TEST(SourceTest, IsolatedSourcesAreTheOnesThatDoNotShareARoot) {
    EXPECT_FALSE(isIsolated(Source::Base));
    EXPECT_FALSE(isIsolated(Source::Detected));

    EXPECT_TRUE(isIsolated(Source::Flatpak));
    EXPECT_TRUE(isIsolated(Source::Container));
    EXPECT_TRUE(isIsolated(Source::Nix));
    EXPECT_TRUE(isIsolated(Source::AppImage));
}

TEST(SourceTest, EverySourceExplainsItsTradeOff) {
    for (const Source source : {
             Source::Base, Source::Flatpak, Source::Container,
             Source::Nix, Source::AppImage, Source::Detected}) {

        EXPECT_FALSE(toString(source).empty());
        EXPECT_FALSE(describe(source).empty());
    }
}

// The same capability from two places is two options, and which is
// which has to survive into the report.
TEST(SourceTest, OptionsCarryTheSourceTheyCameFrom) {
    const std::vector<Component> universe{
        make("kdenlive", "video-editor", Source::Base),
        make("org.kde.kdenlive", "video-editor", Source::Flatpak)
    };

    const auto report = findOptions(
        "video-editor", universe, {},
        Solver(universe, detector()), detector());

    ASSERT_EQ(report.options.size(), 2u);

    bool sawBase = false;
    bool sawFlatpak = false;

    for (const auto& option : report.options) {
        sawBase = sawBase || option.source == Source::Base;
        sawFlatpak = sawFlatpak || option.source == Source::Flatpak;
    }

    EXPECT_TRUE(sawBase);
    EXPECT_TRUE(sawFlatpak);
}

TEST(SourceTest, DetectedHardwareIsNotSomethingYouInstall) {
    nexus::hardware::HardwareInfo info;

    info.architecture = "amd64";

    EXPECT_EQ(nexus::hardware::asComponent(info).source(),
              Source::Detected);
}
