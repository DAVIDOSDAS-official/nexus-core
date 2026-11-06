#include <gtest/gtest.h>

#include <nexus/system/dev.hpp>

using namespace nexus::system;

TEST(DevTest, TheSevenLanguagesAndWhatPeopleType) {
    ASSERT_EQ(devLanguages().size(), 7u);
    EXPECT_EQ(findDevLanguage("C++")->key, "cpp");
    EXPECT_EQ(findDevLanguage("py")->key, "python");
    EXPECT_EQ(findDevLanguage("JavaScript")->key, "js");
    EXPECT_EQ(findDevLanguage("node")->key, "js");
    EXPECT_EQ(findDevLanguage("golang")->key, "go");
    EXPECT_EQ(findDevLanguage("cargo")->key, "rust");
    EXPECT_EQ(findDevLanguage("rb")->key, "ruby");
    EXPECT_EQ(findDevLanguage("java")->key, "java");
    EXPECT_FALSE(findDevLanguage("cobol").has_value());
}

TEST(DevTest, OnlyCAndPythonAreOnTheSystemWithTheAddon) {
    for (const auto& l : devLanguages()) {
        EXPECT_EQ(l.onSystemWithAddon, l.key == "cpp" || l.key == "python") << l.key;
        EXPECT_FALSE(l.packages.empty()) << l.key;
        EXPECT_FALSE(l.commands.empty()) << l.key;
    }
}

TEST(DevTest, ToolboxImageFollowsTheFedoraRelease) {
    EXPECT_EQ(devImageFor("NAME=\"Nexus-CORE\"\nID=fedora\nVERSION_ID=44\n"),
              "registry.fedoraproject.org/fedora-toolbox:44");
    EXPECT_EQ(devImageFor("VERSION_ID=\"45\"\n"), "registry.fedoraproject.org/fedora-toolbox:45");
    EXPECT_EQ(devImageFor("VERSION_ID=rawhide\n"), "registry.fedoraproject.org/fedora-toolbox:latest");
    EXPECT_EQ(devImageFor(""), "registry.fedoraproject.org/fedora-toolbox:latest");
}

TEST(DevTest, HelpTopicsAndTheirOtherNames) {
    EXPECT_NE(devHelp("git").find("git clone"), std::string::npos);
    EXPECT_EQ(devHelp("node"), devHelp("js"));
    EXPECT_EQ(devHelp("c++"), devHelp("cpp"));
    EXPECT_EQ(devHelp("docker"), devHelp("containers"));
    EXPECT_NE(devHelp("ports").find("ss -tlnp"), std::string::npos);
    EXPECT_EQ(devHelp("nothing-like-this"), "");
    EXPECT_GE(devHelpTopics().size(), 11u);
}
