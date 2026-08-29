#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>

#include <nexus/solver.hpp>
#include <nexus/system/apt_source.hpp>
#include <nexus/system/version.hpp>

using nexus::Component;
using nexus::ComponentType;
using nexus::ConflictDetector;
using nexus::Constraint;
using nexus::Requirement;
using nexus::Solver;
using nexus::SolverRequest;
using nexus::SolverStatus;
using nexus::system::AptSource;
using nexus::system::decompressorFor;
using nexus::system::mergeAvailable;

namespace {

// A temporary lists directory holding one plain Packages file.
class TempLists {
public:
    explicit TempLists(const std::string& contents) {
        path_ = std::filesystem::temp_directory_path() /
                ("nexus-lists-" + std::to_string(::getpid()) + "-" +
                 std::to_string(counter_++));

        std::filesystem::create_directories(path_);

        std::ofstream out(path_ / "testrepo_dists_main_Packages");
        out << contents;
    }

    ~TempLists() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    std::string dir() const {
        return path_.string();
    }

private:
    std::filesystem::path path_;
    static int counter_;
};

int TempLists::counter_ = 0;

Component make(const std::string& id, const std::string& version) {
    return Component(id, id, version, ComponentType::Application);
}

}

TEST(AptSourceTest, RecognisesCompressionByExtension) {
    EXPECT_EQ(decompressorFor("a_Packages"), "");
    EXPECT_EQ(decompressorFor("a_Packages.lz4"), "lz4");
    EXPECT_EQ(decompressorFor("a_Packages.gz"), "gzip");
    EXPECT_EQ(decompressorFor("a_Packages.xz"), "xz");
    EXPECT_EQ(decompressorFor("a_Packages.bz2"), "bzip2");
}

TEST(AptSourceTest, ReadsAPlainIndex) {
    TempLists lists(
        "Package: gamemode\n"
        "Architecture: amd64\n"
        "Version: 1.8.1\n"
        "Section: utils\n"
        "Depends: libc6 (>= 2.34)\n"
        "Description: optimise system for games\n"
        " long continuation line that must not become a field\n"
        "\n"
        "Package: libc6\n"
        "Architecture: amd64\n"
        "Multi-Arch: same\n"
        "Version: 2.39\n"
    );

    const AptSource source(lists.dir());
    const auto result = source.load();

    EXPECT_EQ(result.filesRead.size(), 1u);
    EXPECT_TRUE(result.filesSkipped.empty());
    ASSERT_EQ(result.components.size(), 2u);
    EXPECT_EQ(result.components[0].id(), "gamemode");
    EXPECT_EQ(result.components[0].architecture(), "amd64");
    EXPECT_EQ(result.components[0].requirements().size(), 1u);
    EXPECT_EQ(result.components[1].multiArch(), nexus::MultiArch::Same);
}

// The same package appears in several suites. The highest version
// wins, and the rest are counted rather than dropped silently.
TEST(AptSourceTest, KeepsTheHighestVersionAndCountsTheRest) {
    TempLists lists(
        "Package: firefox\n"
        "Architecture: amd64\n"
        "Version: 120.0\n"
        "\n"
        "Package: firefox\n"
        "Architecture: amd64\n"
        "Version: 131.0\n"
        "\n"
        "Package: firefox\n"
        "Architecture: amd64\n"
        "Version: 99.0\n"
    );

    const AptSource source(lists.dir());
    const auto result = source.load();

    ASSERT_EQ(result.components.size(), 1u);
    EXPECT_EQ(result.components[0].version(), "131.0");
    EXPECT_EQ(result.stanzasRead, 3u);
    EXPECT_EQ(result.versionsSuperseded, 2u);
}

TEST(AptSourceTest, SameNameDifferentArchitectureAreSeparate) {
    TempLists lists(
        "Package: libc6\n"
        "Architecture: amd64\n"
        "Version: 2.39\n"
        "\n"
        "Package: libc6\n"
        "Architecture: i386\n"
        "Version: 2.39\n"
    );

    const AptSource source(lists.dir());

    EXPECT_EQ(source.load().components.size(), 2u);
}

TEST(AptSourceTest, ReportsAMissingDirectory) {
    const AptSource source("/nonexistent/apt/lists");
    const auto result = source.load();

    EXPECT_TRUE(result.components.empty());
    ASSERT_EQ(result.filesSkipped.size(), 1u);
    EXPECT_FALSE(result.filesSkipped[0].reason.empty());
}

TEST(MergeAvailableTest, InstalledWinsOverAvailable) {
    std::vector<Component> installed{make("firefox", "131.0")};
    std::vector<Component> available{
        make("firefox", "999.0"),
        make("gamemode", "1.8.1")
    };

    const auto merged = mergeAvailable(installed, available);

    ASSERT_EQ(merged.size(), 2u);

    for (const Component& component : merged) {
        if (component.id() == "firefox") {
            // The installed version is the truth about this machine.
            EXPECT_EQ(component.version(), "131.0");
        }
    }
}

TEST(MergeAvailableTest, AvailableFillsTheGaps) {
    const auto merged = mergeAvailable(
        {make("firefox", "131.0")},
        {make("gamemode", "1.8.1")}
    );

    ASSERT_EQ(merged.size(), 2u);
    EXPECT_EQ(merged[1].id(), "gamemode");
}

// The point of the whole feature: a requirement that cannot be met by
// what is installed can be met once the archive is in view.
TEST(MergeAvailableTest, UnsatisfiableBecomesSatisfiableWithArchive) {
    const auto comparator =
        [](const std::string& left, const std::string& right) {
            return nexus::system::compareVersions(left, right);
        };

    Component gamemode = make("gamemode", "1.8.1");
    gamemode.addRequirement(Requirement(Constraint("libc6")));

    const std::vector<Component> installed{make("libc6", "2.39")};
    const std::vector<Component> available{gamemode};

    SolverRequest request;
    request.requirements.push_back(Requirement(Constraint("gamemode")));

    const Solver withoutArchive(installed, ConflictDetector(comparator));

    EXPECT_EQ(
        withoutArchive.solve(request).status,
        SolverStatus::Unsatisfiable
    );

    const Solver withArchive(
        mergeAvailable(installed, available),
        ConflictDetector(comparator)
    );

    const auto result = withArchive.solve(request);

    EXPECT_EQ(result.status, SolverStatus::Success);
    EXPECT_EQ(result.selected.size(), 2u);
}
