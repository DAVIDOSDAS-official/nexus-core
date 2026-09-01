#include <gtest/gtest.h>

#include <algorithm>
#include <sstream>

#include <nexus/system/control_file.hpp>
#include <nexus/system/dpkg_source.hpp>

using nexus::Capability;
using nexus::Component;
using nexus::ComponentType;
using nexus::system::DpkgSource;
using nexus::system::DpkgSourceResult;
using nexus::system::ModelGapKind;
using nexus::system::parseControlStream;

namespace {

DpkgSourceResult loadFixture(const std::string& text) {
    std::istringstream input(text);
    const DpkgSource source;

    return source.loadFromStanzas(parseControlStream(input));
}

bool hasCapability(
    const std::vector<Capability>& capabilities,
    const std::string& name
) {
    return std::any_of(
        capabilities.begin(),
        capabilities.end(),
        [&](const Capability& capability) {
            return capability.name() == name;
        }
    );
}


}

TEST(DpkgSourceTest, SkipsPackagesThatAreNotInstalled) {
    const auto result = loadFixture(
        "Package: installed-one\n"
        "Status: install ok installed\n"
        "Version: 1.0\n"
        "\n"
        "Package: removed-one\n"
        "Status: deinstall ok config-files\n"
        "Version: 1.0\n"
    );

    ASSERT_EQ(result.components.size(), 1u);
    EXPECT_EQ(result.components[0].id(), "installed-one");
    EXPECT_EQ(result.stanzasSkipped, 1u);
}

TEST(DpkgSourceTest, PackageProvidesItsOwnName) {
    const auto result = loadFixture(
        "Package: libc6\n"
        "Status: install ok installed\n"
        "Version: 2.39\n"
    );

    ASSERT_EQ(result.components.size(), 1u);
    EXPECT_TRUE(hasCapability(
        result.components[0].providedCapabilities(),
        "libc6"
    ));
}

TEST(DpkgSourceTest, MapsProvidesToCapabilities) {
    const auto result = loadFixture(
        "Package: mawk\n"
        "Status: install ok installed\n"
        "Version: 1.3.4\n"
        "Provides: awk\n"
    );

    ASSERT_EQ(result.components.size(), 1u);
    EXPECT_TRUE(hasCapability(
        result.components[0].providedCapabilities(),
        "awk"
    ));
}

TEST(DpkgSourceTest, MapsDependsToRequiredCapabilities) {
    const auto result = loadFixture(
        "Package: apt\n"
        "Status: install ok installed\n"
        "Version: 2.8.3\n"
        "Depends: gpgv, libc6 (>= 2.38)\n"
    );

    ASSERT_EQ(result.components.size(), 1u);

    const auto& required = result.components[0].requiredCapabilities();

    EXPECT_TRUE(hasCapability(required, "gpgv"));
    EXPECT_TRUE(hasCapability(required, "libc6"));
}

TEST(DpkgSourceTest, ClassifiesSectionAsComponentType) {
    const auto result = loadFixture(
        "Package: libc6\n"
        "Status: install ok installed\n"
        "Section: libs\n"
        "Version: 2.39\n"
        "\n"
        "Package: pipewire\n"
        "Status: install ok installed\n"
        "Section: universe/sound\n"
        "Version: 1.0\n"
    );

    ASSERT_EQ(result.components.size(), 2u);
    EXPECT_EQ(result.components[0].type(), ComponentType::Library);
    EXPECT_EQ(result.components[1].type(), ComponentType::Audio);
}

// The following tests are the point of this source: the component model
// cannot represent everything real package metadata contains, and the
// gaps must be reported rather than silently dropped.

TEST(DpkgSourceTest, KeepsEveryAlternative) {
    const auto result = loadFixture(
        "Package: apt\n"
        "Status: install ok installed\n"
        "Version: 2.8.3\n"
        "Depends: base-passwd (>= 3.6.1) | adduser\n"
    );

    ASSERT_EQ(result.components.size(), 1u);
    ASSERT_EQ(result.components[0].requirements().size(), 1u);

    const auto& requirement = result.components[0].requirements()[0];

    ASSERT_EQ(requirement.alternatives.size(), 2u);
    EXPECT_EQ(requirement.alternatives[0].capability, "base-passwd");
    ASSERT_TRUE(requirement.alternatives[0].version.has_value());
    EXPECT_EQ(requirement.alternatives[0].version->version, "3.6.1");
    EXPECT_EQ(requirement.alternatives[1].capability, "adduser");
    EXPECT_TRUE(requirement.hasChoice());
}

TEST(DpkgSourceTest, KeepsVersionConstraintsOnRequirements) {
    const auto result = loadFixture(
        "Package: apt\n"
        "Status: install ok installed\n"
        "Version: 2.8.3\n"
        "Depends: libc6 (>= 2.38), gpgv\n"
    );

    ASSERT_EQ(result.components.size(), 1u);

    const auto& requirements = result.components[0].requirements();

    ASSERT_EQ(requirements.size(), 2u);
    ASSERT_TRUE(requirements[0].alternatives[0].version.has_value());
    EXPECT_EQ(
        requirements[0].alternatives[0].version->version,
        "2.38"
    );
    EXPECT_TRUE(requirements[1].alternatives[0].isUnversioned());
}

TEST(DpkgSourceTest, StoresConflictsOnTheComponent) {
    const auto result = loadFixture(
        "Package: binutils\n"
        "Status: install ok installed\n"
        "Version: 2.42\n"
        "Conflicts: binutils-multiarch (<< 2.27-8)\n"
        "Breaks: aptitude (<< 0.8.10)\n"
    );

    ASSERT_EQ(result.components.size(), 1u);

    const auto& conflicts = result.components[0].conflicts();

    ASSERT_EQ(conflicts.size(), 2u);
    EXPECT_EQ(conflicts[0].capability, "binutils-multiarch");
    ASSERT_TRUE(conflicts[0].version.has_value());
    EXPECT_EQ(conflicts[0].version->version, "2.27-8");
    EXPECT_EQ(conflicts[1].capability, "aptitude");
}

TEST(DpkgSourceTest, StoresTheVersionOfAProvidedCapability) {
    // perl provides libnet-perl at 3.15, which has nothing to do with
    // perl's own version. Storing the package version here is what
    // produced false conflicts before capabilities carried versions.
    const auto result = loadFixture(
        "Package: perl\n"
        "Status: install ok installed\n"
        "Version: 5.38.2-3.2\n"
        "Provides: libnet-perl (= 1:3.15), libcgi-pm-perl\n"
    );

    ASSERT_EQ(result.components.size(), 1u);

    const auto& provided = result.components[0].providedCapabilities();

    bool sawVersioned = false;
    bool sawUnversioned = false;

    for (const auto& capability : provided) {
        if (capability.name() == "libnet-perl") {
            EXPECT_EQ(capability.version(), "1:3.15");
            sawVersioned = true;
        }

        if (capability.name() == "libcgi-pm-perl") {
            EXPECT_FALSE(capability.hasVersion());
            sawUnversioned = true;
        }
    }

    EXPECT_TRUE(sawVersioned);
    EXPECT_TRUE(sawUnversioned);
}

TEST(DpkgSourceTest, CountsRepresentableClauses) {
    const auto result = loadFixture(
        "Package: apt\n"
        "Status: install ok installed\n"
        "Version: 2.8.3\n"
        "Depends: gpgv, libc6 (>= 2.38), base-passwd | adduser\n"
    );

    EXPECT_EQ(result.dependencyClauses, 3u);
    EXPECT_EQ(result.representableClauses, 3u);
}

TEST(DpkgSourceTest, NoLongerGuessesBetweenAlternatives) {
    // Both alternatives are kept, so nothing has to be recorded as a
    // guess. The choice moves to the solver, where it belongs.
    const auto result = loadFixture(
        "Package: apt\n"
        "Status: install ok installed\n"
        "Version: 2.8.3\n"
        "Depends: base-passwd | adduser\n"
    );

    ASSERT_EQ(result.components.size(), 1u);
    EXPECT_TRUE(result.gaps.empty());
}

TEST(DpkgSourceTest, ReadsArchitectureAndMultiArch) {
    const auto result = loadFixture(
        "Package: libc6\n"
        "Status: install ok installed\n"
        "Architecture: amd64\n"
        "Multi-Arch: same\n"
        "Version: 2.39\n"
    );

    ASSERT_EQ(result.components.size(), 1u);
    EXPECT_EQ(result.components[0].architecture(), "amd64");
    EXPECT_EQ(result.components[0].multiArch(), nexus::MultiArch::Same);
}

TEST(DpkgSourceTest, QualifiesIdsOnlyWhenTheNameIsAmbiguous) {
    const auto result = loadFixture(
        "Package: libc6\n"
        "Status: install ok installed\n"
        "Architecture: amd64\n"
        "Multi-Arch: same\n"
        "Version: 2.39\n"
        "\n"
        "Package: libc6\n"
        "Status: install ok installed\n"
        "Architecture: i386\n"
        "Multi-Arch: same\n"
        "Version: 2.39\n"
        "\n"
        "Package: apt\n"
        "Status: install ok installed\n"
        "Architecture: amd64\n"
        "Version: 2.8.3\n"
    );

    ASSERT_EQ(result.components.size(), 3u);
    EXPECT_EQ(result.components[0].id(), "libc6:amd64");
    EXPECT_EQ(result.components[1].id(), "libc6:i386");

    // Unambiguous names keep their plain form.
    EXPECT_EQ(result.components[2].id(), "apt");

    // Both still provide the plain capability name.
    EXPECT_EQ(result.components[0].name(), "libc6");
    EXPECT_EQ(result.components[1].name(), "libc6");
}

TEST(DpkgSourceTest, KeepsArchitectureQualifiersOnRequirements) {
    const auto result = loadFixture(
        "Package: app\n"
        "Status: install ok installed\n"
        "Architecture: amd64\n"
        "Version: 1.0\n"
        "Depends: libc6:i386, libfoo:any\n"
    );

    ASSERT_EQ(result.components.size(), 1u);

    const auto& requirements = result.components[0].requirements();

    ASSERT_EQ(requirements.size(), 2u);
    ASSERT_TRUE(requirements[0].alternatives[0].architecture.has_value());
    EXPECT_EQ(*requirements[0].alternatives[0].architecture, "i386");
    EXPECT_EQ(*requirements[1].alternatives[0].architecture, "any");
}

TEST(DpkgSourceTest, MissingStatusFileThrows) {
    const DpkgSource source("/nonexistent/dpkg/status");

    EXPECT_THROW(source.load(), std::runtime_error);
}

// Recommendations are clauses too, and the source has to keep all of
// them or the reachability walk cannot follow what it never received.
TEST(DpkgSourceTest, KeepsEveryRecommendedAlternative) {
    const auto result = loadFixture(
        "Package: wine-stable-amd64\n"
        "Status: install ok installed\n"
        "Version: 9.0\n"
        "Recommends: libodbc2 | libodbc1, libgphoto2-6\n"
    );

    ASSERT_EQ(result.components.size(), 1u);

    const auto& recommendations =
        result.components[0].recommendations();

    ASSERT_EQ(recommendations.size(), 2u);
    ASSERT_EQ(recommendations[0].alternatives.size(), 2u);
    EXPECT_EQ(recommendations[0].alternatives[0].capability,
              "libodbc2");
    EXPECT_EQ(recommendations[0].alternatives[1].capability,
              "libodbc1");
    EXPECT_EQ(recommendations[1].alternatives.size(), 1u);
}
