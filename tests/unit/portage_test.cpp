#include <gtest/gtest.h>

#include <nexus/system/portage_source.hpp>

using nexus::VersionRelation;
using nexus::system::defaultUseFlags;
using nexus::system::parsePortageAtom;
using nexus::system::parsePortageEntry;
using nexus::system::splitNameVersion;

namespace {

// Copied from the real tree. Every part of this is something a
// made-up fixture would have simplified away.
const char* kNano =
    "BDEPEND=verify-sig? ( sec-keys/openpgp-keys-bennoschulenberg ) "
    "virtual/pkgconfig nls? ( sys-devel/gettext )\n"
    "DEFINED_PHASES=configure install postrm prepare unpack\n"
    "DEPEND=>=sys-libs/ncurses-5.9-r1:=[unicode(+)?] "
    "magic? ( sys-apps/file ) nls? ( virtual/libintl )\n"
    "DESCRIPTION=GNU GPL'd Pico clone with more functionality\n"
    "EAPI=8\n"
    "IUSE=debug justify magic minimal ncurses nls +spell unicode "
    "verify-sig\n"
    "RDEPEND=>=sys-libs/ncurses-5.9-r1:=[unicode(+)?] "
    "magic? ( sys-apps/file ) nls? ( virtual/libintl )\n"
    "REQUIRED_USE=magic? ( !minimal )\n"
    "SLOT=0\n";

}

// Names contain hyphens too, so splitting at the first or the last
// one is wrong in both directions. A version begins at a hyphen
// followed by a digit.
TEST(PortageTest, SplitsNamesFromVersions) {
    EXPECT_EQ(splitNameVersion("nano-8.7.1"),
              std::make_pair(std::string("nano"),
                             std::string("8.7.1")));

    EXPECT_EQ(
        splitNameVersion("openpgp-keys-bennoschulenberg-20240101"),
        std::make_pair(
            std::string("openpgp-keys-bennoschulenberg"),
            std::string("20240101")));

    EXPECT_EQ(splitNameVersion("gpg"),
              std::make_pair(std::string("gpg"), std::string("")));
}

TEST(PortageTest, ReadsVersionOperators) {
    const auto atLeast = parsePortageAtom(">=sys-libs/ncurses-5.9-r1");

    EXPECT_EQ(atLeast.capability, "sys-libs/ncurses");
    ASSERT_TRUE(atLeast.version.has_value());
    EXPECT_EQ(atLeast.version->relation, VersionRelation::LaterOrEqual);
    EXPECT_EQ(atLeast.version->version, "5.9-r1");

    EXPECT_EQ(parsePortageAtom("<app-x/y-2.0").version->relation,
              VersionRelation::Earlier);
    EXPECT_EQ(parsePortageAtom("=app-x/y-2.0").version->relation,
              VersionRelation::Exactly);
}

// A slot dependency is a compatibility declaration and a USE
// dependency says how the thing must be built. Neither is part of
// which package is meant.
TEST(PortageTest, SlotsAndUseDepsAreNotPartOfTheName) {
    EXPECT_EQ(
        parsePortageAtom(">=sys-libs/ncurses-5.9-r1:=[unicode(+)?]")
            .capability,
        "sys-libs/ncurses");

    EXPECT_EQ(parsePortageAtom("app-crypt/gnupg[-alternatives(-)]")
                  .capability,
              "app-crypt/gnupg");

    EXPECT_EQ(parsePortageAtom("dev-libs/foo:0").capability,
              "dev-libs/foo");
}

TEST(PortageTest, ABlockIsNotADependency) {
    EXPECT_TRUE(parsePortageAtom("!app-editors/vim").capability.empty());
}

// IUSE lists "+spell" for on and "spell" for off.
TEST(PortageTest, ReadsWhichUseFlagsDefaultOn) {
    const auto on = defaultUseFlags(
        "debug justify magic minimal ncurses nls +spell unicode");

    EXPECT_EQ(on.size(), 1u);
    EXPECT_EQ(on.count("spell"), 1u);
    EXPECT_EQ(on.count("magic"), 0u);
}

TEST(PortageTest, ReadsAPackage) {
    const auto result =
        parsePortageEntry("app-editors", "nano-8.7.1", kNano);

    ASSERT_EQ(result.components.size(), 1u);

    const auto& component = result.components[0];

    EXPECT_EQ(component.id(), "app-editors/nano");
    EXPECT_EQ(component.version(), "8.7.1");
}

// Profiles ask for "nano"; portage calls it "app-editors/nano".
// Something has to bridge them, and providing both spellings is
// cheaper than a translation table per category.
TEST(PortageTest, ProvidesBothSpellings) {
    const auto result =
        parsePortageEntry("app-editors", "nano-8.7.1", kNano);

    ASSERT_EQ(result.components.size(), 1u);

    bool shortName = false;
    bool qualified = false;

    for (const auto& capability :
         result.components[0].providedCapabilities()) {

        shortName = shortName || capability.name() == "nano";
        qualified =
            qualified || capability.name() == "app-editors/nano";
    }

    EXPECT_TRUE(shortName);
    EXPECT_TRUE(qualified);
}

// magic and nls are both off by default, so neither of their
// dependencies applies; ncurses is unconditional and does.
TEST(PortageTest, ConditionsAreEvaluatedAgainstFlagDefaults) {
    const auto result =
        parsePortageEntry("app-editors", "nano-8.7.1", kNano);

    ASSERT_EQ(result.components.size(), 1u);

    const auto& requirements = result.components[0].requirements();

    ASSERT_EQ(requirements.size(), 1u);
    EXPECT_EQ(requirements[0].alternatives[0].capability,
              "sys-libs/ncurses");
}

// Counted rather than hidden: a reader that silently guesses is
// answering a different question from the one asked.
TEST(PortageTest, GuessedConditionsAreCounted) {
    const auto result =
        parsePortageEntry("app-editors", "nano-8.7.1", kNano);

    EXPECT_EQ(result.conditional, 2u);
    EXPECT_EQ(result.gaps.count("required-use constraints"), 1u);
}

TEST(PortageTest, AnyOfBecomesAlternatives) {
    const auto result = parsePortageEntry(
        "app-x", "y-1.0",
        "IUSE=\n"
        "RDEPEND=|| ( app-alternatives/gpg app-crypt/gnupg )\n");

    ASSERT_EQ(result.components.size(), 1u);

    const auto& requirements = result.components[0].requirements();

    ASSERT_EQ(requirements.size(), 1u);
    EXPECT_EQ(requirements[0].alternatives.size(), 2u);
}

TEST(PortageTest, AFlagThatIsOnBringsItsDependencies) {
    const auto result = parsePortageEntry(
        "app-x", "y-1.0",
        "IUSE=+spell\n"
        "RDEPEND=spell? ( app-text/aspell )\n");

    ASSERT_EQ(result.components.size(), 1u);
    ASSERT_EQ(result.components[0].requirements().size(), 1u);
    EXPECT_EQ(
        result.components[0].requirements()[0].alternatives[0]
            .capability,
        "app-text/aspell");
}

// Gentoo spells "build from the project's git head" as 9999. It is
// never what somebody means by "install nano", and it sorts above
// every real version -- so a resolver preferring the newest picks it
// every time.
TEST(PortageTest, RecognisesLiveEbuilds) {
    EXPECT_TRUE(nexus::system::isLiveEbuild("9999"));
    EXPECT_TRUE(nexus::system::isLiveEbuild("99999999"));

    EXPECT_FALSE(nexus::system::isLiveEbuild("9.2"));
    EXPECT_FALSE(nexus::system::isLiveEbuild("999"));
    EXPECT_FALSE(nexus::system::isLiveEbuild("8.7.1"));
    EXPECT_FALSE(nexus::system::isLiveEbuild(""));
}

// Portage compares numerically per component, so 9.10 is later than
// 9.9 -- which a string comparison gets backwards.
TEST(PortageTest, ComparesVersionsNumerically) {
    using nexus::system::comparePortageVersions;

    EXPECT_GT(comparePortageVersions("9.10", "9.9"), 0);
    EXPECT_LT(comparePortageVersions("9.9", "9.10"), 0);
    EXPECT_EQ(comparePortageVersions("9.2", "9.2"), 0);

    EXPECT_GT(comparePortageVersions("9.2", "8.7.1"), 0);

    // A revision orders after the version it revises.
    EXPECT_GT(comparePortageVersions("5.9-r1", "5.9"), 0);
}
