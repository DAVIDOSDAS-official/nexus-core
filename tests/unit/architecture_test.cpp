#include <gtest/gtest.h>

#include <nexus/architecture.hpp>
#include <nexus/solver.hpp>
#include <nexus/system/version.hpp>

using nexus::architectureSatisfies;
using nexus::Component;
using nexus::ComponentType;
using nexus::ConflictDetector;
using nexus::Constraint;
using nexus::kArchitectureAll;
using nexus::MultiArch;
using nexus::parseMultiArch;
using nexus::Requirement;
using nexus::Solver;
using nexus::SolverRequest;
using nexus::SolverStatus;

namespace {

ConflictDetector debianDetector() {
    return ConflictDetector(
        [](const std::string& left, const std::string& right) {
            return nexus::system::compareVersions(left, right);
        }
    );
}

Component make(
    const std::string& id,
    const std::string& architecture,
    MultiArch multiArch = MultiArch::No
) {
    Component component(id, id, "1.0", ComponentType::Library);
    component.setArchitecture(architecture);
    component.setMultiArch(multiArch);
    return component;
}

}

TEST(ArchitectureTest, ParsesMultiArchField) {
    EXPECT_EQ(parseMultiArch("same"), MultiArch::Same);
    EXPECT_EQ(parseMultiArch("foreign"), MultiArch::Foreign);
    EXPECT_EQ(parseMultiArch("allowed"), MultiArch::Allowed);
    EXPECT_EQ(parseMultiArch(""), MultiArch::No);
    EXPECT_EQ(parseMultiArch("nonsense"), MultiArch::No);
}

TEST(ArchitectureTest, ArchIndependentSatisfiesAnything) {
    EXPECT_TRUE(architectureSatisfies(
        kArchitectureAll, MultiArch::No, std::nullopt, "amd64"
    ));
    EXPECT_TRUE(architectureSatisfies(
        kArchitectureAll, MultiArch::No, std::nullopt, "i386"
    ));
}

TEST(ArchitectureTest, MatchingArchitectureSatisfies) {
    EXPECT_TRUE(architectureSatisfies(
        "amd64", MultiArch::Same, std::nullopt, "amd64"
    ));
}

// The whole point: a 32-bit library does not satisfy a 64-bit binary.
TEST(ArchitectureTest, MismatchedArchitectureDoesNotSatisfy) {
    EXPECT_FALSE(architectureSatisfies(
        "i386", MultiArch::Same, std::nullopt, "amd64"
    ));
}

TEST(ArchitectureTest, ForeignSatisfiesAnyArchitecture) {
    EXPECT_TRUE(architectureSatisfies(
        "amd64", MultiArch::Foreign, std::nullopt, "i386"
    ));
}

TEST(ArchitectureTest, ExplicitQualifierMustMatch) {
    EXPECT_TRUE(architectureSatisfies(
        "i386", MultiArch::Same, std::string("i386"), "amd64"
    ));
    EXPECT_FALSE(architectureSatisfies(
        "amd64", MultiArch::Same, std::string("i386"), "amd64"
    ));
}

TEST(ArchitectureTest, AnyQualifierNeedsAnOptIn) {
    EXPECT_TRUE(architectureSatisfies(
        "i386", MultiArch::Allowed, std::string("any"), "amd64"
    ));
    EXPECT_FALSE(architectureSatisfies(
        "i386", MultiArch::No, std::string("any"), "amd64"
    ));
}

// Architecture-blind callers must keep behaving exactly as before.
TEST(ArchitectureTest, EmptyRequesterIgnoresArchitecture) {
    EXPECT_TRUE(architectureSatisfies(
        "i386", MultiArch::Same, std::nullopt, ""
    ));
}

TEST(ArchitectureSolverTest, WillNotUseAForeignArchLibrary) {
    Component app = make("app", "amd64");
    app.addRequirement(Requirement(Constraint("libfoo")));

    // Only the 32-bit build is available.
    Solver solver(
        {app, make("libfoo", "i386", MultiArch::Same)},
        debianDetector()
    );

    SolverRequest request;
    request.architecture = "amd64";
    request.requirements.push_back(Requirement(Constraint("app")));

    EXPECT_EQ(solver.solve(request).status, SolverStatus::Unsatisfiable);
}

TEST(ArchitectureSolverTest, PicksTheMatchingArchitecture) {
    Component app = make("app", "amd64");
    app.addRequirement(Requirement(Constraint("libfoo")));

    Component wrong = make("libfoo:i386", "i386", MultiArch::Same);
    wrong.addProvidedCapability(nexus::Capability("libfoo"));

    Component right = make("libfoo:amd64", "amd64", MultiArch::Same);
    right.addProvidedCapability(nexus::Capability("libfoo"));

    Solver solver({app, wrong, right}, debianDetector());

    SolverRequest request;
    request.architecture = "amd64";
    request.requirements.push_back(Requirement(Constraint("app")));

    const auto result = solver.solve(request);

    ASSERT_EQ(result.status, SolverStatus::Success);

    bool sawCorrect = false;

    for (const std::string& id : result.selected) {
        EXPECT_NE(id, "libfoo:i386");

        if (id == "libfoo:amd64") {
            sawCorrect = true;
        }
    }

    EXPECT_TRUE(sawCorrect);
}

TEST(ArchitectureSolverTest, ForeignToolSatisfiesAcrossArchitectures) {
    Component app = make("app", "i386");
    app.addRequirement(Requirement(Constraint("helper")));

    Solver solver(
        {app, make("helper", "amd64", MultiArch::Foreign)},
        debianDetector()
    );

    SolverRequest request;
    request.architecture = "i386";
    request.requirements.push_back(Requirement(Constraint("app")));

    EXPECT_EQ(solver.solve(request).status, SolverStatus::Success);
}
