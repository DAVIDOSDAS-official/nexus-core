#include <gtest/gtest.h>

#include <algorithm>

#include <nexus/system/version.hpp>
#include <nexus/transaction.hpp>

using nexus::Capability;
using nexus::Component;
using nexus::ComponentType;
using nexus::ConflictDetector;
using nexus::Constraint;
using nexus::planTransaction;
using nexus::Requirement;
using nexus::TransactionStatus;

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
    const std::vector<std::string>& needs = {},
    const std::vector<std::string>& preNeeds = {}
) {
    Component component(id, id, "1.0", ComponentType::Application);

    component.addProvidedCapability(Capability(id));

    for (const std::string& need : needs) {
        component.addRequirement(Requirement(Constraint(need)));
    }

    for (const std::string& need : preNeeds) {
        Requirement requirement{Constraint(need)};

        requirement.pre = true;

        component.addRequirement(std::move(requirement));
    }

    return component;
}

std::size_t indexOf(
    const nexus::TransactionPlan& plan,
    const std::string& id
) {
    for (std::size_t index = 0; index < plan.steps.size(); ++index) {
        if (plan.steps[index].component == id) {
            return index;
        }
    }

    return plan.steps.size();
}

}

TEST(TransactionTest, OrdersADependencyBeforeItsDependent) {
    const std::vector<Component> universe{
        make("app", {"lib"}),
        make("lib")
    };

    const auto plan =
        planTransaction({"app", "lib"}, universe, detector());

    ASSERT_TRUE(plan.ready());
    ASSERT_EQ(plan.steps.size(), 2u);
    EXPECT_LT(indexOf(plan, "lib"), indexOf(plan, "app"));
}

TEST(TransactionTest, OrdersAChain) {
    const std::vector<Component> universe{
        make("top", {"middle"}),
        make("middle", {"bottom"}),
        make("bottom")
    };

    const auto plan = planTransaction(
        {"top", "middle", "bottom"}, universe, detector());

    ASSERT_TRUE(plan.ready());
    EXPECT_LT(indexOf(plan, "bottom"), indexOf(plan, "middle"));
    EXPECT_LT(indexOf(plan, "middle"), indexOf(plan, "top"));
    EXPECT_TRUE(plan.cycles.empty());
}

// Debian's archive genuinely contains these, and dpkg breaks them by
// unpacking every member before configuring any of them.
TEST(TransactionTest, BreaksACycleAndNamesIt) {
    const std::vector<Component> universe{
        make("libc6", {"libgcc-s1"}),
        make("libgcc-s1", {"libc6"})
    };

    const auto plan = planTransaction(
        {"libc6", "libgcc-s1"}, universe, detector());

    ASSERT_TRUE(plan.ready());
    ASSERT_EQ(plan.cycles.size(), 1u);
    EXPECT_EQ(plan.cycles[0].members.size(), 2u);
    EXPECT_FALSE(plan.cycles[0].containsPreDependency);

    for (const auto& step : plan.steps) {
        EXPECT_TRUE(step.inCycle);
    }
}

// A cycle containing a pre-dependency cannot be broken that way: a
// pre-dependency must be configured before its dependent is unpacked.
TEST(TransactionTest, RefusesACycleContainingAPreDependency) {
    const std::vector<Component> universe{
        make("alpha", {}, {"beta"}),
        make("beta", {"alpha"})
    };

    const auto plan =
        planTransaction({"alpha", "beta"}, universe, detector());

    EXPECT_FALSE(plan.ready());
    EXPECT_EQ(plan.status, TransactionStatus::Blocked);
    ASSERT_EQ(plan.cycles.size(), 1u);
    EXPECT_TRUE(plan.cycles[0].containsPreDependency);
    EXPECT_NE(
        plan.reason.find("pre-dependency"),
        std::string::npos
    );
}

// Regression: grouping leftovers by everything transitively connected
// finds weak connectivity, which reported an unrelated blob of 33
// components as one cycle. A cycle is a strongly connected component.
TEST(TransactionTest, DoesNotReportUnrelatedComponentsAsACycle) {
    const std::vector<Component> universe{
        // A real two-member loop.
        make("libc6", {"libgcc-s1"}),
        make("libgcc-s1", {"libc6"}),
        // These merely depend on the loop. They are not in it.
        make("apt", {"libc6"}),
        make("dpkg", {"libc6"}),
        make("tar", {"libc6"})
    };

    const auto plan = planTransaction(
        {"libc6", "libgcc-s1", "apt", "dpkg", "tar"},
        universe,
        detector()
    );

    ASSERT_TRUE(plan.ready());
    ASSERT_EQ(plan.cycles.size(), 1u);
    EXPECT_EQ(plan.cycles[0].members.size(), 2u);

    const auto& members = plan.cycles[0].members;

    EXPECT_EQ(
        std::count(members.begin(), members.end(), "apt"), 0);
}

TEST(TransactionTest, DependentsOfACycleComeAfterIt) {
    const std::vector<Component> universe{
        make("libc6", {"libgcc-s1"}),
        make("libgcc-s1", {"libc6"}),
        make("apt", {"libc6"})
    };

    const auto plan = planTransaction(
        {"libc6", "libgcc-s1", "apt"}, universe, detector());

    ASSERT_TRUE(plan.ready());
    EXPECT_LT(indexOf(plan, "libc6"), indexOf(plan, "apt"));
    EXPECT_LT(indexOf(plan, "libgcc-s1"), indexOf(plan, "apt"));
}

TEST(TransactionTest, OrderIsDeterministic) {
    const std::vector<Component> universe{
        make("app", {"one", "two"}),
        make("one"),
        make("two")
    };

    const auto first = planTransaction(
        {"app", "one", "two"}, universe, detector());
    const auto second = planTransaction(
        {"two", "one", "app"}, universe, detector());

    ASSERT_TRUE(first.ready());
    ASSERT_TRUE(second.ready());
    ASSERT_EQ(first.steps.size(), second.steps.size());

    for (std::size_t index = 0; index < first.steps.size(); ++index) {
        EXPECT_EQ(
            first.steps[index].component,
            second.steps[index].component
        );
    }
}

TEST(TransactionTest, ReportsAnUnknownComponent) {
    const std::vector<Component> universe{make("app")};

    const auto plan =
        planTransaction({"app", "ghost"}, universe, detector());

    EXPECT_FALSE(plan.ready());
    EXPECT_FALSE(plan.reason.empty());
}

TEST(TransactionTest, HandlesALargeTransaction) {
    constexpr int kSize = 5000;

    std::vector<Component> universe;
    std::vector<std::string> selected;

    universe.reserve(kSize);
    selected.reserve(kSize);

    for (int index = 0; index < kSize; ++index) {
        const std::string name = "link-" + std::to_string(index);

        if (index + 1 < kSize) {
            universe.push_back(
                make(name, {"link-" + std::to_string(index + 1)})
            );
        } else {
            universe.push_back(make(name));
        }

        selected.push_back(name);
    }

    const auto plan =
        planTransaction(selected, universe, detector());

    ASSERT_TRUE(plan.ready());
    EXPECT_EQ(plan.steps.size(), static_cast<std::size_t>(kSize));
    EXPECT_EQ(plan.steps.front().component,
              "link-" + std::to_string(kSize - 1));
}
