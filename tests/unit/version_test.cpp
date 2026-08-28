#include <gtest/gtest.h>

#include <array>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <sys/wait.h>
#include <string>
#include <vector>

#include <nexus/system/version.hpp>

using nexus::system::compareVersions;
using nexus::system::parseVersion;
using nexus::system::satisfies;
using nexus::system::validateVersion;
using nexus::system::Version;
using nexus::system::VersionConstraint;
using nexus::system::VersionRelation;

namespace {

bool lessThan(const std::string& left, const std::string& right) {
    return compareVersions(left, right) < 0;
}

bool equal(const std::string& left, const std::string& right) {
    return compareVersions(left, right) == 0;
}

}

TEST(VersionTest, ParsesPlainUpstream) {
    const Version version = parseVersion("1.2.3");

    EXPECT_EQ(version.epoch, 0u);
    EXPECT_EQ(version.upstream, "1.2.3");
    EXPECT_EQ(version.revision, "");
}

TEST(VersionTest, ParsesEpoch) {
    const Version version = parseVersion("2:1.2.3");

    EXPECT_EQ(version.epoch, 2u);
    EXPECT_EQ(version.upstream, "1.2.3");
}

TEST(VersionTest, ParsesRevision) {
    const Version version = parseVersion("1.2.3-4ubuntu1");

    EXPECT_EQ(version.upstream, "1.2.3");
    EXPECT_EQ(version.revision, "4ubuntu1");
}

TEST(VersionTest, RevisionSplitsOnTheLastHyphen) {
    const Version version = parseVersion("1.0-beta-3");

    EXPECT_EQ(version.upstream, "1.0-beta");
    EXPECT_EQ(version.revision, "3");
}

TEST(VersionTest, ColonWithoutDigitsIsNotAnEpoch) {
    const Version version = parseVersion("1.0:weird");

    EXPECT_EQ(version.epoch, 0u);
    EXPECT_EQ(version.upstream, "1.0:weird");
}

TEST(VersionTest, EpochIsComparedFirst) {
    EXPECT_TRUE(lessThan("1:1.0", "2:0.1"));
    EXPECT_TRUE(lessThan("0.1", "1:0.0"));
}

TEST(VersionTest, MissingEpochEqualsZeroEpoch) {
    EXPECT_TRUE(equal("1.0", "0:1.0"));
}

TEST(VersionTest, MissingRevisionEqualsEmptyRevision) {
    EXPECT_TRUE(equal("1.0", "1.0-"));
}

TEST(VersionTest, ComparesNumericallyNotTextually) {
    EXPECT_TRUE(lessThan("1.9", "1.10"));
    EXPECT_TRUE(lessThan("1.2", "1.11"));
}

TEST(VersionTest, IgnoresLeadingZeros) {
    EXPECT_TRUE(equal("1.007", "1.7"));
    EXPECT_TRUE(equal("0001.0", "1.0"));
}

// The rule that catches everyone: '~' sorts before everything,
// including the end of the string.
TEST(VersionTest, TildeSortsBeforeEverything) {
    EXPECT_TRUE(lessThan("1.0~rc1", "1.0"));
    EXPECT_TRUE(lessThan("1.0~~", "1.0~"));
    EXPECT_TRUE(lessThan("1.0~", "1.0"));
    EXPECT_TRUE(lessThan("1.0~beta", "1.0~rc"));
    EXPECT_TRUE(lessThan("1.0~rc1", "1.0~rc2"));
}

TEST(VersionTest, LettersSortBeforeNonLetters) {
    EXPECT_TRUE(lessThan("1.0a", "1.0+"));
    EXPECT_TRUE(lessThan("1.0a", "1.0."));
}

TEST(VersionTest, UpstreamOutranksRevision) {
    EXPECT_TRUE(lessThan("1.0-9", "1.1-1"));
    EXPECT_TRUE(lessThan("1.0-1", "1.0-2"));
}

TEST(VersionTest, HandlesRealWorldVersions) {
    EXPECT_TRUE(lessThan("2.38", "2.39"));
    EXPECT_TRUE(lessThan("1:2.8.3", "1:2.9.0"));
    EXPECT_TRUE(lessThan(
        "2.42-4ubuntu2.9",
        "2.42-4ubuntu2.10"
    ));
    EXPECT_TRUE(lessThan(
        "46.0-1",
        "46.1-1"
    ));
}

TEST(VersionTest, ComparisonIsAntisymmetric) {
    const std::array<const char*, 8> versions{
        "1.0~rc1", "1.0", "1.0-1", "1.0-2",
        "1:0.5", "2.0", "0.9", "1.0a"
    };

    for (const char* left : versions) {
        for (const char* right : versions) {
            const int forward = compareVersions(left, right);
            const int backward = compareVersions(right, left);

            EXPECT_EQ(forward, -backward)
                << left << " vs " << right;
        }
    }
}

TEST(VersionTest, ValidatesWellFormedVersions) {
    EXPECT_EQ(validateVersion("1.2.3"), "");
    EXPECT_EQ(validateVersion("2:1.2.3-4ubuntu1"), "");
    EXPECT_EQ(validateVersion("1.0~rc1"), "");
}

TEST(VersionTest, RejectsMalformedVersions) {
    EXPECT_NE(validateVersion(""), "");
    EXPECT_NE(validateVersion("abc"), "");
    EXPECT_NE(validateVersion(":1.0"), "");
    EXPECT_NE(validateVersion("x:1.0"), "");
}

// Found by the differential fuzz test: a trailing hyphen is bad
// syntax to dpkg, not an empty revision.
TEST(VersionTest, RejectsEmptyRevision) {
    EXPECT_NE(validateVersion("1.0-"), "");
    EXPECT_NE(validateVersion("2:10beta.--"), "");

    // Parsing stays lenient; only validation rejects.
    EXPECT_EQ(parseVersion("1.0-").upstream, "1.0");
}

TEST(VersionTest, EvaluatesConstraints) {
    EXPECT_TRUE(satisfies(
        "2.39",
        VersionConstraint{VersionRelation::LaterOrEqual, "2.38"}
    ));
    EXPECT_FALSE(satisfies(
        "2.37",
        VersionConstraint{VersionRelation::LaterOrEqual, "2.38"}
    ));
    EXPECT_TRUE(satisfies(
        "2.38",
        VersionConstraint{VersionRelation::Exactly, "2.38"}
    ));
    EXPECT_TRUE(satisfies(
        "1.0~rc1",
        VersionConstraint{VersionRelation::Earlier, "1.0"}
    ));
    EXPECT_FALSE(satisfies(
        "1.0",
        VersionConstraint{VersionRelation::Earlier, "1.0"}
    ));
}

// Differential test against dpkg itself.
//
// dpkg is the definition of correct here, so any disagreement is a bug
// in this code. Skips cleanly when dpkg is not installed.

namespace {

bool haveDpkg() {
    return std::system("dpkg --version > /dev/null 2>&1") == 0;
}

// dpkg exits 0 for true, 1 for false, and 2 when it rejects the
// version outright. Collapsing 2 into "false" would silently turn a
// rejected version into "these are equal", which is how the first
// version of this test hid a validation bug.
int runDpkg(const std::string& command) {
    const int status =
        std::system((command + " > /dev/null 2>&1").c_str());

    if (status == -1) {
        return -1;
    }

    return WEXITSTATUS(status);
}

constexpr int kDpkgRejected = -2;

// Returns -1, 0 or 1 according to dpkg, or kDpkgRejected when dpkg
// considers either version malformed.
int dpkgCompare(const std::string& left, const std::string& right) {
    const std::string base =
        "dpkg --compare-versions '" + left + "' ";

    const int earlier = runDpkg(base + "lt '" + right + "'");

    if (earlier == 2) {
        return kDpkgRejected;
    }

    if (earlier == 0) {
        return -1;
    }

    const int later = runDpkg(base + "gt '" + right + "'");

    if (later == 2) {
        return kDpkgRejected;
    }

    if (later == 0) {
        return 1;
    }

    return 0;
}

std::string randomVersion(std::mt19937& engine) {
    // Pieces chosen to hit the parts of the algorithm that are easy to
    // get wrong: tildes, epochs, leading zeros, letters next to digits.
    static const std::array<const char*, 16> pieces{
        "0", "1", "2", "9", "10", "007",
        ".", "-", "~", "+", "a", "z",
        "rc", "beta", "ubuntu", "deb"
    };

    std::uniform_int_distribution<std::size_t> pieceIndex(
        0, pieces.size() - 1
    );
    std::uniform_int_distribution<int> lengthChoice(1, 6);
    std::uniform_int_distribution<int> epochChoice(0, 3);

    std::string version;

    const int epoch = epochChoice(engine);

    if (epoch > 0) {
        version += std::to_string(epoch);
        version += ":";
    }

    // Debian requires the upstream part to start with a digit.
    version += "1";

    const int length = lengthChoice(engine);

    for (int index = 0; index < length; ++index) {
        version += pieces[pieceIndex(engine)];
    }

    return version;
}

}

TEST(VersionFuzzTest, AgreesWithDpkgOnRandomVersions) {
    if (!haveDpkg()) {
        GTEST_SKIP() << "dpkg not available; skipping differential test";
    }

    std::mt19937 engine(20260828);

    std::vector<std::string> versions;

    while (versions.size() < 60) {
        const std::string candidate = randomVersion(engine);

        // Only compare versions dpkg itself considers well formed.
        if (validateVersion(candidate).empty()) {
            versions.push_back(candidate);
        }
    }

    std::size_t compared = 0;

    for (std::size_t i = 0; i < versions.size(); ++i) {
        for (std::size_t j = i; j < versions.size(); ++j) {
            const int mine = compareVersions(versions[i], versions[j]);
            const int theirs = dpkgCompare(versions[i], versions[j]);

            // validateVersion said both of these were well formed. If
            // dpkg disagrees, that is a validation bug on our side.
            ASSERT_NE(theirs, kDpkgRejected)
                << "validateVersion accepted a version dpkg rejects: '"
                << versions[i] << "' or '" << versions[j] << "'";

            const int normalised =
                (mine < 0) ? -1 : ((mine > 0) ? 1 : 0);

            ASSERT_EQ(normalised, theirs)
                << "disagreement on '" << versions[i]
                << "' vs '" << versions[j] << "'";

            compared += 1;
        }
    }

    EXPECT_GT(compared, 1000u);
}

TEST(VersionFuzzTest, AgreesWithDpkgOnKnownHardCases) {
    if (!haveDpkg()) {
        GTEST_SKIP() << "dpkg not available; skipping differential test";
    }

    static const std::array<const char*, 20> cases{
        "1.0", "1.0~", "1.0~~", "1.0~~a", "1.0a", "1.0+",
        "1.0-1", "1.0-1~", "1:1.0", "2:1.0", "1.007", "1.7",
        "1.10", "1.9", "1.0.0", "1.0", "1.0-0", "1.0-00",
        "1.0~rc1", "1.0~rc2"
    };

    for (const char* left : cases) {
        for (const char* right : cases) {
            const int theirs = dpkgCompare(left, right);

            if (theirs == kDpkgRejected) {
                continue;
            }

            const int mine = compareVersions(left, right);

            const int normalised =
                (mine < 0) ? -1 : ((mine > 0) ? 1 : 0);

            ASSERT_EQ(normalised, theirs)
                << "disagreement on '" << left
                << "' vs '" << right << "'";
        }
    }
}
