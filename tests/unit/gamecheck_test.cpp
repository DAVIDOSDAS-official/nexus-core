#include <gtest/gtest.h>

#include <map>

#include <nexus/system/gamecheck.hpp>
#include <nexus/system/json.hpp>

using namespace nexus::system;

TEST(JsonTest, ReadsNestedValues) {
    const auto r = parseJson(R"({"a":[1,"two",true,null],"b":{"c":"d"}})");
    ASSERT_TRUE(r.error.empty()) << r.error;
    EXPECT_EQ(r.value["a"].items.size(), 4u);
    EXPECT_EQ(r.value["a"].items[1].asString(), "two");
    EXPECT_EQ(r.value["b"]["c"].asString(), "d");
    EXPECT_TRUE(r.value["missing"]["deeper"].isNull());
}

TEST(JsonTest, DecodesEscapesToUtf8) {
    const auto r = parseJson(R"(["čš \"q\" \\ \n"])");
    ASSERT_TRUE(r.error.empty()) << r.error;
    EXPECT_EQ(r.value.items[0].asString(), "\xc4\x8d\xc5\xa1 \"q\" \\ \n");
}

TEST(JsonTest, RefusesBrokenText) {
    EXPECT_FALSE(parseJson("{\"a\":").error.empty());
    EXPECT_FALSE(parseJson("[1,2").error.empty());
    EXPECT_FALSE(parseJson("<html>").error.empty());
    EXPECT_FALSE(parseJson("{} trailing").error.empty());
}

TEST(JsonTest, RefusesAbsurdNesting) {
    EXPECT_FALSE(parseJson(std::string(1000, '[')).error.empty());
}

TEST(GamecheckTest, PicksExactSteamNameFirst) {
    const auto matches = parseSteamSearch(R"({"total":3,"items":[
        {"type":"app","name":"ELDEN RING Nightreign","id":2622380},
        {"type":"app","name":"ELDEN RING","id":1245620},
        {"type":"sub","name":"bundle","id":5}]})");
    ASSERT_EQ(matches.size(), 2u);
    EXPECT_EQ(pickSteamMatch(matches, "elden ring").id, "1245620");
    EXPECT_EQ(pickSteamMatch(matches, "elden").id, "2622380");
    EXPECT_TRUE(pickSteamMatch({}, "x").id.empty());
}

TEST(GamecheckTest, ReadsProtonSummary) {
    const auto s = parseProtonSummary(R"({"bestReportedTier":"platinum",
        "confidence":"strong","score":0.8,"tier":"gold","total":1240,
        "trendingTier":"platinum"})");
    EXPECT_TRUE(s.found);
    EXPECT_EQ(s.tier, "gold");
    EXPECT_EQ(s.reports, 1240);
    EXPECT_FALSE(parseProtonSummary("Not Found").found);
}

const char* kGames = R"([
  {"name":"Halo: The Master Chief Collection","native":false,
   "status":"Supported","anticheats":["Easy Anti-Cheat"],
   "notes":[["All-modes enabled","https://example"]],
   "storeIds":{"steam":"976730"}},
  {"name":"Fortnite","native":false,"status":"Denied",
   "anticheats":["Easy Anti-Cheat"],"notes":[],
   "storeIds":{"epic":{"namespace":"fn","slug":"fortnite"}}}
])";

TEST(GamecheckTest, FindsAntiCheatBySteamIdThenName) {
    const auto halo = findAntiCheat(kGames, "976730", "whatever");
    ASSERT_TRUE(halo.found);
    EXPECT_EQ(halo.status, "Supported");
    EXPECT_EQ(halo.steamId, "976730");
    EXPECT_EQ(halo.anticheats.front(), "Easy Anti-Cheat");
    EXPECT_EQ(halo.notes.front(), "All-modes enabled");

    const auto fortnite = findAntiCheat(kGames, "", "fortnite");
    ASSERT_TRUE(fortnite.found);
    EXPECT_EQ(fortnite.status, "Denied");

    EXPECT_FALSE(findAntiCheat(kGames, "1", "Tetris").found);
}

TEST(GamecheckTest, DeniedAntiCheatOverridesAGoodTier) {
    ProtonSummary proton;
    proton.found = true;
    proton.tier = "platinum";
    const auto fortnite = findAntiCheat(kGames, "", "Fortnite");
    EXPECT_NE(gameVerdict(proton, fortnite).find("Will not work"),
              std::string::npos);
    EXPECT_NE(describeAntiCheatStatus("Denied").find("will not get around"),
              std::string::npos);
}

TEST(GamecheckTest, VerdictFollowsTier) {
    ProtonSummary p;
    p.tier = "gold";
    EXPECT_NE(gameVerdict(p, {}).find("Should work"), std::string::npos);
    p.tier = "borked";
    EXPECT_NE(gameVerdict(p, {}).find("Unlikely"), std::string::npos);
    p.tier = "";
    EXPECT_NE(gameVerdict(p, {}).find("Unknown"), std::string::npos);
    const auto halo = findAntiCheat(kGames, "976730", "");
    p.tier = "gold";
    EXPECT_NE(gameVerdict(p, halo).find("online included"), std::string::npos);
}

TEST(GamecheckTest, DescribesSteamFlatpakSandbox) {
    const auto lines = describeSandbox(
        "[Context]\n"
        "shared=network;ipc;\n"
        "sockets=x11;wayland;pulseaudio;\n"
        "devices=all;\n"
        "filesystems=xdg-music:ro;xdg-pictures:ro;xdg-run/app/com.discordapp.Discord:create;\n"
        "\n[Session Bus Policy]\norg.freedesktop.Notifications=talk\n");
    std::map<std::string, std::string> m(lines.begin(), lines.end());
    EXPECT_EQ(m["Your files"],
              "its own folder, plus Music (read only), Pictures (read only)");
    EXPECT_NE(m["Network"].find("yes"), std::string::npos);
    EXPECT_NE(m["Microphone"].find("without asking"), std::string::npos);
    EXPECT_NE(m["Devices"].find("webcam"), std::string::npos);
    EXPECT_EQ(m["Location"], "only if it asks and you allow it");
}

TEST(GamecheckTest, HomeAccessIsAllFiles) {
    const auto lines = describeSandbox("[Context]\nfilesystems=home;\n");
    EXPECT_EQ(lines.front().second, "all your files");
}

TEST(GamecheckTest, EncodesSearchTerms) {
    EXPECT_EQ(urlEncode("Elden Ring: Č"), "Elden%20Ring%3A%20%C4%8C");
}
