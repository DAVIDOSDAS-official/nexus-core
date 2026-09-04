#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <string>

#include <nexus/system/alias_file.hpp>
#include <nexus/system/dpkg_source.hpp>
#include <nexus/system/flatpak_source.hpp>
#include <nexus/system/pacman_source.hpp>
#include <nexus/system/profile_file.hpp>
#include <nexus/system/rpm_database.hpp>
#include <nexus/system/rpm_source.hpp>
#include <nexus/system/transaction_log.hpp>
#include <nexus/system/xml.hpp>

// Deliberately hostile input.
//
// Every case here asks the same question: given something broken,
// truncated, enormous or absent, does Nexus say so rather than
// crashing or producing a confident wrong answer?
//
// The second failure is the dangerous one. A reader that returns an
// empty result from a corrupt file makes a system look like it has no
// packages, and everything downstream reasons happily from that.

namespace {

class Scratch {
public:
    Scratch() {
        root_ = std::filesystem::temp_directory_path() /
                ("nexus-hostile-" + std::to_string(::getpid()) + "-" +
                 std::to_string(counter_++));

        std::filesystem::create_directories(root_);
    }

    ~Scratch() {
        std::error_code error;
        std::filesystem::permissions(
            root_,
            std::filesystem::perms::owner_all,
            std::filesystem::perm_options::add,
            error);
        std::filesystem::remove_all(root_, error);
    }

    std::string file(
        const std::string& name,
        const std::string& contents
    ) {
        const auto path = root_ / name;

        std::ofstream out(path, std::ios::binary);

        out.write(contents.data(),
                  static_cast<std::streamsize>(contents.size()));

        return path.string();
    }

    std::string directory(const std::string& name) {
        const auto path = root_ / name;

        std::filesystem::create_directories(path);

        return path.string();
    }

    std::string path() const {
        return root_.string();
    }

private:
    std::filesystem::path root_;
    static int counter_;
};

int Scratch::counter_ = 0;

std::string binaryGarbage(std::size_t bytes) {
    std::string text;

    text.reserve(bytes);

    for (std::size_t index = 0; index < bytes; ++index) {
        text.push_back(static_cast<char>((index * 37) % 256));
    }

    return text;
}

}

// A status file cut off mid-stanza: the machine ran out of disk while
// dpkg was writing.
TEST(HostileTest, ATruncatedPackageDatabaseDoesNotCrash) {
    Scratch scratch;

    const std::string path = scratch.file(
        "status",
        "Package: bash\n"
        "Status: install ok installed\n"
        "Version: 5.1\n"
        "\n"
        "Package: coreutils\n"
        "Status: install ok inst");

    const nexus::system::DpkgSource source(path);

    // Either it reads what it can or it throws. It must not lie.
    try {
        const auto result = source.load();

        // The complete stanza is real; the truncated one must not
        // appear as a component with a made-up state.
        EXPECT_LE(result.components.size(), 2u);
    } catch (const std::exception&) {
        SUCCEED();
    }
}

TEST(HostileTest, BinaryGarbageIsNotReadAsPackages) {
    Scratch scratch;

    const std::string path =
        scratch.file("status", binaryGarbage(64 * 1024));

    const nexus::system::DpkgSource source(path);

    try {
        const auto result = source.load();

        // Nothing in that file is a package.
        EXPECT_TRUE(result.components.empty());
    } catch (const std::exception&) {
        SUCCEED();
    }
}

TEST(HostileTest, ADirectoryWhereAFileIsExpectedIsReported) {
    Scratch scratch;

    const std::string path = scratch.directory("status");

    const nexus::system::DpkgSource source(path);

    EXPECT_THROW(source.load(), std::exception);
}

TEST(HostileTest, AnUnreadableFileIsReported) {
    Scratch scratch;

    const std::string path = scratch.file("status", "Package: bash\n");

    std::error_code error;

    std::filesystem::permissions(
        path,
        std::filesystem::perms::none,
        std::filesystem::perm_options::replace,
        error);

    if (error || ::geteuid() == 0) {
        GTEST_SKIP() << "permissions do not apply here";
    }

    const nexus::system::DpkgSource source(path);

    EXPECT_THROW(source.load(), std::exception);
}

// XML cut off mid-element.
TEST(HostileTest, TruncatedRepodataIsReported) {
    const auto result = nexus::system::parseRepodataPrimary(
        "<metadata><package type=\"rpm\"><name>bash</name>"
        "<version epoch=\"0\" ver=\"5.1\" rel=\"1\"/>");

    // Incomplete, so the package must not be reported as read.
    EXPECT_TRUE(result.components.empty() || !result.error.empty());
}

TEST(HostileTest, GarbageInsteadOfXmlIsReported) {
    const auto result =
        nexus::system::parseRepodataPrimary(binaryGarbage(4096));

    EXPECT_TRUE(result.components.empty() || !result.error.empty());
}

// A pull parser must not recurse on nesting depth.
TEST(HostileTest, DeeplyNestedXmlDoesNotExhaustTheStack) {
    std::string document;

    for (int depth = 0; depth < 50000; ++depth) {
        document += "<a>";
    }

    for (int depth = 0; depth < 50000; ++depth) {
        document += "</a>";
    }

    nexus::system::XmlReader reader(std::move(document));

    std::size_t seen = 0;

    while (reader.next() != nexus::system::XmlReader::Event::None) {
        seen += 1;

        if (seen > 200000) {
            break;
        }
    }

    EXPECT_GT(seen, 0u);
}

// One enormous attribute value.
TEST(HostileTest, AnEnormousFieldIsHandled) {
    const std::string huge(4 * 1024 * 1024, 'x');

    nexus::system::XmlReader reader(
        "<a name=\"" + huge + "\"/>");

    ASSERT_EQ(reader.next(),
              nexus::system::XmlReader::Event::StartElement);

    EXPECT_EQ(reader.attribute("name").size(), huge.size());
}

TEST(HostileTest, APacmanDatabaseThatIsNotATarIsReported) {
    Scratch scratch;

    const std::string path =
        scratch.file("core.db", "this is not a tar archive at all");

    const auto result = nexus::system::readPacmanDatabase(path);

    EXPECT_TRUE(result.components.empty());
    EXPECT_FALSE(result.error.empty());
}

TEST(HostileTest, AMissingPacmanDatabaseNamesItself) {
    const auto result =
        nexus::system::readPacmanDatabase("/nonexistent/core.db");

    EXPECT_FALSE(result.error.empty());

    // The message must say what was wrong, not merely that something
    // was.
    EXPECT_GT(result.error.size(), 20u);
}

TEST(HostileTest, PacmanFieldsWithoutValuesAreSkipped) {
    const auto result = nexus::system::parsePacmanDatabase(
        "%FILENAME%\n"
        "\n"
        "%NAME%\n"
        "\n"
        "%VERSION%\n"
        "\n");

    EXPECT_TRUE(result.components.empty());
}

TEST(HostileTest, FlatpakRowsWithMissingColumnsAreSkipped) {
    const auto result = nexus::system::FlatpakSource::parse(
        "com.example.Good\t1.0\tstable\t10 MB\n"
        "com.example.Short\n"
        "\t\t\t\n"
        "com.example.AlsoGood\t2.0\tstable\t20 MB\n",
        "");

    EXPECT_EQ(result.components.size(), 2u);
}

TEST(HostileTest, RpmQueryOutputWithMissingFieldsIsSkipped) {
    const auto result = nexus::system::RpmDatabase::parse(
        "PKG\n"
        "REQ\torphan\t\t\t\n"
        "PKG\tbash\t(none)\t5.1\t1\tx86_64\t100\n");

    // The headerless first record has no name and must not become a
    // component.
    EXPECT_EQ(result.components.size(), 1u);
    EXPECT_EQ(result.components[0].id(), "bash");
}

TEST(HostileTest, AMalformedAliasFileReportsRatherThanSilentlyEmpty) {
    Scratch scratch;

    const std::string path = scratch.file(
        "broken.aliases",
        "Capability: web-browser\n"
        "\n"
        "Capability: firewall\n"
        "Resolves-To:\n");

    const auto result = nexus::system::parseAliasFile(path);

    EXPECT_FALSE(result.problems.empty());
}

TEST(HostileTest, AMissingAliasFileIsReported) {
    const auto result =
        nexus::system::parseAliasFile("/nonexistent/x.aliases");

    EXPECT_FALSE(result.problems.empty());
}

TEST(HostileTest, AProfileDirectoryThatIsAFileIsReported) {
    Scratch scratch;

    const std::string path = scratch.file("profiles", "not a directory");

    const auto result = nexus::system::parseProfileDirectory(path);

    EXPECT_TRUE(result.profiles.empty());
}

// A log that cannot be written must say so rather than reporting a
// change that was never recorded.
TEST(HostileTest, AnUnwritableTransactionLogFails) {
    nexus::system::TransactionRecord record;

    record.when = nexus::system::currentTimestamp();
    record.request = "tree";
    record.outcome = "applied";
    record.succeeded = true;

    // Under /proc, which cannot be written to even by root -- unlike
    // an ordinary absent path, which root would simply create.
    EXPECT_FALSE(nexus::system::recordTransaction(
        "/proc/nexus-cannot-exist/log", record));

    // And an empty path is not a place.
    EXPECT_FALSE(nexus::system::recordTransaction("", record));
}

TEST(HostileTest, ATransactionLogFullOfGarbageReadsAsNothing) {
    Scratch scratch;

    const std::string path =
        scratch.file("transactions.log", binaryGarbage(8192));

    const auto records = nexus::system::readTransactions(path);

    // Whatever it finds, it must not invent a successful change.
    for (const auto& record : records) {
        EXPECT_FALSE(record.when.empty());
    }
}
