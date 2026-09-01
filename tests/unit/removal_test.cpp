#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>

#include <nexus/removal.hpp>
#include <nexus/system/auto_installed.hpp>
#include <nexus/system/version.hpp>

using nexus::Capability;
using nexus::Component;
using nexus::ComponentType;
using nexus::ConflictDetector;
using nexus::Constraint;
using nexus::planRemoval;
using nexus::RemovalPlan;
using nexus::Requirement;

using nexus::system::readAutoInstalled;

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
    const std::vector<std::string>& needs = {}
) {
    Component component(id, id, "1.0", ComponentType::Application);

    component.addProvidedCapability(Capability(id));

    for (const std::string& need : needs) {
        component.addRequirement(Requirement(Constraint(need)));
    }

    return component;
}

bool listed(
    const std::vector<std::string>& items,
    const std::string& value
) {
    return std::find(items.begin(), items.end(), value) != items.end();
}

}

TEST(RemovalTest, ReportsSomethingNotInstalled) {
    const std::vector<Component> installed{make("apt")};

    const RemovalPlan plan = planRemoval(
        "nonexistent", installed, {"apt"}, debianDetector()
    );

    EXPECT_FALSE(plan.possible);
    EXPECT_NE(plan.reason.find("not installed"), std::string::npos);
}

TEST(RemovalTest, RemovesALeafOnItsOwn) {
    const std::vector<Component> installed{make("apt"), make("tree")};

    const RemovalPlan plan = planRemoval(
        "tree", installed, {"apt", "tree"}, debianDetector()
    );

    ASSERT_TRUE(plan.possible);
    EXPECT_EQ(plan.removed.size(), 1u);
    EXPECT_TRUE(plan.orphaned.empty());
}

// A dependency that nothing else wants goes with it.
TEST(RemovalTest, TakesAPrivateDependencyWithIt) {
    const std::vector<Component> installed{
        make("editor", {"libedit"}),
        make("libedit"),
        make("apt")
    };

    const RemovalPlan plan = planRemoval(
        "editor", installed, {"editor", "apt"}, debianDetector()
    );

    ASSERT_TRUE(plan.possible);
    EXPECT_TRUE(listed(plan.orphaned, "libedit"));
    EXPECT_EQ(plan.removed.size(), 2u);
}

// A dependency something else still wants stays.
TEST(RemovalTest, LeavesASharedDependencyAlone) {
    const std::vector<Component> installed{
        make("editor", {"libedit"}),
        make("shell", {"libedit"}),
        make("libedit")
    };

    const RemovalPlan plan = planRemoval(
        "editor",
        installed,
        {"editor", "shell"},
        debianDetector()
    );

    ASSERT_TRUE(plan.possible);
    EXPECT_FALSE(listed(plan.orphaned, "libedit"));
    EXPECT_EQ(plan.removed.size(), 1u);
}

TEST(RemovalTest, RefusesWhenSomethingStillNeedsIt) {
    const std::vector<Component> installed{
        make("desktop", {"libedit"}),
        make("libedit")
    };

    const RemovalPlan plan = planRemoval(
        "libedit", installed, {"desktop"}, debianDetector()
    );

    EXPECT_FALSE(plan.possible);
    EXPECT_TRUE(plan.removed.empty());
    EXPECT_TRUE(listed(plan.requiredBy, "desktop"));
}

TEST(RemovalTest, RemovalIsTransitive) {
    const std::vector<Component> installed{
        make("app", {"middle"}),
        make("middle", {"bottom"}),
        make("bottom")
    };

    const RemovalPlan plan = planRemoval(
        "app", installed, {"app"}, debianDetector()
    );

    ASSERT_TRUE(plan.possible);
    EXPECT_TRUE(listed(plan.orphaned, "middle"));
    EXPECT_TRUE(listed(plan.orphaned, "bottom"));
    EXPECT_EQ(plan.removed.size(), 3u);
}

// Regression. With a single solve, anything already orphaned dropped
// out too and was blamed on the removal -- which made every removal
// report the same collateral, whatever was being removed.
TEST(RemovalTest, DoesNotBlameAlreadyOrphanedComponents) {
    const std::vector<Component> installed{
        make("app"),
        make("other"),
        // Installed, but no root holds it up. It was already orphaned
        // before anyone asked to remove anything.
        make("stale-leftover")
    };

    const RemovalPlan plan = planRemoval(
        "app", installed, {"app", "other"}, debianDetector()
    );

    ASSERT_TRUE(plan.possible);
    EXPECT_FALSE(listed(plan.orphaned, "stale-leftover"));
    EXPECT_EQ(plan.removed.size(), 1u);
}

// Two different removals must not produce the same collateral.
TEST(RemovalTest, DifferentTargetsGiveDifferentAnswers) {
    const std::vector<Component> installed{
        make("editor", {"libedit"}),
        make("browser", {"libweb"}),
        make("libedit"),
        make("libweb")
    };

    const std::set<std::string> roots{"editor", "browser"};

    const RemovalPlan editor = planRemoval(
        "editor", installed, roots, debianDetector()
    );

    const RemovalPlan browser = planRemoval(
        "browser", installed, roots, debianDetector()
    );

    ASSERT_TRUE(editor.possible);
    ASSERT_TRUE(browser.possible);

    EXPECT_TRUE(listed(editor.orphaned, "libedit"));
    EXPECT_FALSE(listed(editor.orphaned, "libweb"));

    EXPECT_TRUE(listed(browser.orphaned, "libweb"));
    EXPECT_FALSE(listed(browser.orphaned, "libedit"));
}

TEST(AutoInstalledTest, ReadsMarkedPackages) {
    const auto path =
        std::filesystem::temp_directory_path() /
        ("nexus-states-" + std::to_string(::getpid()));

    {
        std::ofstream out(path);
        out << "Package: libfoo\n"
               "Architecture: amd64\n"
               "Auto-Installed: 1\n"
               "\n"
               "Package: libbar\n"
               "Architecture: i386\n"
               "Auto-Installed: 1\n"
               "\n"
               "Package: wanted\n"
               "Architecture: amd64\n"
               "Auto-Installed: 0\n";
    }

    const auto automatic = readAutoInstalled(path.string());

    EXPECT_EQ(automatic.size(), 2u);
    EXPECT_EQ(automatic.count("libfoo:amd64"), 1u);
    EXPECT_EQ(automatic.count("libbar:i386"), 1u);
    EXPECT_EQ(automatic.count("wanted:amd64"), 0u);

    std::error_code error;
    std::filesystem::remove(path, error);
}

// With no record of what was automatic, nothing may be assumed
// automatic -- an empty set means every package looks wanted, which
// is the safe direction to be wrong in.
TEST(AutoInstalledTest, MissingFileMeansNothingIsAutomatic) {
    EXPECT_TRUE(readAutoInstalled("/nonexistent/extended_states")
                    .empty());
}

// Scale test.
//
// planRemoval hands every root to the solver at once. On a real
// desktop that is thousands of top-level requirements, and the search
// then recurses once per requirement. This crashed with a stack
// overflow on a 2,987-package system while every other test passed,
// because the container the tests were written on had a third as
// many. The size has to be in the test, not in the machine.
TEST(RemovalTest, HandlesAThousandsOfRootsSystem) {
    constexpr int kRoots = 5000;

    std::vector<Component> installed;

    installed.reserve(kRoots + 2);

    installed.push_back(make("libcommon"));
    installed.push_back(make("libextra", {"libcommon"}));

    std::set<std::string> roots;

    for (int index = 0; index < kRoots; ++index) {
        const std::string name = "app-" + std::to_string(index);

        installed.push_back(make(name, {"libcommon", "libextra"}));
        roots.insert(name);
    }

    const RemovalPlan plan = planRemoval(
        "app-7", installed, roots, debianDetector()
    );

    // Everything else still wants the shared libraries, so only the
    // requested component goes.
    ASSERT_TRUE(plan.possible);
    EXPECT_EQ(plan.removed.size(), 1u);
    EXPECT_TRUE(plan.orphaned.empty());
}

// A chain far deeper than any call stack could hold.
//
// The search depth is the size of the resolved system, so it must not
// live on the stack at all. 20,000 is chosen to be comfortably past
// the point where a recursive implementation dies.
TEST(RemovalTest, HandlesADeepDependencyChain) {
    constexpr int kDepth = 20000;

    std::vector<Component> installed;

    installed.reserve(kDepth);

    for (int index = 0; index < kDepth; ++index) {
        const std::string name = "link-" + std::to_string(index);

        if (index + 1 < kDepth) {
            installed.push_back(
                make(name, {"link-" + std::to_string(index + 1)})
            );
        } else {
            installed.push_back(make(name));
        }
    }

    const RemovalPlan plan = planRemoval(
        "link-0", installed, {"link-0"}, debianDetector()
    );

    ASSERT_TRUE(plan.possible);
    EXPECT_EQ(plan.removed.size(), static_cast<std::size_t>(kDepth));
}

// Debian's Recommends means "you almost certainly want this", and apt
// keeps such packages. Following only hard requirements made 381
// deliberately-kept packages look abandoned on a machine where apt
// considered 4 removable.
TEST(RemovalTest, RecommendationsKeepAComponentAlive) {
    Component desktop = make("desktop");

    desktop.addRecommendedCapability(nexus::Capability("alsa-utils"));

    const std::vector<Component> installed{
        desktop,
        make("alsa-utils"),
        make("genuinely-orphaned")
    };

    const auto reachable = nexus::reachableFrom(
        installed, {"desktop"}, debianDetector());

    EXPECT_EQ(reachable.count("alsa-utils"), 1u);
    EXPECT_EQ(reachable.count("genuinely-orphaned"), 0u);
}

TEST(RemovalTest, ARecommendedComponentIsNotProposedForRemoval) {
    Component desktop = make("desktop");

    desktop.addRecommendedCapability(nexus::Capability("helper"));

    const std::vector<Component> installed{
        desktop,
        make("helper"),
        make("target", {"helper"})
    };

    const RemovalPlan plan = planRemoval(
        "target", installed, {"desktop", "target"}, debianDetector());

    ASSERT_TRUE(plan.possible);
    EXPECT_TRUE(plan.orphaned.empty());
}

// Built from the exact shape that exposed it: wine recommends
// "libodbc2 | libodbc1", the machine has libodbc1, and following only
// the first alternative made it look unwanted.
TEST(RemovalTest, ARecommendationsAlternativesAreAllFollowed) {
    Component wine = make("wine");

    wine.addRecommendation(nexus::Requirement(
        std::vector<nexus::Constraint>{
            nexus::Constraint("libodbc2"),
            nexus::Constraint("libodbc1")
        }
    ));

    // Only the second alternative is installed.
    const std::vector<Component> installed{
        wine,
        make("libodbc1")
    };

    const auto reachable = nexus::reachableFrom(
        installed, {"wine"}, debianDetector());

    EXPECT_EQ(reachable.count("libodbc1"), 1u);
}

TEST(RemovalTest, TheFlatViewStillReportsTheFirstAlternative) {
    Component wine = make("wine");

    wine.addRecommendation(nexus::Requirement(
        std::vector<nexus::Constraint>{
            nexus::Constraint("libodbc2"),
            nexus::Constraint("libodbc1")
        }
    ));

    ASSERT_EQ(wine.recommendedCapabilities().size(), 1u);
    EXPECT_EQ(wine.recommendedCapabilities()[0].name(), "libodbc2");
    EXPECT_EQ(wine.recommendations()[0].alternatives.size(), 2u);
}
