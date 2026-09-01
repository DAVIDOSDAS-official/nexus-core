#include <gtest/gtest.h>

#include <stdexcept>

#include <nexus/diagnosis.hpp>
#include <nexus/system/version.hpp>

using nexus::Capability;
using nexus::Component;
using nexus::ComponentType;
using nexus::ConflictDetector;
using nexus::Constraint;
using nexus::diagnose;
using nexus::Finding;
using nexus::Health;
using nexus::Requirement;

namespace {

ConflictDetector detector() {
    return ConflictDetector(
        [](const std::string& left, const std::string& right) {
            return nexus::system::compareVersions(left, right);
        }
    );
}

Component make(
    const std::string& id,
    const std::vector<std::string>& needs = {}
) {
    Component component(id, id, "1.0", ComponentType::Application);

    component.addProvidedCapability(Capability(id));

    for (const std::string& need : needs) {
        component.addRequirement(Requirement(Constraint(need)));
    }

    return component;
}

Finding findingFor(
    const nexus::Diagnosis& diagnosis,
    const std::string& check
) {
    for (const Finding& finding : diagnosis.findings) {
        if (finding.check == check) {
            return finding;
        }
    }

    throw std::runtime_error("no finding for " + check);
}

}

TEST(DiagnosisTest, AHealthySystemReportsHealthy) {
    const std::vector<Component> installed{
        make("app", {"lib"}),
        make("lib")
    };

    const auto diagnosis =
        diagnose(installed, {"app"}, detector());

    EXPECT_EQ(diagnosis.overall(), Health::Ok);
    EXPECT_EQ(findingFor(diagnosis, "Dependencies").health, Health::Ok);
}

// What everybody means by "broken packages", and what a package
// manager usually reports only when you try to change something.
TEST(DiagnosisTest, FindsARequirementNothingSatisfies) {
    const std::vector<Component> installed{
        make("app", {"missing-library"})
    };

    const auto diagnosis =
        diagnose(installed, {"app"}, detector());

    const Finding finding = findingFor(diagnosis, "Dependencies");

    EXPECT_EQ(finding.health, Health::Problem);
    EXPECT_EQ(finding.total, 1u);
    EXPECT_NE(
        finding.examples[0].find("missing-library"),
        std::string::npos
    );
    EXPECT_FALSE(finding.suggestion.empty());
    EXPECT_EQ(diagnosis.overall(), Health::Problem);
}

TEST(DiagnosisTest, AnAlternativeIsEnoughToBeSatisfied) {
    Component app = make("app");

    app.addRequirement(Requirement(std::vector<Constraint>{
        Constraint("absent"), Constraint("present")
    }));

    const auto diagnosis = diagnose(
        {app, make("present")}, {"app"}, detector());

    EXPECT_EQ(findingFor(diagnosis, "Dependencies").health, Health::Ok);
}

TEST(DiagnosisTest, FindsALiveConflict) {
    Component exim = make("exim4");

    exim.addConflict(Constraint("postfix"));

    const auto diagnosis = diagnose(
        {exim, make("postfix")}, {"exim4", "postfix"}, detector());

    EXPECT_EQ(findingFor(diagnosis, "Conflicts").health,
              Health::Problem);
}

// Unused is untidy, not broken. Calling it a problem trains people to
// ignore the word.
TEST(DiagnosisTest, UnusedIsAWarningNotAProblem) {
    const std::vector<Component> installed{
        make("wanted"),
        make("leftover")
    };

    const auto diagnosis =
        diagnose(installed, {"wanted"}, detector());

    const Finding finding = findingFor(diagnosis, "Unused");

    EXPECT_EQ(finding.health, Health::Warning);
    EXPECT_EQ(finding.total, 1u);
    EXPECT_EQ(finding.examples[0], "leftover");
    EXPECT_EQ(diagnosis.overall(), Health::Warning);
}

// Without a record of what was wanted, nothing can be called unused --
// and saying so is better than reporting every package as leftover.
TEST(DiagnosisTest, WithoutRootsUnusedIsUnknown) {
    const auto diagnosis = diagnose(
        {make("a"), make("b")}, {}, detector());

    EXPECT_EQ(findingFor(diagnosis, "Unused").health, Health::Unknown);
    EXPECT_EQ(findingFor(diagnosis, "Unused").total, 0u);
}

// A diagnosis nobody reads is a diagnosis that did not happen.
TEST(DiagnosisTest, KeepsAFewExamplesNotTheWholeList) {
    std::vector<Component> installed;

    for (int index = 0; index < 40; ++index) {
        installed.push_back(
            make("app-" + std::to_string(index), {"missing"}));
    }

    const auto diagnosis = diagnose(installed, {"app-0"}, detector());

    const Finding finding = findingFor(diagnosis, "Dependencies");

    EXPECT_EQ(finding.total, 40u);
    EXPECT_LE(finding.examples.size(), 5u);
}

TEST(DiagnosisTest, ProblemOutranksWarning) {
    const std::vector<Component> installed{
        make("app", {"missing"}),
        make("leftover")
    };

    const auto diagnosis =
        diagnose(installed, {"app"}, detector());

    EXPECT_EQ(diagnosis.overall(), Health::Problem);
}
