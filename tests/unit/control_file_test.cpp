#include <gtest/gtest.h>

#include <sstream>

#include <nexus/system/control_file.hpp>

using nexus::system::ControlStanza;
using nexus::system::parseControlStream;

TEST(ControlFileTest, ParsesSingleStanza) {
    std::istringstream input(
        "Package: apt\n"
        "Version: 2.8.3\n"
    );

    const auto stanzas = parseControlStream(input);

    ASSERT_EQ(stanzas.size(), 1u);
    EXPECT_EQ(stanzas[0].value("Package"), "apt");
    EXPECT_EQ(stanzas[0].value("Version"), "2.8.3");
}

TEST(ControlFileTest, SeparatesStanzasOnBlankLine) {
    std::istringstream input(
        "Package: apt\n"
        "\n"
        "Package: dpkg\n"
    );

    const auto stanzas = parseControlStream(input);

    ASSERT_EQ(stanzas.size(), 2u);
    EXPECT_EQ(stanzas[0].value("Package"), "apt");
    EXPECT_EQ(stanzas[1].value("Package"), "dpkg");
}

TEST(ControlFileTest, FieldNamesAreCaseInsensitive) {
    std::istringstream input("PACKAGE: apt\n");

    const auto stanzas = parseControlStream(input);

    ASSERT_EQ(stanzas.size(), 1u);
    EXPECT_EQ(stanzas[0].value("package"), "apt");
    EXPECT_EQ(stanzas[0].value("Package"), "apt");
}

TEST(ControlFileTest, JoinsContinuationLines) {
    std::istringstream input(
        "Package: apt\n"
        "Description: package manager\n"
        " second line\n"
        " third line\n"
        "Version: 2.8.3\n"
    );

    const auto stanzas = parseControlStream(input);

    ASSERT_EQ(stanzas.size(), 1u);
    EXPECT_EQ(
        stanzas[0].value("Description"),
        "package manager\nsecond line\nthird line"
    );
    EXPECT_EQ(stanzas[0].value("Version"), "2.8.3");
}

TEST(ControlFileTest, ReportsMissingFieldsAsAbsent) {
    std::istringstream input("Package: apt\n");

    const auto stanzas = parseControlStream(input);

    ASSERT_EQ(stanzas.size(), 1u);
    EXPECT_FALSE(stanzas[0].has("Depends"));
    EXPECT_FALSE(stanzas[0].get("Depends").has_value());
    EXPECT_EQ(stanzas[0].value("Depends"), "");
}

TEST(ControlFileTest, IgnoresTrailingBlankLines) {
    std::istringstream input(
        "Package: apt\n"
        "\n"
        "\n"
        "\n"
    );

    const auto stanzas = parseControlStream(input);

    EXPECT_EQ(stanzas.size(), 1u);
}

// Comments.
//
// The profiles and alias tables are written in this format by hand and
// are full of them. A colonless comment was always harmless: it is
// dropped as malformed without ending the field above it. A comment
// with a colon is read as a field, and that is the case that loses
// data.

TEST(ControlFileTest, ACommentWithAColonDoesNotSwallowContinuations) {
    std::istringstream input(
        "Profile: showcase\n"
        "Requires: init,\n"
        " c-library,\n"
        "# one more, because: a machine needs somewhere to type\n"
        " terminal-emulator\n"
    );

    const auto stanzas = parseControlStream(input);

    ASSERT_EQ(stanzas.size(), 1u);
    EXPECT_EQ(
        stanzas[0].value("Requires"),
        "init,\nc-library,\nterminal-emulator"
    );
}

TEST(ControlFileTest, ACommentContainingAColonIsNotAField) {
    std::istringstream input(
        "Profile: minimal\n"
        "# Stated for the same reason minimalism states its own: a\n"
        "# profile with no preference picks whatever is cheapest.\n"
        "Prefers: text-editor=nano\n"
    );

    const auto stanzas = parseControlStream(input);

    ASSERT_EQ(stanzas.size(), 1u);
    EXPECT_EQ(stanzas[0].value("Prefers"), "text-editor=nano");
    EXPECT_EQ(stanzas[0].fields().size(), 2u);
}

// Guards the fix rather than the bug: skipping comments must not
// start skipping values that happen to contain a hash.
TEST(ControlFileTest, AHashInsideAValueIsPartOfTheValue) {
    std::istringstream input(
        "Package: apt\n"
        "Description: counts things\n"
        " see #1234 for why\n"
    );

    const auto stanzas = parseControlStream(input);

    ASSERT_EQ(stanzas.size(), 1u);
    EXPECT_EQ(
        stanzas[0].value("Description"),
        "counts things\nsee #1234 for why"
    );
}
