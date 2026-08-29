#include <gtest/gtest.h>

#include <nexus/architecture.hpp>
#include <nexus/solver.hpp>
#include <sstream>

#include <nexus/system/control_file.hpp>
#include <nexus/system/dpkg_source.hpp>
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

// Regression: an Architecture: all package used to pass "all" down as
// the requesting architecture, so its own dependencies could never be
// satisfied by any real architecture. On a multi-arch system this made
// most solves fail.
TEST(ArchitectureSolverTest, ArchIndependentPassesDownNativeArch) {
    Component app = make("app", "amd64");
    app.addRequirement(Requirement(Constraint("config-tool")));

    // Architecture: all, but it needs a real native library.
    Component tool = make("config-tool", kArchitectureAll);
    tool.addRequirement(Requirement(Constraint("perl-base")));

    Component perl = make("perl-base", "amd64");

    Solver solver({app, tool, perl}, debianDetector());

    SolverRequest request;
    request.architecture = "amd64";
    request.requirements.push_back(Requirement(Constraint("app")));

    const auto result = solver.solve(request);

    EXPECT_EQ(result.status, SolverStatus::Success);
    EXPECT_EQ(result.backtracks, 0u);
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


// End-to-end on a multi-arch system: real status data, through the
// dpkg source, into the solver. This is the path that failed on a
// machine with i386 enabled while an amd64-only machine looked fine.
TEST(ArchitectureIntegrationTest, SolvesOnAMultiArchSystem) {
    std::istringstream input(
        "Package: apt\n"
        "Status: install ok installed\n"
        "Architecture: amd64\n"
        "Version: 2.8.3\n"
        "Depends: debconf, libc6 (>= 2.38)\n"
        "\n"
        "Package: debconf\n"
        "Status: install ok installed\n"
        "Architecture: all\n"
        "Multi-Arch: foreign\n"
        "Version: 1.5.86\n"
        "Depends: perl-base (>= 5.20.1-3~)\n"
        "\n"
        "Package: perl-base\n"
        "Status: install ok installed\n"
        "Architecture: amd64\n"
        "Multi-Arch: allowed\n"
        "Version: 5.38.2-3.2\n"
        "\n"
        "Package: libc6\n"
        "Status: install ok installed\n"
        "Architecture: amd64\n"
        "Multi-Arch: same\n"
        "Version: 2.39\n"
        "\n"
        "Package: libc6\n"
        "Status: install ok installed\n"
        "Architecture: i386\n"
        "Multi-Arch: same\n"
        "Version: 2.39\n"
    );

    const nexus::system::DpkgSource source;
    const auto loaded = source.loadFromStanzas(
        nexus::system::parseControlStream(input)
    );

    ASSERT_EQ(loaded.components.size(), 5u);

    Solver solver(loaded.components, debianDetector());

    SolverRequest request;
    request.architecture = "amd64";
    request.requirements.push_back(Requirement(Constraint("apt")));

    const auto result = solver.solve(request);

    ASSERT_EQ(result.status, SolverStatus::Success);

    bool sawAmd64 = false;

    for (const std::string& id : result.selected) {
        // The 32-bit libc must never be chosen for an amd64 solve.
        EXPECT_NE(id, "libc6:i386");

        if (id == "libc6:amd64") {
            sawAmd64 = true;
        }
    }

    EXPECT_TRUE(sawAmd64);
}

TEST(ArchitectureIntegrationTest, SolvingForI386PicksThe32BitLibrary) {
    std::istringstream input(
        "Package: game\n"
        "Status: install ok installed\n"
        "Architecture: i386\n"
        "Version: 1.0\n"
        "Depends: libc6 (>= 2.38)\n"
        "\n"
        "Package: libc6\n"
        "Status: install ok installed\n"
        "Architecture: amd64\n"
        "Multi-Arch: same\n"
        "Version: 2.39\n"
        "\n"
        "Package: libc6\n"
        "Status: install ok installed\n"
        "Architecture: i386\n"
        "Multi-Arch: same\n"
        "Version: 2.39\n"
    );

    const nexus::system::DpkgSource source;
    const auto loaded = source.loadFromStanzas(
        nexus::system::parseControlStream(input)
    );

    Solver solver(loaded.components, debianDetector());

    SolverRequest request;
    request.architecture = "i386";
    request.requirements.push_back(Requirement(Constraint("game")));

    const auto result = solver.solve(request);

    ASSERT_EQ(result.status, SolverStatus::Success);

    bool saw32 = false;

    for (const std::string& id : result.selected) {
        EXPECT_NE(id, "libc6:amd64");

        if (id == "libc6:i386") {
            saw32 = true;
        }
    }

    EXPECT_TRUE(saw32);
}

// Regression: the solver used to track selections by component id.
// Two builds of the same package share a name, so once the 64-bit one
// was chosen the 32-bit one looked already satisfied and was silently
// dropped -- exactly the failure multi-arch exists to prevent.
TEST(ArchitectureIntegrationTest, BothArchitecturesCanBeSelected) {
    std::istringstream input(
        "Package: game\n"
        "Status: install ok installed\n"
        "Architecture: amd64\n"
        "Version: 1.0\n"
        "Depends: libc6, libc6:i386\n"
        "\n"
        "Package: libc6\n"
        "Status: install ok installed\n"
        "Architecture: amd64\n"
        "Multi-Arch: same\n"
        "Version: 2.39\n"
        "\n"
        "Package: libc6\n"
        "Status: install ok installed\n"
        "Architecture: i386\n"
        "Multi-Arch: same\n"
        "Version: 2.39\n"
    );

    const nexus::system::DpkgSource source;
    const auto loaded = source.loadFromStanzas(
        nexus::system::parseControlStream(input)
    );

    Solver solver(loaded.components, debianDetector());

    SolverRequest request;
    request.architecture = "amd64";
    request.requirements.push_back(Requirement(Constraint("game")));

    const auto result = solver.solve(request);

    ASSERT_EQ(result.status, SolverStatus::Success);
    ASSERT_EQ(result.selected.size(), 3u);

    bool saw64 = false;
    bool saw32 = false;

    for (const std::string& id : result.selected) {
        saw64 = saw64 || id == "libc6:amd64";
        saw32 = saw32 || id == "libc6:i386";
    }

    EXPECT_TRUE(saw64);
    EXPECT_TRUE(saw32);
}
