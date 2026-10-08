#pragma once

// nexus gamecheck: whether a game will run here, and what the launcher
// it runs under can see. Pure functions over text, so every rule here
// is tested without a network; the CLI fetches the text.
//
// Sources, all public:
//   - Steam store search, for the game's Steam id;
//   - ProtonDB summaries, for how well it runs through Proton;
//   - AreWeAntiCheatYet's games.json, for the anti-cheat's stance;
//   - `flatpak info --show-permissions`, for the launcher's sandbox.
//
// It reports. It never touches an anti-cheat: a game whose developer
// blocks Linux is reported as blocked, with no way around offered.

#include <string>
#include <utility>
#include <vector>

namespace nexus::system {

struct SteamMatch {
    std::string id;
    std::string name;
};

std::vector<SteamMatch> parseSteamSearch(const std::string& json);

// The best match for what was typed: an exact name (any case) first,
// otherwise the store's own first answer. Empty when there is none.
SteamMatch pickSteamMatch(const std::vector<SteamMatch>& matches,
                          const std::string& typed);

struct ProtonSummary {
    bool found = false;
    std::string tier;         // platinum, gold, silver, bronze, borked, ...
    std::string trending;
    std::string confidence;
    long reports = 0;
};

ProtonSummary parseProtonSummary(const std::string& json);

// "gold" -> "runs perfectly after small tweaks"
std::string describeTier(const std::string& tier);

struct AntiCheatEntry {
    bool found = false;
    std::string name;
    std::string steamId;                  // from storeIds, when listed
    std::string status;                   // Supported, Running, Planned, Broken, Denied
    bool native = false;
    std::vector<std::string> anticheats;
    std::vector<std::string> notes;
};

// Matched by Steam id first, then by name (any case).
AntiCheatEntry findAntiCheat(const std::string& gamesJson,
                             const std::string& steamId,
                             const std::string& name);

std::string describeAntiCheatStatus(const std::string& status);

// One line a person can act on, from everything above.
std::string gameVerdict(const ProtonSummary& proton,
                        const AntiCheatEntry& antiCheat);

// What a sandboxed launcher can reach, from
// `flatpak info --show-permissions <app>`. Pairs of (what, answer).
std::vector<std::pair<std::string, std::string>>
describeSandbox(const std::string& permissions);

// What a game could recognise this machine by.
//
// Anti-cheats ban machines, not only accounts: Valorant's Windows
// anti-cheat reads the TPM chip, which on AMD Ryzen sits inside the
// processor, so a resold processor carries the ban (asked about on
// 4 October). On Linux a game runs as an ordinary program, so what
// matters is which identifiers an ordinary program can read here.
// Nexus reports them and never changes or hides one: what an anti-cheat
// sees is between the game and its player.
//
// The CLI looks at the machine (file permissions, never the values,
// which are not printed anywhere) and passes in what it found.
struct IdentifierProbe {
    std::string key;      // mac, disk, machine-id, screen, board, tpm, cpu
    bool present = false; // this machine has it
    bool readable = false;// an ordinary program running as you can read it
};

std::vector<std::pair<std::string, std::string>>
describeIdentifiers(const std::vector<IdentifierProbe>& probes);

// Whether a Flatpak's permissions list a value under a [Context] key,
// e.g. ("shared", "network") or ("devices", "all").
bool sandboxAllows(const std::string& permissions, const std::string& key,
                   const std::string& value);

std::string urlEncode(const std::string& text);

}  // namespace nexus::system
