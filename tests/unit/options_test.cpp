#include <gtest/gtest.h>

#include <nexus/options.hpp>
#include <nexus/system/version.hpp>

using nexus::Capability;
using nexus::Component;
using nexus::ComponentType;
using nexus::ConflictDetector;
using nexus::Constraint;
using nexus::findOptions;
using nexus::Requirement;
using nexus::Solver;

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
    const std::vector<std::string>& provides = {},
    const std::vector<std::string>& needs = {}
) {
    Component component(id, id, "1.0", ComponentType::Application);

    component.addProvidedCapability(Capability(id));

    for (const std::string& capability : provides) {
        component.addProvidedCapability(Capability(capability));
    }

    for (const std::string& need : needs) {
        component.addRequirement(Requirement(Constraint(need)));
    }

    return component;
}

}

TEST(OptionsTest, ReportsNothingWhenNothingProvidesIt) {
    const std::vector<Component> universe{make("apt")};

    const auto report = findOptions(
        "editor", universe, universe,
        Solver(universe, detector()), detector());

    EXPECT_TRUE(report.options.empty());
    EXPECT_FALSE(report.anyInstalled());
}

TEST(OptionsTest, FindsEveryProvider) {
    const std::vector<Component> universe{
        make("mawk", {"awk"}),
        make("gawk", {"awk"}),
        make("original-awk", {"awk"})
    };

    const auto report = findOptions(
        "awk", universe, {},
        Solver(universe, detector()), detector());

    EXPECT_EQ(report.options.size(), 3u);
}

// The number people actually want when choosing between two things
// that do the same job.
TEST(OptionsTest, CostsEachOption) {
    const std::vector<Component> universe{
        make("light", {"awk"}),
        make("heavy", {"awk"}, {"libone", "libtwo"}),
        make("libone"),
        make("libtwo")
    };

    const auto report = findOptions(
        "awk", universe, {},
        Solver(universe, detector()), detector());

    ASSERT_EQ(report.options.size(), 2u);

    // Cheapest first among things not installed.
    EXPECT_EQ(report.options[0].component, "light");
    EXPECT_EQ(report.options[0].componentCount, 1u);
    EXPECT_EQ(report.options[1].component, "heavy");
    EXPECT_EQ(report.options[1].componentCount, 3u);
}

// What is already here should not be counted as a cost.
TEST(OptionsTest, CountsOnlyWhatWouldBeAdded) {
    const std::vector<Component> universe{
        make("heavy", {"awk"}, {"shared"}),
        make("shared")
    };

    const std::vector<Component> installed{make("shared")};

    const auto report = findOptions(
        "awk", universe, installed,
        Solver(universe, detector()), detector());

    ASSERT_EQ(report.options.size(), 1u);
    EXPECT_EQ(report.options[0].componentCount, 2u);
    EXPECT_EQ(report.options[0].wouldAdd, 1u);
}

TEST(OptionsTest, MarksWhatIsInstalled) {
    const std::vector<Component> universe{
        make("mawk", {"awk"}),
        make("gawk", {"awk"})
    };

    const std::vector<Component> installed{make("gawk", {"awk"})};

    const auto report = findOptions(
        "awk", universe, installed,
        Solver(universe, detector()), detector());

    ASSERT_EQ(report.options.size(), 2u);

    // Installed first: somebody choosing wants what they have at the
    // top.
    EXPECT_EQ(report.options[0].component, "gawk");
    EXPECT_TRUE(report.options[0].installed);
    EXPECT_FALSE(report.options[1].installed);
    EXPECT_TRUE(report.anyInstalled());
}

// An option that cannot work is still an option worth showing, with
// the reason. Hiding it would leave somebody wondering why the thing
// they expected is missing.
TEST(OptionsTest, ShowsUnworkableOptionsWithAReason) {
    const std::vector<Component> universe{
        make("works", {"awk"}),
        make("broken", {"awk"}, {"missing-library"})
    };

    const auto report = findOptions(
        "awk", universe, {},
        Solver(universe, detector()), detector());

    ASSERT_EQ(report.options.size(), 2u);

    EXPECT_EQ(report.options[0].component, "works");
    EXPECT_TRUE(report.options[0].workable);

    EXPECT_EQ(report.options[1].component, "broken");
    EXPECT_FALSE(report.options[1].workable);
    EXPECT_NE(
        report.options[1].blockedOn.find("missing-library"),
        std::string::npos
    );
}

// Worth knowing before choosing, not after.
TEST(OptionsTest, NamesWhatAnOptionWouldCollideWith) {
    Component candidate = make("exim4", {"mta"});
    candidate.addConflict(Constraint("postfix"));

    const std::vector<Component> universe{
        candidate,
        make("postfix", {"mta"})
    };

    const std::vector<Component> installed{make("postfix", {"mta"})};

    const auto report = findOptions(
        "mta", universe, installed,
        Solver(universe, detector()), detector());

    ASSERT_EQ(report.options.size(), 2u);

    for (const auto& option : report.options) {
        if (option.component == "exim4") {
            ASSERT_FALSE(option.conflictsWith.empty());
            EXPECT_EQ(option.conflictsWith[0], "postfix");
        }
    }
}

// Costing must resolve for the component, not for the capability --
// otherwise every option reports whatever the solver preferred, which
// is the question this exists to avoid answering.
TEST(OptionsTest, CostsTheOptionNotThePreferredProvider) {
    const std::vector<Component> universe{
        make("cheap", {"awk"}),
        make("expensive", {"awk"}, {"a", "b", "c"}),
        make("a"), make("b"), make("c")
    };

    const auto report = findOptions(
        "awk", universe, {},
        Solver(universe, detector()), detector());

    ASSERT_EQ(report.options.size(), 2u);

    for (const auto& option : report.options) {
        if (option.component == "expensive") {
            EXPECT_EQ(option.componentCount, 4u);
        }
    }
}

// Regression: ids are requalified when sources are merged, so the
// same component is "mawk" in the installed set and "mawk:amd64" in
// the universe. Matching by id finds nothing while the output stays
// plausible.
TEST(OptionsTest, MatchesInstalledAcrossRequalifiedIds) {
    Component fromUniverse(
        "mawk:amd64", "mawk", "1.0", ComponentType::Application);

    fromUniverse.setArchitecture("amd64");
    fromUniverse.addProvidedCapability(Capability("awk"));

    Component fromInstalled(
        "mawk", "mawk", "1.0", ComponentType::Application);

    fromInstalled.setArchitecture("amd64");
    fromInstalled.addProvidedCapability(Capability("awk"));

    const std::vector<Component> universe{fromUniverse};

    const auto report = findOptions(
        "awk", universe, {fromInstalled},
        Solver(universe, detector()), detector(), "amd64");

    ASSERT_EQ(report.options.size(), 1u);
    EXPECT_TRUE(report.options[0].installed);
    EXPECT_TRUE(report.anyInstalled());
}

// Two builds of one package are one choice. For an amd64 request the
// i386 build is only eligible because of multi-arch rules.
TEST(OptionsTest, CollapsesArchitectureDuplicates) {
    Component native(
        "mawk:amd64", "mawk", "1.0", ComponentType::Application);

    native.setArchitecture("amd64");
    native.addProvidedCapability(Capability("awk"));

    Component foreign(
        "mawk:i386", "mawk", "1.0", ComponentType::Application);

    foreign.setArchitecture("i386");
    foreign.setMultiArch(nexus::MultiArch::Foreign);
    foreign.addProvidedCapability(Capability("awk"));

    const std::vector<Component> universe{foreign, native};

    const auto report = findOptions(
        "awk", universe, {},
        Solver(universe, detector()), detector(), "amd64");

    ASSERT_EQ(report.options.size(), 1u);
    EXPECT_EQ(report.options[0].component, "mawk:amd64");
}

// Different packages are still different choices.
TEST(OptionsTest, DoesNotCollapseDistinctPackages) {
    const std::vector<Component> universe{
        make("mawk", {"awk"}),
        make("gawk", {"awk"})
    };

    const auto report = findOptions(
        "awk", universe, {},
        Solver(universe, detector()), detector(), "amd64");

    EXPECT_EQ(report.options.size(), 2u);
}

// Equal cost to add, so the smaller thing overall is the lighter
// choice. Alphabetical order put a 54-component option above a
// 42-component one.
TEST(OptionsTest, BreaksCostTiesByTotalSize) {
    const std::vector<Component> universe{
        make("zebra", {"mta"}),
        make("alpha", {"mta"}, {"one", "two"}),
        make("one"), make("two")
    };

    const auto report = findOptions(
        "mta", universe, {},
        Solver(universe, detector()), detector());

    ASSERT_EQ(report.options.size(), 2u);
    EXPECT_EQ(report.options[0].component, "zebra");
}
