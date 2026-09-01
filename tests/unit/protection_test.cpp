#include <gtest/gtest.h>

#include <nexus/removal.hpp>
#include <nexus/system/protection.hpp>
#include <nexus/system/version.hpp>

using nexus::Component;
using nexus::ComponentType;
using nexus::ConflictDetector;
using nexus::system::parseProtectionRules;
using nexus::system::protectedComponents;
using nexus::system::ProtectionRules;

namespace {

// Copied from a real /etc/apt/apt.conf.d/01autoremove rather than
// invented, because the shape of the file is the thing being parsed.
const char* kRealConfig = R"CONF(
APT
{
  NeverAutoRemove
  {
	"^firmware-linux.*";
	"^linux-firmware$";
	"^linux-image-[a-z0-9]*$";
	"^linux-image-[a-z0-9]*-[a-z0-9]*$";
  };
  VersionedKernelPackages
  {
	# kernels
	"linux-.*";
	"kfreebsd-.*";
	# (out-of-tree) modules
	".*-modules";
	".*-kernel";
  };
  Never-MarkAuto-Sections
  {
	"metapackages";
	"universe/metapackages";
  };
};
)CONF";

ConflictDetector detector() {
    return ConflictDetector(
        [](const std::string& left, const std::string& right) {
            return nexus::system::compareVersions(left, right);
        }
    );
}

Component kernel(
    const std::string& name,
    const std::string& version
) {
    Component component(name, name, version, ComponentType::Kernel);

    component.addProvidedCapability(nexus::Capability(name));

    return component;
}

}

TEST(ProtectionTest, ParsesTheRealConfiguration) {
    const ProtectionRules rules = parseProtectionRules(kRealConfig);

    EXPECT_EQ(rules.neverRemove.size(), 4u);
    EXPECT_EQ(rules.kernelPatterns.size(), 4u);
    EXPECT_EQ(rules.neverAutoSections.size(), 2u);
    EXPECT_FALSE(rules.empty());
}

TEST(ProtectionTest, AnEmptyConfigurationYieldsNoRules) {
    EXPECT_TRUE(parseProtectionRules("").empty());
    EXPECT_TRUE(parseProtectionRules("APT { };").empty());
}

// The worst thing this tool could ever be wrong about.
TEST(ProtectionTest, TheRunningKernelIsProtected) {
    const std::vector<Component> installed{
        kernel("linux-image-7.1.1-76070101-generic", "7.1.1"),
        kernel("linux-image-7.0.9-76070009-generic", "7.0.9")
    };

    const auto guarded = protectedComponents(
        installed,
        parseProtectionRules(kRealConfig),
        "7.1.1-76070101-generic"
    );

    EXPECT_EQ(guarded.count("linux-image-7.1.1-76070101-generic"), 1u);
}

// Removing the newest leaves no fallback if the running one turns out
// to be broken, which is why apt keeps one more than the graph needs.
TEST(ProtectionTest, TheNewestKernelIsProtected) {
    const std::vector<Component> installed{
        kernel("linux-image-7.0.9-76070009-generic", "7.0.9"),
        kernel("linux-image-7.0.11-76070011-generic", "7.0.11")
    };

    // Running something else entirely.
    const auto guarded = protectedComponents(
        installed, parseProtectionRules(kRealConfig), "6.0.0-generic");

    EXPECT_EQ(guarded.count("linux-image-7.0.11-76070011-generic"), 1u);
    EXPECT_EQ(guarded.count("linux-image-7.0.9-76070009-generic"), 0u);
}

TEST(ProtectionTest, MatchesNeverAutoRemovePatterns) {
    const std::vector<Component> installed{
        kernel("linux-firmware", "1.0"),
        kernel("firmware-linux-nonfree", "1.0"),
        kernel("ordinary-package", "1.0")
    };

    const auto guarded = protectedComponents(
        installed, parseProtectionRules(kRealConfig), "");

    EXPECT_EQ(guarded.count("linux-firmware"), 1u);
    EXPECT_EQ(guarded.count("firmware-linux-nonfree"), 1u);
    EXPECT_EQ(guarded.count("ordinary-package"), 0u);
}

TEST(ProtectionTest, NoRulesProtectNothing) {
    const std::vector<Component> installed{
        kernel("linux-image-7.1.1-generic", "7.1.1")
    };

    EXPECT_TRUE(
        protectedComponents(installed, ProtectionRules{}, "7.1.1")
            .empty()
    );
}

// The graph may well say nothing needs the running kernel. Acting on
// that leaves a machine that does not boot.
TEST(ProtectionTest, RemovalRefusesAProtectedComponent) {
    const std::vector<Component> installed{
        kernel("linux-image-7.1.1-generic", "7.1.1"),
        kernel("other", "1.0")
    };

    const auto plan = nexus::planRemoval(
        "linux-image-7.1.1-generic",
        installed,
        {"other"},
        detector(),
        "",
        {"linux-image-7.1.1-generic"}
    );

    EXPECT_FALSE(plan.possible);
    EXPECT_TRUE(plan.removed.empty());
    EXPECT_NE(plan.reason.find("protected"), std::string::npos);
}

TEST(ProtectionTest, ProtectedComponentsAreNotCarriedOutAsCollateral) {
    Component app = kernel("app", "1.0");

    app.addRequirement(nexus::Requirement(
        nexus::Constraint("linux-firmware")));

    const std::vector<Component> installed{
        app,
        kernel("linux-firmware", "1.0")
    };

    const auto plan = nexus::planRemoval(
        "app", installed, {"app"}, detector(), "",
        {"linux-firmware"});

    ASSERT_TRUE(plan.possible);
    EXPECT_TRUE(plan.orphaned.empty());
}
