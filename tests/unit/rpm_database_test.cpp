#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <unistd.h>

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
    "FIL\t/usr/lib64/libgcc_s.so.1\n"
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

// bash requires /usr/bin/sh. Without file provides, a system looks
// unsatisfiable for reasons that have nothing to do with packages.
TEST(RpmDatabaseTest, FilesAreProvidedCapabilities) {
    const auto result = RpmDatabase::parse(kRealOutput);

    ASSERT_GE(result.components.size(), 2u);
    EXPECT_EQ(result.fileProvides, 1u);
    EXPECT_TRUE(provides(
        result.components[1], "/usr/lib64/libgcc_s.so.1"));
}

// A conditional requirement is not a capability name. Passed
// through, "(glibc-gconv-extra if redhat-rpm-config)" is something
// nothing can ever provide, and every solve fails on it.
TEST(RpmDatabaseTest, RecordsBooleanRequirementsInsteadOfPassingThem) {
    const auto result = RpmDatabase::parse(
        "PKG\tbash\t(none)\t5.2\t1\tx86_64\n"
        "REQ\t(glibc-gconv-extra if redhat-rpm-config)\t\t\t\n"
        "REQ\tglibc\t>=\t2.34\t\n");

    ASSERT_EQ(result.components.size(), 1u);
    EXPECT_EQ(result.booleanRequirements, 1u);
    EXPECT_EQ(result.components[0].requirements().size(), 1u);
    EXPECT_TRUE(requires_(result.components[0], "glibc"));
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

// Reading the real database, through a stand-in rpm.
//
// On 23 September doctor on a freshly installed laptop read 1054
// packages where rpm lists 1103, and reported 305 requirements that
// "nothing installed satisfies" -- libsystemd among them, on a
// machine that could not have booted without it. It did not happen
// again, so the cause was never seen. What made it dangerous was that
// it could not be seen: rpm's exit status was never checked, its
// errors went to /dev/null, and a short list was taken as the whole
// machine. These tests are about that, not about the cause.

namespace {

class FakeRpm {
public:
    // A directory holding an executable `rpm`, put first on PATH for
    // the life of the object.
    explicit FakeRpm(const std::string& script) {
        dir_ = std::filesystem::temp_directory_path() /
               ("nexus-fake-rpm-" + std::to_string(::getpid()) + "-" +
                std::to_string(counter_++));
        std::filesystem::create_directories(dir_);

        const auto path = dir_ / "rpm";
        {
            std::ofstream out(path);
            out << "#!/bin/sh\n" << script;
        }
        std::filesystem::permissions(
            path, std::filesystem::perms::owner_all);

        const char* old = std::getenv("PATH");
        oldPath_ = old == nullptr ? "" : old;
        ::setenv("PATH", (dir_.string() + ":" + oldPath_).c_str(), 1);
    }

    ~FakeRpm() {
        ::setenv("PATH", oldPath_.c_str(), 1);
        std::error_code error;
        std::filesystem::remove_all(dir_, error);
    }

private:
    std::filesystem::path dir_;
    std::string oldPath_;
    static inline int counter_ = 0;
};

// The full query asks for PKG lines; the count asks for one '.' per
// package. The fake answers each the way it is told to.
const char* kTwoPackages =
    "printf 'PKG\\ta\\t(none)\\t1\\t1\\tx86_64\\t10\\n"
    "PKG\\tb\\t(none)\\t1\\t1\\tx86_64\\t10\\n'\n";

}

TEST(RpmDatabaseLoadTest, AConsistentReadIsNotFlagged) {
    FakeRpm rpm(
        "case \"$*\" in *PKG*) " + std::string(kTwoPackages) +
        " ;; *) printf '.\\n.\\n' ;; esac\n");

    const auto result = RpmDatabase().load();

    EXPECT_TRUE(result.error.empty()) << result.error;
    EXPECT_EQ(result.packagesRead, 2u);
    EXPECT_TRUE(result.incomplete.empty()) << result.incomplete;
}

// rpm lists three; the full query yields two. Whatever the reason,
// the list is not the machine, and must not be presented as it.
TEST(RpmDatabaseLoadTest, FewerPackagesThanRpmListsIsIncomplete) {
    FakeRpm rpm(
        "case \"$*\" in *PKG*) " + std::string(kTwoPackages) +
        " ;; *) printf '.\\n.\\n.\\n' ;; esac\n");

    const auto result = RpmDatabase().load();

    EXPECT_EQ(result.packagesRead, 2u);
    EXPECT_NE(result.incomplete.find("2"), std::string::npos)
        << result.incomplete;
    EXPECT_NE(result.incomplete.find("3"), std::string::npos)
        << result.incomplete;
}

// rpm printed some records, then failed. Its exit status and its own
// words both have to reach the person reading the report.
TEST(RpmDatabaseLoadTest, AFailingRpmIsReportedWithItsOwnMessage) {
    FakeRpm rpm(
        "case \"$*\" in *PKG*) " + std::string(kTwoPackages) +
        " echo 'error: rpmdb: database is locked' >&2; exit 1 ;;"
        " *) printf '.\\n.\\n' ;; esac\n");

    const auto result = RpmDatabase().load();

    EXPECT_FALSE(result.incomplete.empty());
    EXPECT_NE(result.incomplete.find("database is locked"),
              std::string::npos) << result.incomplete;
}
