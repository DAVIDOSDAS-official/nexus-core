#include <gtest/gtest.h>

#include <nexus/resolver.hpp>

TEST(ResolverTest, FindsSingleProvider) {
    nexus::Component pipewire(
        "audio.pipewire",
        "PipeWire",
        "1.0",
        nexus::ComponentType::Audio
    );

    pipewire.addProvidedCapability(
        nexus::Capability("audio")
    );

    nexus::Resolver resolver({pipewire});

    nexus::ResolutionResult result =
        resolver.resolve(nexus::Capability("audio"));

    EXPECT_EQ(
        result.status,
        nexus::ResolutionStatus::Success
    );

    ASSERT_EQ(result.candidates.size(), 1);
    EXPECT_EQ(result.candidates[0], "audio.pipewire");

    EXPECT_EQ(
        result.selectedProvider,
        "audio.pipewire"
    );
}

TEST(ResolverTest, ReportsMissingProvider) {
    nexus::Component pipewire(
        "audio.pipewire",
        "PipeWire",
        "1.0",
        nexus::ComponentType::Audio
    );

    pipewire.addProvidedCapability(
        nexus::Capability("audio")
    );

    nexus::Resolver resolver({pipewire});

    nexus::ResolutionResult result =
        resolver.resolve(nexus::Capability("bluetooth"));

    EXPECT_EQ(
        result.status,
        nexus::ResolutionStatus::NotFound
    );

    EXPECT_TRUE(result.candidates.empty());
    EXPECT_TRUE(result.selectedProvider.empty());
}

TEST(ResolverTest, ReportsAmbiguousProvider) {
    nexus::Component pipewire(
        "audio.pipewire",
        "PipeWire",
        "1.0",
        nexus::ComponentType::Audio
    );

    pipewire.addProvidedCapability(
        nexus::Capability("audio")
    );

    nexus::Component pulseaudio(
        "audio.pulseaudio",
        "PulseAudio",
        "1.0",
        nexus::ComponentType::Audio
    );

    pulseaudio.addProvidedCapability(
        nexus::Capability("audio")
    );

    nexus::Resolver resolver({pipewire, pulseaudio});

    nexus::ResolutionResult result =
        resolver.resolve(nexus::Capability("audio"));

    EXPECT_EQ(
        result.status,
        nexus::ResolutionStatus::Ambiguous
    );

    EXPECT_EQ(result.candidates.size(), 2);
    EXPECT_TRUE(result.selectedProvider.empty());
}
