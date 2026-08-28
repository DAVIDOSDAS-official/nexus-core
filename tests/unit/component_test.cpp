#include <gtest/gtest.h>

#include <nexus/component.hpp>

TEST(ComponentTest, StoresIdentityCorrectly) {
    nexus::Component component(
        "desktop.kde",
        "KDE Plasma",
        "6.0",
        nexus::ComponentType::Desktop
    );

    EXPECT_EQ(component.id(), "desktop.kde");
    EXPECT_EQ(component.name(), "KDE Plasma");
    EXPECT_EQ(component.version(), "6.0");
    EXPECT_EQ(component.type(), nexus::ComponentType::Desktop);
}

TEST(ComponentTest, StoresCapabilitiesCorrectly) {
    nexus::Component kde(
        "desktop.kde",
        "KDE Plasma",
        "6.0",
        nexus::ComponentType::Desktop
    );

    kde.addProvidedCapability(nexus::Capability("desktop"));
    kde.addProvidedCapability(nexus::Capability("graphical-session"));

    kde.addRequiredCapability(nexus::Capability("audio"));

    kde.addRecommendedCapability(nexus::Capability("pipewire"));

    ASSERT_EQ(kde.providedCapabilities().size(), 2);
    ASSERT_EQ(kde.requiredCapabilities().size(), 1);
    ASSERT_EQ(kde.recommendedCapabilities().size(), 1);

    EXPECT_EQ(kde.providedCapabilities()[0].name(), "desktop");
    EXPECT_EQ(kde.providedCapabilities()[1].name(), "graphical-session");

    EXPECT_EQ(kde.requiredCapabilities()[0].name(), "audio");

    EXPECT_EQ(kde.recommendedCapabilities()[0].name(), "pipewire");
}

TEST(ComponentTest, StoresRequiredCapabilitiesCorrectly) {
    nexus::Component kde(
        "desktop.kde",
        "KDE Plasma",
        "6.0",
        nexus::ComponentType::Desktop
    );

    kde.addRequiredCapability(
        nexus::Capability("graphical-session")
    );

    kde.addRequiredCapability(
        nexus::Capability("audio")
    );

    const auto& requirements = kde.requiredCapabilities();

    ASSERT_EQ(requirements.size(), 2);
    EXPECT_EQ(requirements[0].name(), "graphical-session");
    EXPECT_EQ(requirements[1].name(), "audio");
}
