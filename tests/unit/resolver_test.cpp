#include <gtest/gtest.h>

#include <nexus/capability.hpp>
#include <nexus/component.hpp>
#include <nexus/resolver.hpp>

using namespace nexus;

TEST(ResolverTest, FindsSingleProvider) {
    Component wayland(
        "display.wayland",
        "Wayland",
        "1.0",
        ComponentType::DisplayServer
    );

    wayland.addProvidedCapability(
        Capability("graphical-session")
    );

    Resolver resolver({wayland});

    ResolutionRequest request{
        Capability("graphical-session"),
        std::nullopt,
        std::nullopt
    };

    const ResolutionResult result =
        resolver.resolve(request);

    EXPECT_EQ(result.status, ResolutionStatus::Success);
    EXPECT_EQ(result.selectedProvider, "display.wayland");
}

TEST(ResolverTest, ReportsMissingProvider) {
    Resolver resolver({});

    ResolutionRequest request{
        Capability("audio"),
        std::nullopt,
        std::nullopt
    };

    const ResolutionResult result =
        resolver.resolve(request);

    EXPECT_EQ(result.status, ResolutionStatus::NotFound);
}

TEST(ResolverTest, ReportsAmbiguousProvider) {
    Component pipewire(
        "audio.pipewire",
        "PipeWire",
        "1.0",
        ComponentType::Audio
    );

    pipewire.addProvidedCapability(
        Capability("audio")
    );

    Component pulseaudio(
        "audio.pulseaudio",
        "PulseAudio",
        "1.0",
        ComponentType::Audio
    );

    pulseaudio.addProvidedCapability(
        Capability("audio")
    );

    Resolver resolver({pipewire, pulseaudio});

    ResolutionRequest request{
        Capability("audio"),
        std::nullopt,
        std::nullopt
    };

    const ResolutionResult result =
        resolver.resolve(request);

    EXPECT_EQ(result.status, ResolutionStatus::Ambiguous);
    EXPECT_EQ(result.candidates.size(), 2);
}

TEST(ResolverTest, SelectsPreferredProvider) {
    Component pipewire(
        "audio.pipewire",
        "PipeWire",
        "1.0",
        ComponentType::Audio
    );

    pipewire.addProvidedCapability(
        Capability("audio")
    );

    Component pulseaudio(
        "audio.pulseaudio",
        "PulseAudio",
        "1.0",
        ComponentType::Audio
    );

    pulseaudio.addProvidedCapability(
        Capability("audio")
    );

    Resolver resolver({pipewire, pulseaudio});

    ResolutionRequest request{
        Capability("audio"),
        std::string("audio.pulseaudio"),
        std::nullopt
    };

    const ResolutionResult result =
        resolver.resolve(request);

    EXPECT_EQ(result.status, ResolutionStatus::Success);
    EXPECT_EQ(result.selectedProvider, "audio.pulseaudio");
}

TEST(ResolverTest, SelectsRequiredProvider) {
    Component pipewire(
        "audio.pipewire",
        "PipeWire",
        "1.0",
        ComponentType::Audio
    );

    pipewire.addProvidedCapability(
        Capability("audio")
    );

    Component pulseaudio(
        "audio.pulseaudio",
        "PulseAudio",
        "1.0",
        ComponentType::Audio
    );

    pulseaudio.addProvidedCapability(
        Capability("audio")
    );

    Resolver resolver({pipewire, pulseaudio});

    ResolutionRequest request{
        Capability("audio"),
        std::nullopt,
        std::string("audio.pulseaudio")
    };

    const ResolutionResult result =
        resolver.resolve(request);

    EXPECT_EQ(result.status, ResolutionStatus::Success);
    EXPECT_EQ(result.selectedProvider, "audio.pulseaudio");
}

TEST(ResolverTest, RequiredProviderCannotBeSilentlyReplaced) {
    Component pipewire(
        "audio.pipewire",
        "PipeWire",
        "1.0",
        ComponentType::Audio
    );

    pipewire.addProvidedCapability(
        Capability("audio")
    );

    Resolver resolver({pipewire});

    ResolutionRequest request{
        Capability("audio"),
        std::nullopt,
        std::string("audio.pulseaudio")
    };

    const ResolutionResult result =
        resolver.resolve(request);

    EXPECT_EQ(result.status, ResolutionStatus::NotFound);
    EXPECT_TRUE(result.selectedProvider.empty());
}

TEST(ResolverTest, ResolvesRequiredDependency) {
    Component wayland(
        "display.wayland",
        "Wayland",
        "1.0",
        ComponentType::DisplayServer
    );

    wayland.addProvidedCapability(
        Capability("graphical-session")
    );

    Component kde(
        "desktop.kde",
        "KDE Plasma",
        "6.0",
        ComponentType::Desktop
    );

    kde.addProvidedCapability(
        Capability("desktop")
    );

    kde.addRequiredCapability(
        Capability("graphical-session")
    );

    Resolver resolver({kde, wayland});

    ResolutionRequest request{
        Capability("desktop"),
        std::nullopt,
        std::string("desktop.kde")
    };

    const ResolutionResult result =
        resolver.resolve(request);

    EXPECT_EQ(result.status, ResolutionStatus::Success);
    EXPECT_EQ(result.selectedProvider, "desktop.kde");

    ASSERT_EQ(result.plan.install.size(), 2);

    EXPECT_EQ(result.plan.install[0], "display.wayland");
    EXPECT_EQ(result.plan.install[1], "desktop.kde");
}

TEST(ResolverTest, ResolvesMultipleDependencyLevels) {
    Component xserver(
        "display.xserver",
        "X Server",
        "1.0",
        ComponentType::DisplayServer
    );

    xserver.addProvidedCapability(
        Capability("display-server")
    );

    Component wayland(
        "session.wayland",
        "Wayland Session",
        "1.0",
        ComponentType::Compositor
    );

    wayland.addProvidedCapability(
        Capability("graphical-session")
    );

    wayland.addRequiredCapability(
        Capability("display-server")
    );

    Component kde(
        "desktop.kde",
        "KDE Plasma",
        "6.0",
        ComponentType::Desktop
    );

    kde.addProvidedCapability(
        Capability("desktop")
    );

    kde.addRequiredCapability(
        Capability("graphical-session")
    );

    Resolver resolver({kde, wayland, xserver});

    ResolutionRequest request{
        Capability("desktop"),
        std::nullopt,
        std::string("desktop.kde")
    };

    const ResolutionResult result =
        resolver.resolve(request);

    EXPECT_EQ(result.status, ResolutionStatus::Success);
    EXPECT_EQ(result.selectedProvider, "desktop.kde");

    ASSERT_EQ(result.plan.install.size(), 3);

    EXPECT_EQ(result.plan.install[0], "display.xserver");
    EXPECT_EQ(result.plan.install[1], "session.wayland");
    EXPECT_EQ(result.plan.install[2], "desktop.kde");
}

TEST(ResolverTest, RejectsDependencyCycle) {
    Component componentA(
        "component.a",
        "Component A",
        "1.0",
        ComponentType::Utility
    );

    componentA.addProvidedCapability(
        Capability("capability-a")
    );

    componentA.addRequiredCapability(
        Capability("capability-b")
    );

    Component componentB(
        "component.b",
        "Component B",
        "1.0",
        ComponentType::Utility
    );

    componentB.addProvidedCapability(
        Capability("capability-b")
    );

    componentB.addRequiredCapability(
        Capability("capability-a")
    );

    Resolver resolver({componentA, componentB});

    ResolutionRequest request{
        Capability("capability-a"),
        std::nullopt,
        std::string("component.a")
    };

    const ResolutionResult result =
        resolver.resolve(request);

    EXPECT_EQ(result.status, ResolutionStatus::NotFound);
    EXPECT_NE(
        result.reason.find("cycle"),
        std::string::npos
    );
}
