#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include <nexus/system/compression.hpp>
#include <nexus/system/rpm_repo.hpp>

using nexus::system::decompressCommand;
using nexus::system::decompressTool;
using nexus::system::findPrimaryHref;
using nexus::system::readPossiblyCompressed;
using nexus::system::RpmRepository;

namespace {

// A throwaway dnf-style cache directory.
class FakeCache {
public:
    FakeCache() {
        root_ = std::filesystem::temp_directory_path() /
                ("nexus-rpm-" + std::to_string(::getpid()) + "-" +
                 std::to_string(counter_++));

        std::filesystem::create_directories(root_);
    }

    ~FakeCache() {
        std::error_code error;
        std::filesystem::remove_all(root_, error);
    }

    void repository(
        const std::string& name,
        const std::string& repomd,
        const std::string& primaryName,
        const std::string& primary
    ) {
        const auto data = root_ / name / "repodata";

        std::filesystem::create_directories(data);

        write(data / "repomd.xml", repomd);

        if (!primaryName.empty()) {
            write(data / primaryName, primary);
        }
    }

    std::string path() const {
        return root_.string();
    }

private:
    static void write(
        const std::filesystem::path& file,
        const std::string& text
    ) {
        std::ofstream out(file);
        out << text;
    }

    std::filesystem::path root_;
    static int counter_;
};

int FakeCache::counter_ = 0;

std::string repomdFor(const std::string& primaryName) {
    return
        "<repomd>"
        "<revision>1</revision>"
        "<data type=\"filelists\">"
        "<location href=\"repodata/x-filelists.xml.zst\"/>"
        "</data>"
        "<data type=\"primary\">"
        "<location href=\"repodata/" + primaryName + "\"/>"
        "</data>"
        "<data type=\"primary_db\">"
        "<location href=\"repodata/y-primary.sqlite.zst\"/>"
        "</data>"
        "</repomd>";
}

const char* kNewerPrimary =
    "<metadata>"
    "<package type=\"rpm\"><name>alpha</name><arch>x86_64</arch>"
    "<version epoch=\"0\" ver=\"2.0\" rel=\"1.fc42\"/>"
    "<format></format></package>"
    "</metadata>";

const char* kMultiArchPrimary =
    "<metadata>"
    "<package type=\"rpm\"><name>alpha</name><arch>x86_64</arch>"
    "<version epoch=\"0\" ver=\"1.0\" rel=\"1.fc42\"/>"
    "<format></format></package>"
    "<package type=\"rpm\"><name>alpha</name><arch>i686</arch>"
    "<version epoch=\"0\" ver=\"1.0\" rel=\"1.fc42\"/>"
    "<format></format></package>"
    "</metadata>";

const char* kPrimary =
    "<metadata>"
    "<package type=\"rpm\"><name>alpha</name><arch>x86_64</arch>"
    "<version epoch=\"0\" ver=\"1.0\" rel=\"1.fc42\"/>"
    "<format></format></package>"
    "</metadata>";

}

TEST(CompressionTest, PicksTheRightToolAndFlags) {
    EXPECT_EQ(decompressTool("a.zst"), "zstd");
    EXPECT_EQ(decompressTool("a.zck"), "unzck");
    EXPECT_EQ(decompressTool("a.lz4"), "lz4");
    EXPECT_EQ(decompressTool("a.gz"), "gzip");
    EXPECT_EQ(decompressTool("a.xml"), "");

    EXPECT_EQ(decompressCommand("a.zst"), "zstd -dc");
    EXPECT_EQ(decompressCommand("a.gz"), "gzip -dc");
    EXPECT_EQ(decompressCommand("a.xml"), "");
}

// zchunk takes -c where every other decompressor takes -dc. Fedora's
// large repositories are all .zck, so getting this wrong produces a
// reader that works on a six-package repository and fails on a
// seventy-thousand-package one.
TEST(CompressionTest, ZchunkUsesADifferentFlag) {
    EXPECT_EQ(decompressCommand("a.zck"), "unzck -c");
}

TEST(CompressionTest, ReadsAPlainFile) {
    const auto path = std::filesystem::temp_directory_path() /
                      ("nexus-plain-" + std::to_string(::getpid()));

    {
        std::ofstream out(path);
        out << "hello";
    }

    std::string contents;
    std::string reason;

    EXPECT_TRUE(readPossiblyCompressed(path.string(), contents, reason));
    EXPECT_EQ(contents, "hello");

    std::error_code error;
    std::filesystem::remove(path, error);
}

TEST(CompressionTest, ReportsAMissingFile) {
    std::string contents;
    std::string reason;

    EXPECT_FALSE(
        readPossiblyCompressed("/nonexistent/file", contents, reason));
    EXPECT_FALSE(reason.empty());
}

// repomd lists primary_db as well, a SQLite dump of the same content.
// Matching on a prefix would pick the wrong one.
TEST(RpmRepoTest, FindsPrimaryAndNotPrimaryDb) {
    EXPECT_EQ(
        findPrimaryHref(repomdFor("abc-primary.xml.zst")),
        "repodata/abc-primary.xml.zst"
    );
}

TEST(RpmRepoTest, ReportsNoPrimaryEntry) {
    EXPECT_TRUE(findPrimaryHref(
        "<repomd><data type=\"other\">"
        "<location href=\"repodata/o.xml\"/></data></repomd>"
    ).empty());
}

TEST(RpmRepoTest, ReadsARepository) {
    FakeCache cache;

    cache.repository(
        "fedora-abc", repomdFor("p-primary.xml"), "p-primary.xml",
        kPrimary
    );

    const auto result = RpmRepository(cache.path()).load();

    ASSERT_EQ(result.repositoriesRead.size(), 1u);
    ASSERT_EQ(result.components.size(), 1u);
    EXPECT_EQ(result.components[0].id(), "alpha");
    EXPECT_EQ(result.components[0].architecture(), "amd64");
    EXPECT_EQ(result.packagesRead, 1u);
    EXPECT_TRUE(result.skipped.empty());
}

TEST(RpmRepoTest, ReadsSeveralRepositories) {
    FakeCache cache;

    cache.repository("fedora", repomdFor("a-primary.xml"),
                     "a-primary.xml", kPrimary);
    cache.repository("updates", repomdFor("b-primary.xml"),
                     "b-primary.xml", kNewerPrimary);

    const auto result = RpmRepository(cache.path()).load();

    EXPECT_EQ(result.repositoriesRead.size(), 2u);

    // The same package in two repositories is one package, not two.
    ASSERT_EQ(result.components.size(), 1u);
    EXPECT_EQ(result.versionsSuperseded, 1u);
    EXPECT_EQ(result.components[0].version(), "2.0-1.fc42");
}

// Two architectures of one name are genuinely two packages, and must
// keep distinct ids.
TEST(RpmRepoTest, KeepsBothArchitecturesOfAName) {
    FakeCache cache;

    cache.repository("fedora", repomdFor("a-primary.xml"),
                     "a-primary.xml", kMultiArchPrimary);

    const auto result = RpmRepository(cache.path()).load();

    ASSERT_EQ(result.components.size(), 2u);
    EXPECT_NE(result.components[0].id(), result.components[1].id());
    EXPECT_EQ(result.versionsSuperseded, 0u);
}

TEST(RpmRepoTest, SkipsARepositoryWithoutPrimary) {
    FakeCache cache;

    cache.repository(
        "broken",
        "<repomd><data type=\"other\">"
        "<location href=\"repodata/o.xml\"/></data></repomd>",
        "", ""
    );

    const auto result = RpmRepository(cache.path()).load();

    EXPECT_TRUE(result.components.empty());
    ASSERT_EQ(result.skipped.size(), 1u);
    EXPECT_NE(
        result.skipped[0].reason.find("no primary"),
        std::string::npos
    );
}

TEST(RpmRepoTest, SkipsAMissingPrimaryFile) {
    FakeCache cache;

    cache.repository("gone", repomdFor("absent-primary.xml"), "", "");

    const auto result = RpmRepository(cache.path()).load();

    EXPECT_TRUE(result.components.empty());
    ASSERT_EQ(result.skipped.size(), 1u);
}

TEST(RpmRepoTest, ReportsAMissingCacheDirectory) {
    const auto result = RpmRepository("/nonexistent/cache").load();

    EXPECT_TRUE(result.components.empty());
    ASSERT_EQ(result.skipped.size(), 1u);
}
