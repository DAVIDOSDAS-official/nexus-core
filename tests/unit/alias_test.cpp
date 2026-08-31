#include <gtest/gtest.h>

#include <sstream>

#include <nexus/alias.hpp>
#include <nexus/profile_check.hpp>
#include <nexus/system/alias_file.hpp>
#include <nexus/system/version.hpp>

using nexus::AliasTable;
using nexus::Capability;
using nexus::Component;
using nexus::ComponentType;
using nexus::ConflictDetector;
using nexus::Constraint;
using nexus::Profile;
using nexus::Requirement;
using nexus::Solver;
using nexus::system::parseAliasStream;

namespace {

AliasTable parse(const std::string& text) {
    std::istringstream input(text);
    return parseAliasStream(input).table;
}

Component make(
    const std::string& id,
    const std::vector<std::string>& provides = {}
) {
    Component component(id, id, "1.0", ComponentType::Application);

    component.addProvidedCapability(Capability(id));

    for (const std::string& capability : provides) {
        component.addProvidedCapability(Capability(capability));
    }

    return component;
}

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

std::vector<std::string> namesOf(const Requirement& requirement) {
    std::vector<std::string> names;

    for (const Constraint& option : requirement.alternatives) {
        names.push_back(option.capability);
    }

    return names;
}

}

TEST(AliasTest, ParsesAnAliasFile) {
    const AliasTable table = parse(
        "Capability: web-browser\n"
        "Resolves-To: firefox | chromium\n"
        "\n"
        "Capability: firewall\n"
        "Resolves-To: ufw | nftables\n"
    );

    EXPECT_EQ(table.size(), 2u);
    EXPECT_TRUE(table.knows("web-browser"));
    EXPECT_FALSE(table.knows("nonsense"));
}

TEST(AliasTest, ReportsAnAliasWithNoTargets) {
    std::istringstream input("Capability: empty\n");

    EXPECT_FALSE(parseAliasStream(input).problems.empty());
}

// Expansion is additive, and the original comes first, so a real
// capability by that name always beats the translation. On Debian
// "mail-transport-agent" is a genuine virtual package; on Fedora it
// is not, and falls through.
TEST(AliasTest, KeepsTheOriginalNameFirst) {
    const AliasTable table = parse(
        "Capability: web-browser\n"
        "Resolves-To: firefox | chromium\n"
    );

    const Requirement expanded =
        table.expand(Requirement(Constraint("web-browser")));

    EXPECT_EQ(
        namesOf(expanded),
        (std::vector<std::string>{"web-browser", "firefox", "chromium"})
    );
}

TEST(AliasTest, LeavesUnknownCapabilitiesAlone) {
    const AliasTable table = parse(
        "Capability: web-browser\nResolves-To: firefox\n");

    const Requirement expanded =
        table.expand(Requirement(Constraint("gamemode")));

    EXPECT_EQ(namesOf(expanded), (std::vector<std::string>{"gamemode"}));
}

TEST(AliasTest, ExpandsEveryAlternative) {
    const AliasTable table = parse(
        "Capability: a\nResolves-To: a1\n"
        "\nCapability: b\nResolves-To: b1\n");

    Requirement requirement(std::vector<Constraint>{
        Constraint("a"), Constraint("b")
    });

    EXPECT_EQ(
        namesOf(table.expand(requirement)),
        (std::vector<std::string>{"a", "a1", "b", "b1"})
    );
}

TEST(AliasTest, DoesNotRepeatAName) {
    const AliasTable table = parse(
        "Capability: a\nResolves-To: shared | other\n"
        "\nCapability: b\nResolves-To: shared\n");

    Requirement requirement(std::vector<Constraint>{
        Constraint("a"), Constraint("b")
    });

    const auto names = namesOf(table.expand(requirement));

    EXPECT_EQ(
        std::count(names.begin(), names.end(), "shared"),
        1
    );
}

TEST(AliasTest, PreservesPreDependency) {
    const AliasTable table = parse(
        "Capability: a\nResolves-To: a1\n");

    Requirement requirement(Constraint("a"));

    requirement.pre = true;

    EXPECT_TRUE(table.expand(requirement).pre);
}

// The point of the whole thing: one profile, two ecosystems, and the
// answer is right on both.
TEST(AliasTest, OneProfileWorksOnBothEcosystems) {
    Profile profile;

    profile.name = "basic";
    profile.requirements.push_back(
        Requirement(Constraint("web-browser")));

    const AliasTable debian = parse(
        "Capability: web-browser\n"
        "Resolves-To: firefox | epiphany-browser\n");

    const AliasTable fedora = parse(
        "Capability: web-browser\n"
        "Resolves-To: firefox | epiphany\n");

    // Debian names it epiphany-browser; Fedora names it epiphany.
    const auto onDebian = nexus::checkProfile(
        profile,
        solverOver({make("epiphany-browser")}),
        debian
    );

    const auto onFedora = nexus::checkProfile(
        profile,
        solverOver({make("epiphany")}),
        fedora
    );

    EXPECT_TRUE(onDebian.complete());
    EXPECT_TRUE(onFedora.complete());

    // And without the table, neither would resolve.
    const auto without = nexus::checkProfile(
        profile,
        solverOver({make("epiphany")})
    );

    EXPECT_FALSE(without.complete());
}
