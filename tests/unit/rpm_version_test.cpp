#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <string>
#include <sys/wait.h>
#include <vector>

#include <nexus/system/rpm_version.hpp>

using nexus::system::compareRpmSegments;
using nexus::system::compareRpmVersions;
using nexus::system::parseRpmVersion;
using nexus::system::RpmVersion;

namespace {

bool before(const std::string& left, const std::string& right) {
    return compareRpmSegments(left, right) < 0;
}

bool same(const std::string& left, const std::string& right) {
    return compareRpmSegments(left, right) == 0;
}

}

TEST(RpmVersionTest, ParsesEpochVersionRelease) {
    const RpmVersion version = parseRpmVersion("2:1.2.3-4.fc42");

    EXPECT_EQ(version.epoch, 2u);
    EXPECT_EQ(version.version, "1.2.3");
    EXPECT_EQ(version.release, "4.fc42");
}

TEST(RpmVersionTest, MissingEpochIsZero) {
    EXPECT_EQ(parseRpmVersion("1.0").epoch, 0u);
    EXPECT_EQ(compareRpmVersions("1.0", "0:1.0"), 0);
}

TEST(RpmVersionTest, EpochOutranksEverything) {
    EXPECT_LT(compareRpmVersions("1:1.0", "2:0.1"), 0);
    EXPECT_LT(compareRpmVersions("0.1", "1:0.0"), 0);
}

TEST(RpmVersionTest, ComparesNumericallyNotTextually) {
    EXPECT_TRUE(before("1.9", "1.10"));
    EXPECT_TRUE(before("1.2", "1.11"));
}

TEST(RpmVersionTest, IgnoresLeadingZeros) {
    EXPECT_TRUE(same("1.007", "1.7"));
}

// Separators carry no meaning at all in rpm: they are skipped rather
// than compared. This is the sharpest difference from dpkg, where a
// separator is an ordinary character with a sort weight.
TEST(RpmVersionTest, SeparatorsAreSkippedEntirely) {
    // Within a segment, any run of non-alphanumerics is skipped
    // rather than compared. A hyphen cannot appear here -- it is what
    // splits version from release -- so it is not tested at this
    // level.
    EXPECT_TRUE(same("1.0.1", "1_0_1"));
    EXPECT_TRUE(same("1.0", "1....0"));
    EXPECT_TRUE(same("1.0", "1+0"));
}

// And at the whole-version level a hyphen is structural, not a
// separator: it splits version from release.
TEST(RpmVersionTest, HyphenSplitsVersionFromRelease) {
    EXPECT_GT(compareRpmVersions("0.1", "0-1"), 0);
    EXPECT_EQ(parseRpmVersion("0-1").version, "0");
    EXPECT_EQ(parseRpmVersion("0-1").release, "1");
}

TEST(RpmVersionTest, DigitsOutrankLetters) {
    EXPECT_TRUE(before("1.a", "1.1"));
    EXPECT_TRUE(before("1.alpha", "1.0"));
}

TEST(RpmVersionTest, TildeSortsBeforeEverything) {
    EXPECT_TRUE(before("1.0~rc1", "1.0"));
    EXPECT_TRUE(before("1.0~~", "1.0~"));
    EXPECT_TRUE(before("1.0~rc1", "1.0~rc2"));
}

// '^' has no dpkg equivalent: it sorts after the end of a string but
// before anything else, which is how rpm expresses a post-release
// snapshot.
TEST(RpmVersionTest, CaretSortsAfterNothingButBeforeSomething) {
    EXPECT_TRUE(before("1.0", "1.0^"));
    EXPECT_TRUE(before("1.0^", "1.0.1"));
    EXPECT_TRUE(before("1.0^", "1.0^20260830"));
}

TEST(RpmVersionTest, ReleaseBreaksATie) {
    EXPECT_LT(compareRpmVersions("1.0-1.fc42", "1.0-2.fc42"), 0);
    EXPECT_LT(compareRpmVersions("1.0-9", "1.1-1"), 0);
}

TEST(RpmVersionTest, HandlesRealFedoraVersions) {
    EXPECT_LT(
        compareRpmVersions("2.41-18.fc42", "2.43-8.fc44"), 0);
    EXPECT_LT(
        compareRpmVersions("25.1.9-1.fc42", "26.1.4-4.fc44"), 0);
    EXPECT_LT(
        compareRpmVersions("1:3.8.13-1.fc44", "2:3.8.13-1.fc44"), 0);
}

TEST(RpmVersionTest, ComparisonIsAntisymmetric) {
    static const std::array<const char*, 10> versions{
        "1.0", "1.0~rc1", "1.0^", "1.0.1", "1.0a",
        "1:0.5", "2.0", "0.9", "1.0-1", "1.0-2"
    };

    for (const char* left : versions) {
        for (const char* right : versions) {
            EXPECT_EQ(
                compareRpmVersions(left, right),
                -compareRpmVersions(right, left)
            ) << left << " vs " << right;
        }
    }
}

// Differential test against rpm itself, the same way the dpkg
// algorithm is checked against dpkg. rpm is the definition of correct
// here, so any disagreement is a bug in this code.

namespace {

bool haveRpm() {
    return std::system("rpm --version > /dev/null 2>&1") == 0;
}

// Returns -1, 0 or 1 according to rpm.
//
// rpm's lua vercmp compares complete epoch:version-release strings,
// splitting on ':' and '-' itself -- it is not the segment comparison
// its name suggests. Comparing a segment function against it produced
// disagreements that were entirely the test's fault.
int rpmCompare(const std::string& left, const std::string& right) {
    const std::string command =
        "rpm --eval '%{lua:print(rpm.vercmp(\"" + left +
        "\",\"" + right + "\"))}' 2>/dev/null";

    std::FILE* pipe = popen(command.c_str(), "r");

    if (pipe == nullptr) {
        return -2;
    }

    char buffer[64]{};

    const bool read = std::fgets(buffer, sizeof(buffer), pipe) != nullptr;

    pclose(pipe);

    if (!read) {
        return -2;
    }

    return std::atoi(buffer);
}

std::string randomVersion(std::mt19937& engine) {
    // Pieces chosen to hit what is easy to get wrong: tildes, carets,
    // separator runs, leading zeros, letters beside digits.
    static const std::array<const char*, 18> pieces{
        "0", "1", "2", "9", "10", "007",
        ".", "-", "~", "^", "_", "+",
        "a", "z", "rc", "beta", "fc42", "git"
    };

    std::uniform_int_distribution<std::size_t> pieceIndex(
        0, pieces.size() - 1
    );
    std::uniform_int_distribution<int> lengthChoice(1, 6);

    std::string version = "1";

    const int length = lengthChoice(engine);

    for (int index = 0; index < length; ++index) {
        version += pieces[pieceIndex(engine)];
    }

    return version;
}

}

TEST(RpmVersionFuzzTest, AgreesWithRpmOnKnownHardCases) {
    if (!haveRpm()) {
        GTEST_SKIP() << "rpm not available; skipping differential test";
    }

    static const std::array<const char*, 16> cases{
        "1.0", "1.0~", "1.0~~", "1.0^", "1.0^^", "1.0.1",
        "1.0a", "1.007", "1.7", "1.10", "1.9",
        "1_0-1", "1....0", "1.0~rc1", "1.0^20260830", "1"
    };

    for (const char* left : cases) {
        for (const char* right : cases) {
            const int theirs = rpmCompare(left, right);

            ASSERT_NE(theirs, -2)
                << "rpm could not compare '" << left
                << "' and '" << right << "'";

            EXPECT_EQ(compareRpmVersions(left, right), theirs)
                << "disagreement on '" << left
                << "' vs '" << right << "'";
        }
    }
}

TEST(RpmVersionFuzzTest, AgreesWithRpmOnRandomVersions) {
    if (!haveRpm()) {
        GTEST_SKIP() << "rpm not available; skipping differential test";
    }

    std::mt19937 engine(20260830);

    std::vector<std::string> versions;

    while (versions.size() < 40) {
        versions.push_back(randomVersion(engine));
    }

    std::size_t compared = 0;

    for (std::size_t i = 0; i < versions.size(); ++i) {
        for (std::size_t j = i; j < versions.size(); ++j) {
            const int theirs = rpmCompare(versions[i], versions[j]);

            ASSERT_NE(theirs, -2);

            ASSERT_EQ(compareRpmVersions(versions[i], versions[j]),
                      theirs)
                << "disagreement on '" << versions[i]
                << "' vs '" << versions[j] << "'";

            compared += 1;
        }
    }

    EXPECT_GT(compared, 500u);
}
