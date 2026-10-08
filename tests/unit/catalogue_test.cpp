#include <gtest/gtest.h>

#include <nexus/system/catalogue.hpp>

using namespace nexus::system;

// Cut down from the real files (Flathub's appstream.xml.gz and
// Fedora 44's appstream-data, 8 October 2026), keeping the shapes
// that matter: translations, nested developer name, doubled icons,
// verification keys, a runtime and a font to skip.
static const char* kFlathub = R"(<?xml version="1.0" encoding="UTF-8"?>
<components version="0.8" origin="flatpak">
  <component type="desktop-application">
    <id>com.obsproject.Studio</id>
    <name>OBS Studio</name>
    <name xml:lang="de">OBS Studio DE</name>
    <summary>Live stream and record videos</summary>
    <summary xml:lang="pl">Nagrywaj</summary>
    <project_license>GPL-2.0-or-later</project_license>
    <description>
      <p>Free and open source software for video
      capturing, recording, and live streaming.</p>
      <p xml:lang="de">Nicht dieser</p>
      <p>Features:</p>
      <ul>
        <li>Scenes made of <em>many</em> sources.</li>
        <li>Modular &apos;Dock&apos; UI.</li>
      </ul>
    </description>
    <developer id="com.obsproject">
      <name>OBS Project</name>
    </developer>
    <developer_name>OBS Project</developer_name>
    <launchable type="desktop-id">com.obsproject.Studio.desktop</launchable>
    <icon height="64" type="cached" width="64">com.obsproject.Studio.png</icon>
    <icon height="64" scale="2" type="cached" width="64">com.obsproject.Studio.png</icon>
    <icon height="128" type="cached" width="128">com.obsproject.Studio.png</icon>
    <icon height="128" scale="2" type="cached" width="128">com.obsproject.Studio.png</icon>
    <icon type="stock">com.obsproject.Studio</icon>
    <icon height="128" type="remote" width="128">https://dl.flathub.org/x.png</icon>
    <url type="homepage">https://obsproject.com</url>
    <url type="bugtracker">https://github.com/obsproject/obs-studio/issues</url>
    <categories>
      <category>AudioVideo</category>
      <category>Recorder</category>
    </categories>
    <screenshots>
      <screenshot type="default">
        <caption>The OBS Studio window</caption>
        <image height="700" type="source" width="1000">https://dl.flathub.org/orig.png</image>
        <image height="526" type="thumbnail" width="752">https://dl.flathub.org/752.png</image>
        <image height="436" type="thumbnail" width="624">https://dl.flathub.org/624.png</image>
        <image height="156" type="thumbnail" width="224">https://dl.flathub.org/224.png</image>
      </screenshot>
    </screenshots>
    <releases>
      <release timestamp="1786665600" type="stable" version="32.2.2"/>
      <release timestamp="1700000000" type="stable" version="30.0.0"/>
    </releases>
    <custom>
      <value key="flathub::manifest">https://github.com/obsproject/</value>
    <value key="flathub::verification::verified">true</value>
  <value key="flathub::verification::method">website</value>
  <value key="flathub::verification::website">obsproject.com</value>
  </custom>
    <bundle type="flatpak" runtime="org.freedesktop.Platform/x86_64/25.08">app/com.obsproject.Studio/x86_64/stable</bundle>
  </component>
  <component type="runtime">
    <id>org.freedesktop.Platform</id>
    <name>Freedesktop Platform</name>
  </component>
  <component type="desktop-application">
    <id>org.gimp.GIMP</id>
    <name>GNU Image Manipulation Program</name>
    <summary>Create images and edit photographs</summary>
    <categories><category>Graphics</category></categories>
    <custom><value key="flathub::verification::verified">false</value></custom>
    <bundle type="flatpak">app/org.gimp.GIMP/x86_64/stable</bundle>
  </component>
</components>
)";

static const char* kFedora = R"(<?xml version="1.0" encoding="UTF-8"?>
<components origin="fedora" version="0.8">
<component type="desktop">
<id>com.obsproject.Studio</id>
<pkgname>obs-studio</pkgname>
<name>OBS Studio</name>
<summary>Live stream and record videos</summary>
<icon type="cached" height="64" width="64">com.obsproject.Studio.png</icon>
<icon type="cached" height="128" width="128">com.obsproject.Studio.png</icon>
<categories>
<category>AudioVideo</category>
</categories>
<releases>
<release timestamp="1768608000" version="32.0.4"/>
</releases>
</component>
<component type="desktop">
<id>org.gimp.GIMP</id>
<pkgname>gimp</pkgname>
<name>GNU Image Manipulation Program</name>
<summary>Create images and edit photographs</summary>
<developer_name>The GIMP team</developer_name>
<developer_name xml:lang="sr-Latn">Gimpov tim</developer_name>
<categories><category>Graphics</category></categories>
</component>
<component type="desktop">
<id>firefox.desktop</id>
<pkgname>firefox</pkgname>
<name>Firefox</name>
<summary>Web Browser</summary>
<categories><category>Network</category><category>WebBrowser</category></categories>
<keywords><keyword>browser</keyword><keyword>Web</keyword></keywords>
</component>
<component type="font">
<id>abattis-cantarell-fonts</id>
<name>Cantarell</name>
</component>
</components>
)";

static std::vector<CatalogueApp> both() {
    std::string error;
    auto apps = parseCatalogue(kFlathub, "flathub", error);
    EXPECT_EQ(error, "");
    auto fedora = parseCatalogue(kFedora, "fedora", error);
    EXPECT_EQ(error, "");
    apps.insert(apps.end(), fedora.begin(), fedora.end());
    return apps;
}

TEST(CatalogueTest, ReadsAFlathubApp) {
    std::string error;
    const auto apps = parseCatalogue(kFlathub, "flathub", error);

    ASSERT_EQ(apps.size(), 2u);  // the runtime is not an app
    const CatalogueApp& obs = apps[0];
    EXPECT_EQ(obs.id, "com.obsproject.Studio");
    EXPECT_EQ(obs.name, "OBS Studio");
    EXPECT_EQ(obs.summary, "Live stream and record videos");
    EXPECT_EQ(obs.developer, "OBS Project");
    EXPECT_EQ(obs.package, "com.obsproject.Studio");
    EXPECT_EQ(obs.flatpakRef, "app/com.obsproject.Studio/x86_64/stable");
    EXPECT_EQ(obs.icon, "com.obsproject.Studio.png");
    EXPECT_EQ(obs.iconSize, 128);
    EXPECT_EQ(obs.homepage, "https://obsproject.com");
    EXPECT_EQ(obs.license, "GPL-2.0-or-later");
    EXPECT_EQ(obs.latestRelease, "32.2.2");
    EXPECT_TRUE(obs.verified);
    EXPECT_EQ(obs.verifiedBy, "obsproject.com");
    ASSERT_EQ(obs.screenshots.size(), 1u);
    EXPECT_EQ(obs.screenshots[0], "https://dl.flathub.org/624.png");
    EXPECT_EQ(obs.categories,
              (std::vector<std::string>{"AudioVideo", "Recorder"}));
}

TEST(CatalogueTest, SkipsTranslations) {
    std::string error;
    const auto apps = parseCatalogue(kFlathub, "flathub", error);
    EXPECT_EQ(apps[0].name, "OBS Studio");
    EXPECT_EQ(apps[0].description.find("Nicht"), std::string::npos);

    const auto fedora = parseCatalogue(kFedora, "fedora", error);
    EXPECT_EQ(fedora[1].developer, "The GIMP team");
}

TEST(CatalogueTest, DescriptionIsPlainTextWithListItems) {
    std::string error;
    const auto apps = parseCatalogue(kFlathub, "flathub", error);
    EXPECT_EQ(apps[0].description,
              "Free and open source software for video capturing, "
              "recording, and live streaming.\n"
              "Features:\n"
              "\xe2\x80\xa2 Scenes made of many sources.\n"
              "\xe2\x80\xa2 Modular 'Dock' UI.");
}

TEST(CatalogueTest, FlathubUnverifiedIsNotVerified) {
    std::string error;
    const auto apps = parseCatalogue(kFlathub, "flathub", error);
    EXPECT_EQ(apps[1].id, "org.gimp.GIMP");
    EXPECT_FALSE(apps[1].verified);
    EXPECT_EQ(apps[1].iconSize, 0);
}

TEST(CatalogueTest, ReadsFedoraAndDropsDesktopSuffixAndFonts) {
    std::string error;
    const auto apps = parseCatalogue(kFedora, "fedora", error);
    ASSERT_EQ(apps.size(), 3u);
    EXPECT_EQ(apps[0].package, "obs-studio");
    EXPECT_EQ(apps[0].flatpakRef, "");
    EXPECT_FALSE(apps[0].verified);
    EXPECT_EQ(apps[2].id, "firefox");
    EXPECT_EQ(apps[2].keywords,
              (std::vector<std::string>{"browser", "web"}));
}

TEST(CatalogueTest, BrokenXmlReportsAndKeepsWhatItRead) {
    std::string error;
    const std::string broken =
        std::string(kFedora).substr(0, std::string(kFedora).find(
            "<component type=\"desktop\">\n<id>firefox")) + "<compo";
    const auto apps = parseCatalogue(broken, "fedora", error);
    EXPECT_NE(error, "");
    EXPECT_EQ(apps.size(), 2u);
}

TEST(CatalogueTest, GroupsTheSameAppFromBothSources) {
    const auto apps = both();
    const auto entries = groupCatalogue(apps);

    ASSERT_EQ(entries.size(), 3u);  // OBS, GIMP, Firefox
    EXPECT_EQ(entries[0].key, "com.obsproject.studio");
    ASSERT_EQ(entries[0].offers.size(), 2u);
    EXPECT_EQ(entries[0].offers[0]->source, "flathub");
    EXPECT_EQ(entries[0].offers[1]->source, "fedora");
    EXPECT_EQ(entries[2].key, "firefox");
    EXPECT_EQ(entries[2].offers.size(), 1u);
}

TEST(CatalogueTest, SearchPutsTheNameMatchFirst) {
    const auto apps = both();
    const auto entries = groupCatalogue(apps);

    auto found = searchCatalogue(entries, "obs");
    ASSERT_FALSE(found.empty());
    EXPECT_EQ(entries[found[0]].key, "com.obsproject.studio");

    found = searchCatalogue(entries, "  Browser ");
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(entries[found[0]].key, "firefox");

    found = searchCatalogue(entries, "photographs");
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(entries[found[0]].key, "org.gimp.gimp");

    // Every word, any order.
    found = searchCatalogue(entries, "videos record");
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(entries[found[0]].key, "com.obsproject.studio");
    EXPECT_TRUE(searchCatalogue(entries, "videos zzzz").empty());

    EXPECT_TRUE(searchCatalogue(entries, "").empty());
    EXPECT_TRUE(searchCatalogue(entries, "zzzz").empty());
}

TEST(CatalogueTest, SuggestsVerifiedFlathubAndSaysWhy) {
    const auto apps = both();
    const auto entries = groupCatalogue(apps);
    const auto s = suggestSource(entries[0]);

    EXPECT_EQ(s.source, "flathub");
    ASSERT_FALSE(s.reasons.empty());
    EXPECT_EQ(s.reasons[0],
              "Published by OBS Project itself, as Flathub confirms");
    EXPECT_NE(s.otherNote.find("next restart"), std::string::npos);
}

TEST(CatalogueTest, SuggestsFedoraOverAnUnverifiedFlathubCopy) {
    const auto apps = both();
    const auto entries = groupCatalogue(apps);
    const auto s = suggestSource(entries[1]);  // GIMP

    EXPECT_EQ(s.source, "fedora");
    EXPECT_EQ(s.reasons[0], "Built and signed by Fedora's packagers");
    EXPECT_NE(s.otherNote.find("volunteers"), std::string::npos);
}

TEST(CatalogueTest, OnlyOneSourceIsThatSource) {
    const auto apps = both();
    const auto entries = groupCatalogue(apps);
    const auto s = suggestSource(entries[2]);  // Firefox, Fedora only

    EXPECT_EQ(s.source, "fedora");
    EXPECT_EQ(s.otherNote, "");
}

TEST(CatalogueTest, ShopCategories) {
    EXPECT_EQ(shopCategory({"AudioVideo", "Recorder"}), "Audio and video");
    EXPECT_EQ(shopCategory({"Graphics", "Game"}), "Games");
    EXPECT_EQ(shopCategory({"Network", "WebBrowser"}), "Internet");
    EXPECT_EQ(shopCategory({"Science"}), "Education");
    EXPECT_EQ(shopCategory({"X-Unknown"}), "");
    EXPECT_EQ(shopCategories().size(), 8u);
}

TEST(CatalogueTest, RestartAndKindInPlainWords) {
    CatalogueApp flat;
    flat.source = "flathub";
    CatalogueApp rpm;
    rpm.source = "fedora";
    EXPECT_EQ(sourceKind(flat), "Flatpak app");
    EXPECT_EQ(sourceRestart(flat), "not needed");
    EXPECT_EQ(sourceKind(rpm), "System package");
    EXPECT_EQ(sourceRestart(rpm), "needed once");
}
