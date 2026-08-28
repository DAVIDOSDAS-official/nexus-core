#include <gtest/gtest.h>

#include <nexus/resolver.hpp>

TEST(ResolverTest, FindsProviderForCapability) {
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

    nexus::Capability required("audio");

    const nexus::Component* provider =
        resolver.findProvider(required);

    ASSERT_NE(provider, nullptr);
    EXPECT_EQ(provider->id(), "audio.pipewire");
}

TEST(ResolverTest, ReturnsNullWhenNoProviderExists) {
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

    nexus::Capability required("bluetooth");

    const nexus::Component* provider =
        resolver.findProvider(required);

    EXPECT_EQ(provider, nullptr);
}
