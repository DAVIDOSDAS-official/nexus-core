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
using nexus::Solver;
using nexus::system::readAutoInstalled;

namespace {

Solver solverOver(std::vector<Component> components) {
    return Solver(
        std::move(components),
        ConflictDetector(
            [](const std::string& left, const std::string& right) {
                return nexus::system::compareVersions(left, right);
            }
        )
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
        "nonexistent", installed, {"apt"}, solverOver(installed)
    );

    EXPECT_FALSE(plan.possible);
    EXPECT_NE(plan.reason.find("not installed"), std::string::npos);
}

TEST(RemovalTest, RemovesALeafOnItsOwn) {
    const std::vector<Component> installed{make("apt"), make("tree")};

    const RemovalPlan plan = planRemoval(
        "tree", installed, {"apt", "tree"}, solverOver(installed)
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
        "editor", installed, {"editor", "apt"}, solverOver(installed)
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
        solverOver(installed)
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
        "libedit", installed, {"desktop"}, solverOver(installed)
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
        "app", installed, {"app"}, solverOver(installed)
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
        "app", installed, {"app", "other"}, solverOver(installed)
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
        "editor", installed, roots, solverOver(installed)
    );

    const RemovalPlan browser = planRemoval(
        "browser", installed, roots, solverOver(installed)
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
