#include <gtest/gtest.h>

#include <nexus/system/dependency_expression.hpp>

using nexus::system::parseDependencyField;
using nexus::system::parseProvidesField;
using nexus::system::VersionRelation;

TEST(DependencyExpressionTest, ParsesSingleUnversionedDependency) {
    const auto clauses = parseDependencyField("gpgv");

    ASSERT_EQ(clauses.size(), 1u);
    ASSERT_EQ(clauses[0].alternatives.size(), 1u);
    EXPECT_EQ(clauses[0].alternatives[0].name, "gpgv");
    EXPECT_FALSE(clauses[0].alternatives[0].constraint.has_value());
    EXPECT_TRUE(clauses[0].isSimple());
}

TEST(DependencyExpressionTest, ParsesMultipleClauses) {
    const auto clauses = parseDependencyField("gpgv, ubuntu-keyring");

    ASSERT_EQ(clauses.size(), 2u);
    EXPECT_EQ(clauses[0].alternatives[0].name, "gpgv");
    EXPECT_EQ(clauses[1].alternatives[0].name, "ubuntu-keyring");
}

TEST(DependencyExpressionTest, ParsesVersionConstraint) {
    const auto clauses = parseDependencyField("libc6 (>= 2.38)");

    ASSERT_EQ(clauses.size(), 1u);
    ASSERT_TRUE(clauses[0].alternatives[0].constraint.has_value());
    EXPECT_EQ(
        clauses[0].alternatives[0].constraint->relation,
        VersionRelation::LaterOrEqual
    );
    EXPECT_EQ(clauses[0].alternatives[0].constraint->version, "2.38");
}

TEST(DependencyExpressionTest, ParsesAllVersionRelations) {
    EXPECT_EQ(
        parseDependencyField("a (<< 1)")[0]
            .alternatives[0].constraint->relation,
        VersionRelation::Earlier
    );
    EXPECT_EQ(
        parseDependencyField("a (<= 1)")[0]
            .alternatives[0].constraint->relation,
        VersionRelation::EarlierOrEqual
    );
    EXPECT_EQ(
        parseDependencyField("a (= 1)")[0]
            .alternatives[0].constraint->relation,
        VersionRelation::Exactly
    );
    EXPECT_EQ(
        parseDependencyField("a (>> 1)")[0]
            .alternatives[0].constraint->relation,
        VersionRelation::Later
    );
}

TEST(DependencyExpressionTest, ParsesAlternatives) {
    const auto clauses =
        parseDependencyField("base-passwd (>= 3.6.1) | adduser");

    ASSERT_EQ(clauses.size(), 1u);
    ASSERT_EQ(clauses[0].alternatives.size(), 2u);
    EXPECT_EQ(clauses[0].alternatives[0].name, "base-passwd");
    EXPECT_EQ(clauses[0].alternatives[1].name, "adduser");
    EXPECT_FALSE(clauses[0].isSimple());
}

TEST(DependencyExpressionTest, ParsesArchitectureQualifier) {
    const auto clauses = parseDependencyField("libc6:amd64");

    ASSERT_EQ(clauses.size(), 1u);
    EXPECT_EQ(clauses[0].alternatives[0].name, "libc6");
    ASSERT_TRUE(clauses[0].alternatives[0].architecture.has_value());
    EXPECT_EQ(*clauses[0].alternatives[0].architecture, "amd64");
}

TEST(DependencyExpressionTest, StripsArchitectureRestrictions) {
    const auto clauses = parseDependencyField("libc6 [amd64 arm64]");

    ASSERT_EQ(clauses.size(), 1u);
    EXPECT_EQ(clauses[0].alternatives[0].name, "libc6");
}

TEST(DependencyExpressionTest, StripsBuildProfiles) {
    const auto clauses = parseDependencyField("dh-python <!nocheck>");

    ASSERT_EQ(clauses.size(), 1u);
    EXPECT_EQ(clauses[0].alternatives[0].name, "dh-python");
}

TEST(DependencyExpressionTest, HandlesEmptyField) {
    EXPECT_TRUE(parseDependencyField("").empty());
    EXPECT_TRUE(parseDependencyField("   ").empty());
    EXPECT_TRUE(parseDependencyField(",,").empty());
}

TEST(DependencyExpressionTest, ParsesVersionedProvides) {
    const auto terms =
        parseProvidesField("apt-transport-https (= 2.8.3), foo");

    ASSERT_EQ(terms.size(), 2u);
    EXPECT_EQ(terms[0].name, "apt-transport-https");
    ASSERT_TRUE(terms[0].constraint.has_value());
    EXPECT_EQ(terms[0].constraint->version, "2.8.3");
    EXPECT_EQ(terms[1].name, "foo");
    EXPECT_FALSE(terms[1].constraint.has_value());
}

TEST(DependencyExpressionTest, RoundTripsToString) {
    const auto clauses =
        parseDependencyField("base-passwd (>= 3.6.1) | adduser");

    EXPECT_EQ(
        toString(clauses[0]),
        "base-passwd (>= 3.6.1) | adduser"
    );
}
