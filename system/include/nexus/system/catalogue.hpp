#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace nexus::system {

// The app catalogues the Shop reads: Fedora's (the appstream-data
// package, /usr/share/swcatalog/xml/fedora.xml.gz) and Flathub's
// (downloaded by `flatpak update --appstream`, kept under
// /var/lib/flatpak/appstream/flathub). Both are AppStream XML.
//
// Only what the Shop shows is kept. Translations are skipped (the
// catalogues carry dozens per app), and so are fonts, add-ons,
// runtimes and codecs: the Shop lists apps.
//
// What a catalogue says about a version is the developer's latest
// release note, not the version a source would install -- Fedora's
// file is written once per Fedora release and is months old by the
// time anyone reads it. So it is kept as "latestRelease" and never
// shown as the version that would be installed.
struct CatalogueApp {
    std::string id;          // as written, minus a trailing ".desktop"
    std::string source;      // "flathub", "fedora", "rpmfusion-free",
                             // "rpmfusion-nonfree"
    std::string name;
    std::string summary;
    std::string developer;
    std::string description; // plain text: paragraphs, "• " list items
    std::string package;     // Fedora: package name; Flathub: app id
    std::string flatpakRef;  // Flathub: app/<id>/<arch>/<branch>
    std::string icon;        // cached icon file name, if any
    int iconSize = 0;        // 128 or 64 (0: none)
    std::string license;
    std::string homepage;
    std::string latestRelease;
    std::vector<std::string> categories;
    std::vector<std::string> keywords;
    std::vector<std::string> screenshots; // thumbnail URLs, ~624 wide

    // Flathub only: the developer proved it is them (website, or
    // their account on GitHub, GitLab...). Fedora has no equivalent:
    // its packages are built and signed by Fedora itself.
    bool verified = false;
    std::string verifiedBy; // the website or login it was proved by
};

// Read one catalogue. Returns the apps; fills error and returns what
// it read so far if the XML is broken.
std::vector<CatalogueApp> parseCatalogue(
    const std::string& xml,
    const std::string& source,
    std::string& error
);

// The same app from several sources, under one key: the id, lower
// case. Firefox is org.mozilla.firefox in both catalogues, which is
// what makes the side-by-side comparison possible.
struct ShopEntry {
    std::string key;
    std::vector<const CatalogueApp*> offers; // Flathub first, then Fedora

    const CatalogueApp& first() const { return *offers.front(); }
};

std::vector<ShopEntry> groupCatalogue(
    const std::vector<CatalogueApp>& apps
);

std::string catalogueKey(const std::string& id);

// Search by name, id, keywords and summary, best match first.
// Returns indexes into entries.
std::vector<std::size_t> searchCatalogue(
    const std::vector<ShopEntry>& entries,
    const std::string& query,
    std::size_t limit = 60
);

// The Shop's own groups, from freedesktop categories. Empty when
// none fits.
std::string shopCategory(const std::vector<std::string>& categories);

// The groups, in the order Home shows them.
const std::vector<std::string>& shopCategories();

// The source a catalogue file speaks for, from its origin attribute
// ("fedora", "flatpak", "rpmfusion-free-44"...). Empty: not one the
// Shop shows (Nexus offers known sources only).
std::string catalogueSource(const std::string& origin);

// "Flathub", "Fedora", "RPM Fusion", "RPM Fusion (non-free)".
std::string sourceLabel(const std::string& source);

// Who built it and who vouches for it, in a few words.
std::string sourcePackager(const CatalogueApp& app);

// Fedora and RPM Fusion packages join the system image.
bool isSystemSource(const std::string& source);

// Which source Nexus suggests, and why, in plain words. The person
// still chooses; this only says which one and the reasons.
struct SourceSuggestion {
    std::string source;                 // as CatalogueApp::source
    std::vector<std::string> reasons;   // for the suggested one
    std::string otherNote;              // why not the other, if any
};

SourceSuggestion suggestSource(const ShopEntry& entry);

// One line per source, for the comparison table.
std::string sourceKind(const CatalogueApp& app);   // "Flatpak app"...
std::string sourceRestart(const CatalogueApp& app); // on this system

// Whether text is shaped like a Flatpak app id (com.obsproject.Studio):
// letters, digits, _ and -, at least three parts. It goes into a
// command line, so anything else is refused rather than quoted.
bool isFlatpakId(const std::string& id);

}
