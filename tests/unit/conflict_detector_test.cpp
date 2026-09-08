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

// Regression: this exact shape produced six false conflicts on a real
// Ubuntu system before capabilities carried versions. perl's own
// version (5.38.2) is far above the 3.15 threshold, but the version
// that matters is the one it provides libnet-perl at.
TEST(ConflictDetectorTest, VersionedProvidesIsCheckedNotPackageVersion) {
    Component modules = make("perl-modules-5.38", "5.38.2");
    modules.addConflict(Constraint(
        "libnet-perl",
        VersionConstraint{VersionRelation::Earlier, "1:3.15"}
    ));

    Component perl = make("perl", "5.38.2-3.2ubuntu0.2");
    perl.addProvidedCapability(nexus::Capability(
        "libnet-perl",
        "1:3.15"
    ));

    EXPECT_TRUE(debianDetector().detect({modules, perl}).empty());
}

TEST(ConflictDetectorTest, UnversionedProvidesFallsBackToPackageVersion) {
    Component sendmail = make("sendmail", "8.18");
    sendmail.addConflict(Constraint(
        "mail-transport-agent",
        VersionConstraint{VersionRelation::Earlier, "4.0"}
    ));

    Component postfix = make("postfix", "3.8.6");
    postfix.addProvidedCapability(nexus::Capability(
        "mail-transport-agent"
    ));

    // No version on the provides, so postfix's own 3.8.6 is used,
    // which is below 4.0 and therefore a real conflict.
    EXPECT_EQ(debianDetector().detect({sendmail, postfix}).size(), 1u);
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

// Two builds of one package are not two packages. A Multi-Arch: same
// library declares a conflict against its own name to exclude older
// versions of itself, and the other architecture's build matches it --
// while being exactly what multi-arch exists to allow alongside.
TEST(ConflictDetectorTest, APackageDoesNotConflictWithItsOtherBuild) {
    Component amd64("libjack:amd64", "libjack", "1.9",
                    ComponentType::Library);
    amd64.setArchitecture("amd64");
    amd64.setMultiArch(nexus::MultiArch::Same);
    amd64.addProvidedCapability(nexus::Capability("libjack"));
    amd64.addConflict(Constraint("libjack"));

    Component i386("libjack:i386", "libjack", "1.9",
                   ComponentType::Library);
    i386.setArchitecture("i386");
    i386.setMultiArch(nexus::MultiArch::Same);
    i386.addProvidedCapability(nexus::Capability("libjack"));
    i386.addConflict(Constraint("libjack"));

    EXPECT_TRUE(debianDetector().detect({amd64, i386}).empty());
    EXPECT_TRUE(debianDetector().check(amd64, {i386}).empty());
}

// A genuine conflict between different packages still stands.
TEST(ConflictDetectorTest, DifferentPackagesStillConflict) {
    Component exim = make("exim4", "4.97");
    exim.addConflict(Constraint("postfix"));

    EXPECT_EQ(
        debianDetector().detect({exim, make("postfix", "3.8")}).size(),
        1u
    );
}

// A component may provide the same capability at several versions.
// debhelper provides debhelper-compat at 9, 10, 11, 12 and 13, and
// any of them can satisfy a constraint -- so checking only the first
// answers about the wrong one four times out of five, and every
// Debian source build asking for compat 13 was refused by the package
// that provides it.
TEST(ConflictDetectorTest, AnyProvidedVersionCanSatisfy) {
    Component debhelper("debhelper", "debhelper", "13.14.1",
                        ComponentType::Application);

    for (const char* version : {"9", "10", "11", "12", "13"}) {
        debhelper.addProvidedCapability(
            nexus::Capability("debhelper-compat", version));
    }

    const auto exactly = [](const std::string& version) {
        Constraint constraint("debhelper-compat");

        constraint.version = nexus::VersionConstraint{
            nexus::VersionRelation::Exactly, version};

        return constraint;
    };

    // The first one still matches.
    EXPECT_TRUE(debianDetector().matches(debhelper, exactly("9")));

    // And so does the last, which is the one that was failing.
    EXPECT_TRUE(debianDetector().matches(debhelper, exactly("13")));
    EXPECT_TRUE(debianDetector().matches(debhelper, exactly("11")));

    // One it does not provide still does not match.
    EXPECT_FALSE(debianDetector().matches(debhelper, exactly("14")));
}

TEST(ConflictDetectorTest, RangesWorkAcrossSeveralProvides) {
    Component debhelper("debhelper", "debhelper", "13.14.1",
                        ComponentType::Application);

    debhelper.addProvidedCapability(
        nexus::Capability("debhelper-compat", "9"));
    debhelper.addProvidedCapability(
        nexus::Capability("debhelper-compat", "13"));

    Constraint atLeast("debhelper-compat");

    atLeast.version = nexus::VersionConstraint{
        nexus::VersionRelation::LaterOrEqual, "12"};

    EXPECT_TRUE(debianDetector().matches(debhelper, atLeast));
}
