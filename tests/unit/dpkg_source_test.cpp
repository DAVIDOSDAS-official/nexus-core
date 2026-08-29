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

std::size_t countGaps(
    const DpkgSourceResult& result,
    ModelGapKind kind
) {
    std::size_t count = 0;

    for (const auto& gap : result.gaps) {
        if (gap.kind == kind) {
            count += 1;
        }
    }

    return count;
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

TEST(DpkgSourceTest, RecordsAlternativesAsAModelGap) {
    const auto result = loadFixture(
        "Package: apt\n"
        "Status: install ok installed\n"
        "Version: 2.8.3\n"
        "Depends: base-passwd | adduser\n"
    );

    EXPECT_EQ(countGaps(result, ModelGapKind::Alternatives), 1u);
}

TEST(DpkgSourceTest, RecordsVersionConstraintsAsModelGaps) {
    const auto result = loadFixture(
        "Package: apt\n"
        "Status: install ok installed\n"
        "Version: 2.8.3\n"
        "Depends: libc6 (>= 2.38), gpgv\n"
    );

    EXPECT_EQ(countGaps(result, ModelGapKind::VersionConstraint), 1u);
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
    EXPECT_EQ(result.representableClauses, 1u);
}

TEST(DpkgSourceTest, FirstAlternativeIsSelectedButRecorded) {
    // The model can only hold one name. Choosing the first is a
    // decision, not a fact, so it must appear in the gap list.
    const auto result = loadFixture(
        "Package: apt\n"
        "Status: install ok installed\n"
        "Version: 2.8.3\n"
        "Depends: base-passwd | adduser\n"
    );

    ASSERT_EQ(result.components.size(), 1u);
    EXPECT_TRUE(hasCapability(
        result.components[0].requiredCapabilities(),
        "base-passwd"
    ));
    EXPECT_FALSE(hasCapability(
        result.components[0].requiredCapabilities(),
        "adduser"
    ));
    EXPECT_EQ(countGaps(result, ModelGapKind::Alternatives), 1u);
}

TEST(DpkgSourceTest, MissingStatusFileThrows) {
    const DpkgSource source("/nonexistent/dpkg/status");

    EXPECT_THROW(source.load(), std::runtime_error);
}
