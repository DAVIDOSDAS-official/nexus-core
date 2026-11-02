#include <gtest/gtest.h>

#include <nexus/system/suggest.hpp>

using namespace nexus::system;

// The typo from the Asus, 1 November.
TEST(SuggestTest, FindsTheTypoFromTheAsus) {
    const std::vector<std::string> names = {
        "java-latest-openjdk", "java-21-openjdk", "java-25-openjdk",
        "javascriptcoregtk", "firefox"};
    const auto s = spellingSuggestions(names, "java-lastest-openjdk");
    ASSERT_FALSE(s.empty());
    EXPECT_EQ(s.front(), "java-latest-openjdk");
}

TEST(SuggestTest, ClosestFirstThenShortest) {
    const std::vector<std::string> names = {"firefoxx", "firefox", "firefox-x"};
    const auto s = spellingSuggestions(names, "firefx");
    ASSERT_GE(s.size(), 2u);
    EXPECT_EQ(s[0], "firefox");  // one letter away
}

TEST(SuggestTest, ShortNamesMustBeCloser) {
    // "vim" vs "vlc": two letters off in a three-letter name is
    // another program, not a typo.
    EXPECT_TRUE(spellingSuggestions({"vlc"}, "vim").empty());
    EXPECT_EQ(spellingSuggestions({"vlc"}, "vlv"),
              std::vector<std::string>{"vlc"});
}

TEST(SuggestTest, NothingForAnExactNameOrNothingClose) {
    EXPECT_TRUE(spellingSuggestions({"steam"}, "steam").empty());
    EXPECT_TRUE(spellingSuggestions({"steam", "gimp"}, "libreoffice").empty());
    EXPECT_TRUE(spellingSuggestions({}, "x").empty());
}

TEST(SuggestTest, DuplicatesAndLimit) {
    const std::vector<std::string> names = {"nmapp", "nmapp", "nmao", "nmap2", "map"};
    const auto s = spellingSuggestions(names, "nmap", 2);
    EXPECT_EQ(s.size(), 2u);
    EXPECT_NE(s[0], s[1]);
}

TEST(SuggestTest, BoundedDistanceGivesUpEarly) {
    EXPECT_EQ(boundedDistance("kitten", "sitting", 3), 3u);
    EXPECT_EQ(boundedDistance("abc", "abcdefgh", 2), 3u);
    EXPECT_EQ(boundedDistance("same", "same", 0), 0u);
}
