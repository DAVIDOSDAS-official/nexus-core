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
