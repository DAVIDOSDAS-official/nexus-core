#include <gtest/gtest.h>

#include <algorithm>

#include <nexus/solver.hpp>
#include <nexus/system/version.hpp>

using nexus::Capability;
using nexus::Component;
using nexus::ComponentType;
using nexus::ConflictDetector;
using nexus::Constraint;
using nexus::Requirement;
using nexus::Solver;
using nexus::SolverRequest;
using nexus::SolverStatus;
using nexus::VersionConstraint;
using nexus::VersionRelation;

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
    const std::string& version = "1.0"
) {
    return Component(id, id, version, ComponentType::Application);
}

bool contains(
    const std::vector<std::string>& items,
    const std::string& value
) {
    return std::find(items.begin(), items.end(), value) != items.end();
}

SolverRequest wanting(const std::string& capability) {
    SolverRequest request;
    request.requirements.push_back(
        Requirement(Constraint(capability))
    );
    return request;
}

}

TEST(SolverTest, SolvesASingleRequirement) {
    Solver solver({make("pipewire")}, debianDetector());

    const auto result = solver.solve(wanting("pipewire"));

    EXPECT_EQ(result.status, SolverStatus::Success);
    EXPECT_TRUE(contains(result.selected, "pipewire"));
}

TEST(SolverTest, ReportsWhatItCouldNotSatisfy) {
    Solver solver({make("pipewire")}, debianDetector());

    const auto result = solver.solve(wanting("bluetooth"));

    EXPECT_EQ(result.status, SolverStatus::Unsatisfiable);
    EXPECT_NE(result.blockedOn.find("bluetooth"), std::string::npos);
}

TEST(SolverTest, FollowsTransitiveRequirements) {
    Component desktop = make("kde");
    desktop.addRequirement(Requirement(Constraint("audio")));

    Component pipewire = make("pipewire");
    pipewire.addProvidedCapability(Capability("audio"));

    Solver solver({desktop, pipewire}, debianDetector());

    const auto result = solver.solve(wanting("kde"));

    EXPECT_EQ(result.status, SolverStatus::Success);
    EXPECT_TRUE(contains(result.selected, "pipewire"));
}

TEST(SolverTest, TakesTheFirstAlternativeWhenItWorks) {
    Component app = make("app");
    app.addRequirement(Requirement(std::vector<Constraint>{
        Constraint("base-passwd"),
        Constraint("adduser")
    }));

    Solver solver(
        {app, make("base-passwd"), make("adduser")},
        debianDetector()
    );

    const auto result = solver.solve(wanting("app"));

    EXPECT_EQ(result.status, SolverStatus::Success);
    EXPECT_TRUE(contains(result.selected, "base-passwd"));
    EXPECT_FALSE(contains(result.selected, "adduser"));
    EXPECT_EQ(result.backtracks, 0u);
}

TEST(SolverTest, FallsBackToTheSecondAlternative) {
    Component app = make("app");
    app.addRequirement(Requirement(std::vector<Constraint>{
        Constraint("base-passwd"),
        Constraint("adduser")
    }));

    // Only the second alternative exists.
    Solver solver({app, make("adduser")}, debianDetector());

    const auto result = solver.solve(wanting("app"));

    EXPECT_EQ(result.status, SolverStatus::Success);
    EXPECT_TRUE(contains(result.selected, "adduser"));
}

// The heart of the matter: a choice that looks fine turns out to be
// wrong two levels down, and the solver has to undo it.
TEST(SolverTest, BacktracksWhenAChoiceFailsLater) {
    Component app = make("app");
    app.addRequirement(Requirement(std::vector<Constraint>{
        Constraint("backend-a"),
        Constraint("backend-b")
    }));

    // backend-a needs something that does not exist.
    Component backendA = make("backend-a");
    backendA.addRequirement(Requirement(Constraint("missing-library")));

    // backend-b needs something that does.
    Component backendB = make("backend-b");
    backendB.addRequirement(Requirement(Constraint("present-library")));

    Solver solver(
        {app, backendA, backendB, make("present-library")},
        debianDetector()
    );

    const auto result = solver.solve(wanting("app"));

    EXPECT_EQ(result.status, SolverStatus::Success);
    EXPECT_TRUE(contains(result.selected, "backend-b"));
    EXPECT_FALSE(contains(result.selected, "backend-a"));
    EXPECT_GT(result.backtracks, 0u);
}

TEST(SolverTest, BacktracksOutOfAConflict) {
    Component app = make("app");
    app.addRequirement(Requirement(std::vector<Constraint>{
        Constraint("mta-one"),
        Constraint("mta-two")
    }));
    app.addRequirement(Requirement(Constraint("mailer")));

    Component mailer = make("mailer");
    mailer.addConflict(Constraint("mta-one"));

    Solver solver(
        {app, make("mta-one"), make("mta-two"), mailer},
        debianDetector()
    );

    const auto result = solver.solve(wanting("app"));

    EXPECT_EQ(result.status, SolverStatus::Success);
    EXPECT_TRUE(contains(result.selected, "mta-two"));
    EXPECT_FALSE(contains(result.selected, "mta-one"));
}

TEST(SolverTest, RespectsVersionConstraints) {
    Component app = make("app");
    app.addRequirement(Requirement(Constraint(
        "libc",
        VersionConstraint{VersionRelation::LaterOrEqual, "2.38"}
    )));

    Solver solver(
        {app, make("libc", "2.37")},
        debianDetector()
    );

    EXPECT_EQ(
        solver.solve(wanting("app")).status,
        SolverStatus::Unsatisfiable
    );

    Solver better(
        {app, make("libc", "2.39")},
        debianDetector()
    );

    EXPECT_EQ(
        better.solve(wanting("app")).status,
        SolverStatus::Success
    );
}

TEST(SolverTest, PreferenceBreaksATie) {
    Component wayland = make("wayland");
    wayland.addProvidedCapability(Capability("display"));

    Component xorg = make("xorg");
    xorg.addProvidedCapability(Capability("display"));

    Solver solver({wayland, xorg}, debianDetector());

    SolverRequest request = wanting("display");
    request.preferred["display"] = "xorg";

    const auto result = solver.solve(request);

    EXPECT_EQ(result.status, SolverStatus::Success);
    EXPECT_TRUE(contains(result.selected, "xorg"));
}

// A preference is a tie-breaker. A requirement is absolute: if it
// cannot be honoured, the solve fails rather than quietly using
// something else.
TEST(SolverTest, RequiredProviderIsNeverSilentlyReplaced) {
    Component wayland = make("wayland");
    wayland.addProvidedCapability(Capability("display"));

    Component xorg = make("xorg");
    xorg.addProvidedCapability(Capability("display"));

    Solver solver({wayland, xorg}, debianDetector());

    SolverRequest request = wanting("display");
    request.required["display"] = "mir";

    const auto result = solver.solve(request);

    EXPECT_EQ(result.status, SolverStatus::Unsatisfiable);
    EXPECT_FALSE(contains(result.selected, "wayland"));
    EXPECT_FALSE(contains(result.selected, "xorg"));
}

TEST(SolverTest, HandlesCyclesWithoutLooping) {
    Component alpha = make("alpha");
    alpha.addRequirement(Requirement(Constraint("beta")));

    Component beta = make("beta");
    beta.addRequirement(Requirement(Constraint("alpha")));

    Solver solver({alpha, beta}, debianDetector());

    const auto result = solver.solve(wanting("alpha"));

    EXPECT_EQ(result.status, SolverStatus::Success);
    EXPECT_TRUE(contains(result.selected, "alpha"));
    EXPECT_TRUE(contains(result.selected, "beta"));
}

TEST(SolverTest, RecordsAnExplanationForEveryChoice) {
    Component desktop = make("kde");
    desktop.addRequirement(Requirement(Constraint("audio")));

    Component pipewire = make("pipewire");
    pipewire.addProvidedCapability(Capability("audio"));

    Solver solver({desktop, pipewire}, debianDetector());

    const auto result = solver.solve(wanting("kde"));

    ASSERT_EQ(result.steps.size(), result.selected.size());

    for (const auto& step : result.steps) {
        EXPECT_FALSE(step.selected.empty());
        EXPECT_FALSE(step.reason.empty());
        EXPECT_FALSE(step.requirement.empty());
    }
}

TEST(SolverTest, ExplanationMarksABacktrackedChoice) {
    Component app = make("app");
    app.addRequirement(Requirement(std::vector<Constraint>{
        Constraint("backend-a"),
        Constraint("backend-b")
    }));

    Component backendA = make("backend-a");
    backendA.addRequirement(Requirement(Constraint("missing")));

    Solver solver(
        {app, backendA, make("backend-b")},
        debianDetector()
    );

    const auto result = solver.solve(wanting("app"));

    ASSERT_EQ(result.status, SolverStatus::Success);

    bool sawBacktrack = false;

    for (const auto& step : result.steps) {
        if (step.wasBacktrackedInto) {
            sawBacktrack = true;
            EXPECT_EQ(step.selected, "backend-b");
        }
    }

    EXPECT_TRUE(sawBacktrack);
}

TEST(SolverTest, ReportsHittingTheDecisionLimit) {
    Component app = make("app");
    app.addRequirement(Requirement(Constraint("missing")));

    Solver solver({app}, debianDetector());
    solver.setDecisionLimit(1);

    const auto result = solver.solve(wanting("app"));

    // Either it ran out of options or it ran out of budget; both are
    // reported, neither is silently treated as success.
    EXPECT_NE(result.status, SolverStatus::Success);
}
