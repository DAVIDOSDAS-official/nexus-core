#include <gtest/gtest.h>

#include <nexus/system/language.hpp>

using namespace nexus::system;

// Lines as the Asus printed them, 4 October.
static const char* kRepoquery =
    "langpacks-sr | Serbian langpacks meta-package\n"
    "langpacks-core-sr | Serbian langpacks core meta-package\n"
    "langpacks-fonts-sr | Metapackage to install extra fonts for Serbian\n"
    "langpacks-mk | Macedonian langpacks meta-package\n"
    "langpacks-af | Afrikaans langpacks meta-package\n"
    "langpacks-sr | Serbian langpacks meta-package\n"
    "glibc-langpack-sr | Locale data for Serbian\n"
    "langpacks-odd | Something else entirely\n";

TEST(LanguageTest, ReadsLanguagesNotTheirParts) {
    const auto list = parseLanguagePacks(kRepoquery);
    ASSERT_EQ(list.size(), 4u);
    EXPECT_EQ(list[0].name, "Afrikaans");
    EXPECT_EQ(list[1].name, "Macedonian");
    EXPECT_EQ(list[2].name, "odd");     // no usual summary: the code
    EXPECT_EQ(list[3].code, "sr");
    EXPECT_EQ(list[3].name, "Serbian");
}

TEST(LanguageTest, FindsByCodeOrNameInAnyCase) {
    const auto list = parseLanguagePacks(kRepoquery);
    ASSERT_NE(findLanguage(list, "sr"), nullptr);
    EXPECT_EQ(findLanguage(list, "SERBIAN")->code, "sr");
    EXPECT_EQ(findLanguage(list, " macedonian ")->code, "mk");
    EXPECT_EQ(findLanguage(list, "serb"), nullptr);
}

static const char* kLocales =
    "en_US.UTF-8\n"
    "mk_MK.UTF-8\n"
    "sr_ME.UTF-8\n"
    "sr_RS.UTF-8\n"
    "sr_RS.UTF-8@latin\n"
    "srn_SR.UTF-8\n";

TEST(LanguageTest, LocalesBelongToTheirLanguageOnly) {
    const auto sr = localesFor(kLocales, "sr");
    ASSERT_EQ(sr.size(), 3u);
    EXPECT_EQ(sr[2], "sr_RS.UTF-8@latin");
    EXPECT_EQ(localesFor(kLocales, "mk").size(), 1u);
    EXPECT_TRUE(localesFor(kLocales, "xx").empty());
}

TEST(LanguageTest, AnExactLocaleIsTheAnswer) {
    const auto one = localesFor(kLocales, "sr_RS.UTF-8@latin");
    ASSERT_EQ(one.size(), 1u);
    EXPECT_EQ(one[0], "sr_RS.UTF-8@latin");
}

TEST(LanguageTest, DescribesLocales) {
    EXPECT_EQ(describeLocale("sr_RS.UTF-8@latin"), "sr, RS, Latin script");
    EXPECT_EQ(describeLocale("mk_MK.UTF-8"), "mk, MK");
    EXPECT_EQ(localeLanguage("sr_RS.UTF-8@latin"), "sr");
}

TEST(LanguageTest, ReadsLangFromLocaleConf) {
    EXPECT_EQ(localeConfLang("LANG=\"en_US.UTF-8\"\n"), "en_US.UTF-8");
    EXPECT_EQ(localeConfLang("# x\nLC_TIME=x\nLANG=mk_MK.UTF-8\n"),
              "mk_MK.UTF-8");
    EXPECT_EQ(localeConfLang(""), "");
}
