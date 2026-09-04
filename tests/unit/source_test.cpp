#include <gtest/gtest.h>
#include <nexus/system/pacman_source.hpp>
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

// "What can Flatpak give me" is a different question from "what are
// my options", and both are worth being able to ask.
TEST(SourceTest, OptionsCanBeLimitedToOneSource) {
    const std::vector<Component> universe{
        make("kdenlive", "video-editor", Source::Base),
        make("org.kde.kdenlive", "video-editor", Source::Flatpak),
        make("org.shotcut.Shotcut", "video-editor", Source::Flatpak)
    };

    const auto all = findOptions(
        "video-editor", universe, {},
        Solver(universe, detector()), detector());

    EXPECT_EQ(all.options.size(), 3u);

    const auto flatpakOnly = findOptions(
        "video-editor", universe, {},
        Solver(universe, detector()), detector(), "",
        nexus::AliasTable{}, Source::Flatpak);

    ASSERT_EQ(flatpakOnly.options.size(), 2u);

    for (const auto& option : flatpakOnly.options) {
        EXPECT_EQ(option.source, Source::Flatpak);
    }
}

// Naming a source outranks the order of alternatives, because asking
// for the Flatpak is a choice and alias order is only a default.
TEST(SourceTest, ANamedSourceOutranksAliasOrder) {
    nexus::Component base = make("kdenlive", "video-editor",
                                 Source::Base);
    nexus::Component flatpak = make("org.kde.kdenlive", "video-editor",
                                    Source::Flatpak);

    const std::vector<Component> universe{base, flatpak};

    Solver solver(universe, detector());

    nexus::Requirement wanted(std::vector<nexus::Constraint>{
        nexus::Constraint("kdenlive"),
        nexus::Constraint("org.kde.kdenlive")
    });

    // Without an opinion, the first alternative wins.
    nexus::SolverRequest plain;
    plain.requirements.push_back(wanted);

    const auto byOrder = solver.solve(plain);

    ASSERT_EQ(byOrder.status, nexus::SolverStatus::Success);
    EXPECT_EQ(byOrder.selected.front(), "kdenlive");

    // Naming Flatpak changes it.
    nexus::SolverRequest preferred;
    preferred.requirements.push_back(wanted);
    preferred.preferredSources = {Source::Flatpak};

    const auto bySource = solver.solve(preferred);

    ASSERT_EQ(bySource.status, nexus::SolverStatus::Success);
    EXPECT_EQ(bySource.selected.front(), "org.kde.kdenlive");
}

// Components from an isolated source do not share a root filesystem
// with the base system, so an Arch package's dependency cannot be met
// by a Debian package that happens to share a name. Without a scope
// the universe is one flat namespace, and an Arch kdenlive reported
// itself satisfied by 654 Debian components.
TEST(SourceTest, AnIsolatedComponentResolvesWithinItsOwnSource) {
    Component archApp("kdenlive", "kdenlive", "26.0",
                      ComponentType::Application);
    archApp.setSource(Source::Container);
    archApp.addProvidedCapability(Capability("kdenlive"));
    archApp.addRequirement(
        nexus::Requirement(nexus::Constraint("glib2")));

    // Only the base system has something called glib2.
    Component baseLibrary("glib2", "glib2", "2.0",
                          ComponentType::Library);
    baseLibrary.setSource(Source::Base);
    baseLibrary.addProvidedCapability(Capability("glib2"));

    Solver solver({archApp, baseLibrary}, detector());

    nexus::SolverRequest request;
    request.requirements.push_back(
        nexus::Requirement(nexus::Constraint("kdenlive")));

    // The Debian library must not satisfy the Arch package.
    EXPECT_EQ(solver.solve(request).status,
              nexus::SolverStatus::Unsatisfiable);
}

TEST(SourceTest, AnIsolatedComponentIsSatisfiedByItsOwnSource) {
    Component archApp("kdenlive", "kdenlive", "26.0",
                      ComponentType::Application);
    archApp.setSource(Source::Container);
    archApp.addProvidedCapability(Capability("kdenlive"));
    archApp.addRequirement(
        nexus::Requirement(nexus::Constraint("glib2")));

    Component archLibrary("glib2", "glib2", "2.0",
                          ComponentType::Library);
    archLibrary.setSource(Source::Container);
    archLibrary.addProvidedCapability(Capability("glib2"));

    Solver solver({archApp, archLibrary}, detector());

    nexus::SolverRequest request;
    request.requirements.push_back(
        nexus::Requirement(nexus::Constraint("kdenlive")));

    EXPECT_EQ(solver.solve(request).status,
              nexus::SolverStatus::Success);
}

// And a base package is not satisfied by a Flatpak either: the rule
// runs both ways.
TEST(SourceTest, ABasePackageIsNotSatisfiedByAnIsolatedOne) {
    Component baseApp("thing", "thing", "1.0",
                      ComponentType::Application);
    baseApp.setSource(Source::Base);
    baseApp.addProvidedCapability(Capability("thing"));
    baseApp.addRequirement(
        nexus::Requirement(nexus::Constraint("helper")));

    Component flatpakHelper("helper", "helper", "1.0",
                            ComponentType::Application);
    flatpakHelper.setSource(Source::Flatpak);
    flatpakHelper.addProvidedCapability(Capability("helper"));

    Solver solver({baseApp, flatpakHelper}, detector());

    nexus::SolverRequest request;
    request.requirements.push_back(
        nexus::Requirement(nexus::Constraint("thing")));

    EXPECT_EQ(solver.solve(request).status,
              nexus::SolverStatus::Unsatisfiable);
}

// Detected components describe the machine and belong to no source,
// so a hardware capability satisfies a requirement from anywhere.
TEST(SourceTest, DetectedComponentsSatisfyAnySource) {
    Component archApp("game", "game", "1.0",
                      ComponentType::Application);
    archApp.setSource(Source::Container);
    archApp.addProvidedCapability(Capability("game"));
    archApp.addRequirement(
        nexus::Requirement(nexus::Constraint("gpu-vendor-amd")));

    Component metal("system-hardware", "system-hardware", "0",
                    ComponentType::Driver);
    metal.setSource(Source::Detected);
    metal.addProvidedCapability(Capability("gpu-vendor-amd"));

    Solver solver({archApp, metal}, detector());

    nexus::SolverRequest request;
    request.requirements.push_back(
        nexus::Requirement(nexus::Constraint("game")));

    EXPECT_EQ(solver.solve(request).status,
              nexus::SolverStatus::Success);
}

// Arch's kdenlive and Debian's share a name and are not the same
// package.
TEST(SourceTest, IdentityDistinguishesSources) {
    Component base = make("kdenlive", "video-editor", Source::Base);
    Component arch = make("kdenlive", "video-editor",
                          Source::Container);

    const std::vector<Component> universe{base, arch};

    // Only the base one is installed.
    const auto report = findOptions(
        "video-editor", universe, {base},
        Solver(universe, detector()), detector());

    ASSERT_EQ(report.options.size(), 2u);

    for (const auto& option : report.options) {
        if (option.source == Source::Base) {
            EXPECT_TRUE(option.installed);
        } else {
            EXPECT_FALSE(option.installed);
        }
    }
}

// Scope propagates from whatever is selected, but the first
// requirement has nothing above it to inherit from. Costing an Arch
// package by asking for its name resolved whichever package answered
// to that name first, and reported a Debian dependency tree as the
// Arch one's.
TEST(SourceTest, CostingHappensWithinTheOptionsOwnSource) {
    Component debian("kdenlive", "kdenlive", "21.0",
                     ComponentType::Application);
    debian.setSource(Source::Base);
    debian.addProvidedCapability(Capability("kdenlive"));
    debian.addProvidedCapability(Capability("video-editor"));
    debian.addRequirement(
        nexus::Requirement(nexus::Constraint("qtbase")));

    Component qtbase("qtbase", "qtbase", "5.0",
                     ComponentType::Library);
    qtbase.setSource(Source::Base);
    qtbase.addProvidedCapability(Capability("qtbase"));

    // The Arch one has no dependencies at all.
    Component arch("kdenlive", "kdenlive", "26.0",
                   ComponentType::Application);
    arch.setSource(Source::Container);
    arch.addProvidedCapability(Capability("kdenlive"));
    arch.addProvidedCapability(Capability("video-editor"));

    const std::vector<Component> universe{debian, qtbase, arch};

    const auto report = findOptions(
        "video-editor", universe, {},
        Solver(universe, detector()), detector());

    ASSERT_EQ(report.options.size(), 2u);

    for (const auto& option : report.options) {
        if (option.source == Source::Container) {
            // One component, not the Debian package's two.
            EXPECT_EQ(option.componentCount, 1u);
            EXPECT_EQ(option.wouldAdd, 1u);
        } else {
            EXPECT_EQ(option.componentCount, 2u);
        }
    }
}

// Version ordering belongs to the ecosystem a component came from,
// not to the machine doing the comparing.
TEST(SourceTest, EachSourceOrdersVersionsItsOwnWay) {
    nexus::ConflictDetector detector;

    // The default is strict: 2.42.3-1 is not 2.42.3.
    detector = nexus::ConflictDetector(
        [](const std::string& left, const std::string& right) {
            return nexus::system::compareVersions(left, right);
        });

    // Arch packages compare at the constraint's precision.
    detector.setComparatorFor(
        Source::Container,
        [](const std::string& provided, const std::string& required) {
            return nexus::system::comparePacmanConstraint(
                provided, required);
        });

    const auto exactly =
        [](const std::string& name, const std::string& version) {
            nexus::Constraint constraint(name);

            constraint.version = nexus::VersionConstraint{
                nexus::VersionRelation::Exactly, version};

            return constraint;
        };

    Component base("util-linux", "util-linux", "2.42.3-1",
                   ComponentType::Library);
    base.setSource(Source::Base);
    base.addProvidedCapability(Capability("util-linux"));

    Component arch("util-linux-libs", "util-linux-libs", "2.42.3-1",
                   ComponentType::Library);
    arch.setSource(Source::Container);
    arch.addProvidedCapability(Capability("util-linux-libs"));

    // Debian rules: the revision is part of the version.
    EXPECT_FALSE(
        detector.matches(base, exactly("util-linux", "2.42.3")));

    // pacman rules: it is not, unless named.
    EXPECT_TRUE(
        detector.matches(arch, exactly("util-linux-libs", "2.42.3")));
}
