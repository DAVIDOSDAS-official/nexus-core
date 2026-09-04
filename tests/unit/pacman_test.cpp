#include <gtest/gtest.h>

#include <algorithm>

#include <nexus/system/pacman_source.hpp>

using nexus::Capability;
using nexus::Component;
using nexus::Source;
using nexus::VersionRelation;
using nexus::system::parsePacmanDatabase;
using nexus::system::parsePacmanDependency;

namespace {

// Copied from Arch's core.db rather than invented. A field name on
// its own line, values beneath, a blank line between -- nothing like
// a control file and nothing like repodata.
const char* kRealEntry =
    "%FILENAME%\n"
    "acl-2.4.0-1-x86_64.pkg.tar.zst\n"
    "\n"
    "%NAME%\n"
    "acl\n"
    "\n"
    "%BASE%\n"
    "acl\n"
    "\n"
    "%VERSION%\n"
    "2.4.0-1\n"
    "\n"
    "%DESC%\n"
    "Access control list utilities, libraries and headers\n"
    "\n"
    "%CSIZE%\n"
    "150821\n"
    "\n"
    "%ISIZE%\n"
    "353496\n"
    "\n"
    "%ARCH%\n"
    "x86_64\n"
    "\n"
    "%DEPENDS%\n"
    "glibc>=2.38\n"
    "attr\n"
    "\n"
    "%PROVIDES%\n"
    "libacl.so=1-64\n";

bool provides(const Component& component, const std::string& name) {
    const auto& capabilities = component.providedCapabilities();

    return std::any_of(
        capabilities.begin(),
        capabilities.end(),
        [&name](const Capability& capability) {
            return capability.name() == name;
        }
    );
}

}

TEST(PacmanTest, ReadsARealEntry) {
    const auto result = parsePacmanDatabase(kRealEntry);

    ASSERT_TRUE(result.error.empty());
    ASSERT_EQ(result.components.size(), 1u);

    const Component& component = result.components[0];

    EXPECT_EQ(component.id(), "acl");
    EXPECT_EQ(component.version(), "2.4.0-1");
    EXPECT_EQ(component.architecture(), "amd64");
    EXPECT_EQ(component.downloadSize(), 150821u);
    EXPECT_EQ(component.installedSize(), 353496u);
}

// The operator is written against the name with no spaces, and the
// longest must match first or ">=" is read as ">".
TEST(PacmanTest, ReadsDependencyOperators) {
    EXPECT_EQ(parsePacmanDependency("attr").capability, "attr");
    EXPECT_TRUE(parsePacmanDependency("attr").isUnversioned());

    const auto atLeast = parsePacmanDependency("glibc>=2.38");

    EXPECT_EQ(atLeast.capability, "glibc");
    ASSERT_TRUE(atLeast.version.has_value());
    EXPECT_EQ(atLeast.version->relation, VersionRelation::LaterOrEqual);
    EXPECT_EQ(atLeast.version->version, "2.38");

    EXPECT_EQ(
        parsePacmanDependency("foo>1.0").version->relation,
        VersionRelation::Later);
    EXPECT_EQ(
        parsePacmanDependency("foo<=1.0").version->relation,
        VersionRelation::EarlierOrEqual);
    EXPECT_EQ(
        parsePacmanDependency("foo=1.0").version->relation,
        VersionRelation::Exactly);
}

TEST(PacmanTest, ReadsDependencies) {
    const auto result = parsePacmanDatabase(kRealEntry);

    ASSERT_EQ(result.components.size(), 1u);

    const auto& requirements = result.components[0].requirements();

    ASSERT_EQ(requirements.size(), 2u);
    EXPECT_EQ(requirements[0].alternatives[0].capability, "glibc");
    EXPECT_EQ(requirements[1].alternatives[0].capability, "attr");
}

// Arch writes soname provides as libacl.so=1-64, so the version is
// part of the provide rather than separate.
TEST(PacmanTest, ReadsVersionedProvides) {
    const auto result = parsePacmanDatabase(kRealEntry);

    ASSERT_EQ(result.components.size(), 1u);
    EXPECT_TRUE(provides(result.components[0], "libacl.so"));

    for (const auto& capability :
         result.components[0].providedCapabilities()) {

        if (capability.name() == "libacl.so") {
            EXPECT_EQ(capability.version(), "1-64");
        }
    }
}

// An Arch package read on Arch comes from the base repositories; the
// same package read on Debian is only reachable through a container,
// and calling it Base there would promise something undeliverable.
TEST(PacmanTest, TheSourceIsToldNotAssumed) {
    EXPECT_EQ(
        parsePacmanDatabase(kRealEntry).components[0].source(),
        Source::Container);

    EXPECT_EQ(
        parsePacmanDatabase(kRealEntry, Source::Base)
            .components[0].source(),
        Source::Base);
}

TEST(PacmanTest, ReadsSeveralPackages) {
    const std::string two =
        std::string(kRealEntry) +
        "\n"
        "%FILENAME%\n"
        "attr-2.5.2-1-x86_64.pkg.tar.zst\n"
        "\n"
        "%NAME%\n"
        "attr\n"
        "\n"
        "%VERSION%\n"
        "2.5.2-1\n"
        "\n"
        "%ARCH%\n"
        "x86_64\n";

    const auto result = parsePacmanDatabase(two);

    ASSERT_EQ(result.components.size(), 2u);
    EXPECT_EQ(result.packagesRead, 2u);
    EXPECT_EQ(result.components[0].id(), "acl");
    EXPECT_EQ(result.components[1].id(), "attr");
}

TEST(PacmanTest, ArchIndependentPackagesAreAll) {
    const auto result = parsePacmanDatabase(
        "%FILENAME%\nx\n\n%NAME%\nx\n\n%VERSION%\n1\n\n"
        "%ARCH%\nany\n");

    ASSERT_EQ(result.components.size(), 1u);
    EXPECT_EQ(result.components[0].architecture(), "all");
}

TEST(PacmanTest, ReadsConflicts) {
    const auto result = parsePacmanDatabase(
        "%FILENAME%\nx\n\n%NAME%\nx\n\n%VERSION%\n1\n\n"
        "%CONFLICTS%\nother<2.0\n");

    ASSERT_EQ(result.components.size(), 1u);
    ASSERT_EQ(result.components[0].conflicts().size(), 1u);
    EXPECT_EQ(result.components[0].conflicts()[0].capability, "other");
}

TEST(PacmanTest, HandlesAnEmptyDatabase) {
    EXPECT_TRUE(parsePacmanDatabase("").components.empty());
}
