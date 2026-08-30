#include <gtest/gtest.h>

#include <algorithm>
#include <string>

#include <nexus/system/rpm_database.hpp>

using nexus::Capability;
using nexus::Component;
using nexus::system::RpmDatabase;

namespace {

// Captured from `rpm -qa` on Fedora 42 rather than invented. The
// details that matter here -- "(none)" for an absent epoch, empty
// versions on unconstrained requirements, rpmlib entries outnumbering
// real ones -- are exactly what a made-up fixture would smooth over.
const char* kRealOutput =
    "PKG\tfilesystem\t(none)\t3.18\t47.fc42\tx86_64\n"
    "PRV\tfilesystem\t=\t3.18-47.fc42\n"
    "PRV\tfilesystem(merged-sbin)\t=\t1\n"
    "PRV\tfilesystem(x86-64)\t=\t3.18-47.fc42\n"
    "REQ\trpmlib(BuiltinLuaScripts)\t<=\t4.2.2-1\trpmlib\n"
    "REQ\trpmlib(CompressedFileNames)\t<=\t3.0.4-1\trpmlib\n"
    "REQ\trpmlib(PayloadIsZstd)\t<=\t5.4.18-1\trpmlib\n"
    "REQ\tsetup\t\t\tpre\n"
    "PKG\tlibgcc\t(none)\t15.2.1\t7.fc42\tx86_64\n"
    "PRV\tlibgcc\t=\t15.2.1-7.fc42\n"
    "PRV\tlibgcc_s.so.1()(64bit)\t\t\n"
    "REQ\tglibc\t>=\t2.34\t\n"
    "CON\tlibgcc-old\t<\t9.0\n"
    "PKG\ttzdata\t(none)\t2025c\t1.fc42\tnoarch\n"
    "PRV\ttzdata\t=\t2025c-1.fc42\n";

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

bool requires_(const Component& component, const std::string& name) {
    for (const auto& requirement : component.requirements()) {
        for (const auto& option : requirement.alternatives) {
            if (option.capability == name) {
                return true;
            }
        }
    }

    return false;
}

}

TEST(RpmDatabaseTest, ReadsPackages) {
    const auto result = RpmDatabase::parse(kRealOutput);

    ASSERT_TRUE(result.error.empty());
    ASSERT_EQ(result.components.size(), 3u);
    EXPECT_EQ(result.packagesRead, 3u);
    EXPECT_EQ(result.components[0].id(), "filesystem");
    EXPECT_EQ(result.components[2].architecture(), "all");
}

// rpm prints "(none)" for an absent epoch rather than nothing.
TEST(RpmDatabaseTest, TreatsNoneEpochAsAbsent) {
    const auto result = RpmDatabase::parse(kRealOutput);

    ASSERT_EQ(result.components.size(), 3u);
    EXPECT_EQ(result.components[0].version(), "3.18-47.fc42");
    EXPECT_EQ(result.components[1].version(), "15.2.1-7.fc42");
}

TEST(RpmDatabaseTest, ReadsProvidesWithVersions) {
    const auto result = RpmDatabase::parse(kRealOutput);

    ASSERT_FALSE(result.components.empty());

    const Component& filesystem = result.components[0];

    EXPECT_TRUE(provides(filesystem, "filesystem(merged-sbin)"));
    EXPECT_TRUE(provides(filesystem, "filesystem(x86-64)"));

    for (const auto& capability :
         filesystem.providedCapabilities()) {
        if (capability.name() == "filesystem(x86-64)") {
            EXPECT_EQ(capability.version(), "3.18-47.fc42");
        }
    }
}

// The single most important filter here. Almost everything a package
// declares is a requirement on rpm's own features, which no package
// provides. Left in, every package on the system looks unsatisfiable.
TEST(RpmDatabaseTest, DropsRpmlibRequirementsAndCountsThem) {
    const auto result = RpmDatabase::parse(kRealOutput);

    ASSERT_FALSE(result.components.empty());

    EXPECT_EQ(result.rpmlibRequirements, 3u);
    EXPECT_FALSE(
        requires_(result.components[0], "rpmlib(PayloadIsZstd)"));
    EXPECT_TRUE(requires_(result.components[0], "setup"));
    EXPECT_EQ(result.components[0].requirements().size(), 1u);
}

TEST(RpmDatabaseTest, ReadsPreRequirements) {
    const auto result = RpmDatabase::parse(kRealOutput);

    ASSERT_FALSE(result.components.empty());
    ASSERT_EQ(result.components[0].requirements().size(), 1u);
    EXPECT_TRUE(result.components[0].requirements()[0].pre);
}

// An unconstrained requirement has no flags and no version.
TEST(RpmDatabaseTest, HandlesAnUnconstrainedRequirement) {
    const auto result = RpmDatabase::parse(kRealOutput);

    ASSERT_FALSE(result.components.empty());

    const auto& option =
        result.components[0].requirements()[0].alternatives[0];

    EXPECT_EQ(option.capability, "setup");
    EXPECT_TRUE(option.isUnversioned());
}

TEST(RpmDatabaseTest, ReadsVersionConstraints) {
    const auto result = RpmDatabase::parse(kRealOutput);

    ASSERT_GE(result.components.size(), 2u);

    const auto& option =
        result.components[1].requirements()[0].alternatives[0];

    EXPECT_EQ(option.capability, "glibc");
    ASSERT_TRUE(option.version.has_value());
    EXPECT_EQ(
        option.version->relation, nexus::VersionRelation::LaterOrEqual);
    EXPECT_EQ(option.version->version, "2.34");
}

// depflags renders as symbols on some rpm versions and as two-letter
// names on others. Betting on one would break on the other.
TEST(RpmDatabaseTest, AcceptsBothFlagSpellings) {
    const auto symbols = RpmDatabase::parse(
        "PKG\ta\t(none)\t1.0\t1\tx86_64\nREQ\tb\t>=\t2.0\t\n");
    const auto names = RpmDatabase::parse(
        "PKG\ta\t(none)\t1.0\t1\tx86_64\nREQ\tb\tGE\t2.0\t\n");

    ASSERT_EQ(symbols.components.size(), 1u);
    ASSERT_EQ(names.components.size(), 1u);

    const auto& fromSymbols =
        symbols.components[0].requirements()[0].alternatives[0];
    const auto& fromNames =
        names.components[0].requirements()[0].alternatives[0];

    ASSERT_TRUE(fromSymbols.version.has_value());
    ASSERT_TRUE(fromNames.version.has_value());
    EXPECT_EQ(fromSymbols.version->relation, fromNames.version->relation);
}

TEST(RpmDatabaseTest, ReadsConflicts) {
    const auto result = RpmDatabase::parse(kRealOutput);

    ASSERT_GE(result.components.size(), 2u);
    ASSERT_EQ(result.components[1].conflicts().size(), 1u);
    EXPECT_EQ(
        result.components[1].conflicts()[0].capability, "libgcc-old");
}

TEST(RpmDatabaseTest, SonameProvidesAreOrdinaryCapabilities) {
    const auto result = RpmDatabase::parse(kRealOutput);

    ASSERT_GE(result.components.size(), 2u);
    EXPECT_TRUE(
        provides(result.components[1], "libgcc_s.so.1()(64bit)"));
}

TEST(RpmDatabaseTest, HandlesEmptyOutput) {
    const auto result = RpmDatabase::parse("");

    EXPECT_TRUE(result.components.empty());
    EXPECT_EQ(result.packagesRead, 0u);
}

// Lines before the first package have nothing to attach to.
TEST(RpmDatabaseTest, IgnoresOrphanedDependencyLines) {
    const auto result = RpmDatabase::parse(
        "PRV\tstray\t\t\nPKG\ta\t(none)\t1.0\t1\tnoarch\n");

    ASSERT_EQ(result.components.size(), 1u);
    EXPECT_FALSE(provides(result.components[0], "stray"));
}
