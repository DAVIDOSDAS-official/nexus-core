#include <gtest/gtest.h>

#include <nexus/component.hpp>
#include <nexus/conflict_detector.hpp>
#include <nexus/system/version.hpp>

using nexus::Component;
using nexus::ComponentType;
using nexus::Conflict;
using nexus::ConflictDetector;
using nexus::Constraint;
using nexus::VersionConstraint;
using nexus::VersionRelation;

namespace {

// The system layer supplies the Debian comparison algorithm; core
// stays independent of any one package ecosystem.
ConflictDetector debianDetector() {
    return ConflictDetector(
        [](const std::string& left, const std::string& right) {
            return nexus::system::compareVersions(left, right);
        }
    );
}

Component make(
    const std::string& id,
    const std::string& version
) {
    return Component(id, id, version, ComponentType::Application);
}

}

TEST(ConflictDetectorTest, FindsNoConflictWhenNoneDeclared) {
    const std::vector<Component> components{
        make("apt", "2.8.3"),
        make("dpkg", "1.22.6")
    };

    EXPECT_TRUE(debianDetector().detect(components).empty());
}

TEST(ConflictDetectorTest, FindsUnversionedConflict) {
    Component exim = make("exim4", "4.97");
    exim.addConflict(Constraint("postfix"));

    const std::vector<Component> components{
        exim,
        make("postfix", "3.8.6")
    };

    const auto conflicts = debianDetector().detect(components);

    ASSERT_EQ(conflicts.size(), 1u);
    EXPECT_EQ(conflicts[0].componentId, "exim4");
    EXPECT_EQ(conflicts[0].conflictsWith, "postfix");
}

TEST(ConflictDetectorTest, VersionConditionRulesConflictIn) {
    Component binutils = make("binutils", "2.42");
    binutils.addConflict(Constraint(
        "aptitude",
        VersionConstraint{VersionRelation::Earlier, "0.8.10"}
    ));

    const std::vector<Component> components{
        binutils,
        make("aptitude", "0.8.9")
    };

    EXPECT_EQ(debianDetector().detect(components).size(), 1u);
}

TEST(ConflictDetectorTest, VersionConditionRulesConflictOut) {
    Component binutils = make("binutils", "2.42");
    binutils.addConflict(Constraint(
        "aptitude",
        VersionConstraint{VersionRelation::Earlier, "0.8.10"}
    ));

    const std::vector<Component> components{
        binutils,
        make("aptitude", "0.8.13")
    };

    EXPECT_TRUE(debianDetector().detect(components).empty());
}

TEST(ConflictDetectorTest, ConflictMatchesProvidedCapability) {
    Component sendmail = make("sendmail", "8.18");
    sendmail.addConflict(Constraint("mail-transport-agent"));

    Component postfix = make("postfix", "3.8.6");
    postfix.addProvidedCapability(nexus::Capability(
        "mail-transport-agent"
    ));

    const auto conflicts =
        debianDetector().detect({sendmail, postfix});

    ASSERT_EQ(conflicts.size(), 1u);
    EXPECT_EQ(conflicts[0].conflictsWith, "postfix");
}

TEST(ConflictDetectorTest, ComponentDoesNotConflictWithItself) {
    Component apt = make("apt", "2.8.3");
    apt.addConflict(Constraint("apt"));

    EXPECT_TRUE(debianDetector().detect({apt}).empty());
}

TEST(ConflictDetectorTest, CheckReportsBothDirections) {
    Component installed = make("postfix", "3.8.6");
    installed.addConflict(Constraint("exim4"));

    Component candidate = make("exim4", "4.97");
    candidate.addConflict(Constraint("postfix"));

    const auto conflicts =
        debianDetector().check(candidate, {installed});

    // One from the candidate, one from what is already installed.
    ASSERT_EQ(conflicts.size(), 2u);
}

TEST(ConflictDetectorTest, CheckIsCleanWhenNothingCollides) {
    Component candidate = make("exim4", "4.97");
    candidate.addConflict(Constraint("postfix"));

    const auto conflicts =
        debianDetector().check(candidate, {make("apt", "2.8.3")});

    EXPECT_TRUE(conflicts.empty());
}

// Without a comparator, core cannot evaluate a version condition. It
// must over-report rather than stay silent.
TEST(ConflictDetectorTest, WithoutComparatorAssumesConditionHolds) {
    Component binutils = make("binutils", "2.42");
    binutils.addConflict(Constraint(
        "aptitude",
        VersionConstraint{VersionRelation::Earlier, "0.8.10"}
    ));

    const ConflictDetector detector;

    EXPECT_FALSE(detector.hasComparator());
    EXPECT_EQ(
        detector.detect({binutils, make("aptitude", "0.8.13")}).size(),
        1u
    );
}

TEST(ConflictDetectorTest, ReasonNamesBothComponents) {
    Component exim = make("exim4", "4.97");
    exim.addConflict(Constraint("postfix"));

    const auto conflicts =
        debianDetector().detect({exim, make("postfix", "3.8.6")});

    ASSERT_EQ(conflicts.size(), 1u);
    EXPECT_NE(conflicts[0].reason.find("exim4"), std::string::npos);
    EXPECT_NE(conflicts[0].reason.find("postfix"), std::string::npos);
}
