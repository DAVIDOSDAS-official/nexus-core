#include <nexus/system/catalogue.hpp>

#include <nexus/system/xml.hpp>

#include <algorithm>
#include <cstdlib>
#include <cctype>
#include <map>
#include <utility>

namespace nexus::system {

namespace {

std::string lower(std::string text) {
    for (char& character : text) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    }
    return text;
}

bool endsWith(const std::string& text, const std::string& suffix) {
    return text.size() >= suffix.size() &&
           text.compare(text.size() - suffix.size(), suffix.size(),
                        suffix) == 0;
}

// Runs of whitespace (the catalogues indent their text) become one
// space, and the ends are trimmed.
std::string squeeze(const std::string& text) {
    std::string output;
    bool space = false;

    for (char character : text) {
        if (std::isspace(static_cast<unsigned char>(character)) != 0) {
            space = !output.empty();
            continue;
        }
        if (space) {
            output.push_back(' ');
            space = false;
        }
        output.push_back(character);
    }
    return output;
}

std::string joined(const std::vector<std::string>& words) {
    std::string all;
    for (const std::string& word : words) {
        all += word + " ";
    }
    return all;
}

// Every word of the query appears somewhere in the text. Only for
// queries of two words or more: one word is already covered above.
bool everyWord(const std::string& query, const std::string& text) {
    std::size_t words = 0;
    std::size_t start = 0;
    while (start < query.size()) {
        std::size_t end = query.find(' ', start);
        if (end == std::string::npos) {
            end = query.size();
        }
        if (end > start) {
            if (text.find(query.substr(start, end - start)) ==
                std::string::npos) {
                return false;
            }
            ++words;
        }
        start = end + 1;
    }
    return words >= 2;
}

bool inlineMarkup(const std::string& name) {
    return name == "em" || name == "code" || name == "strong" ||
           name == "b" || name == "i";
}

bool listedApp(const std::string& type) {
    return type == "desktop" || type == "desktop-application";
}

}

std::string catalogueKey(const std::string& id) {
    std::string key = lower(id);

    if (endsWith(key, ".desktop")) {
        key.resize(key.size() - 8);
    }
    return key;
}

std::vector<CatalogueApp> parseCatalogue(
    const std::string& xml,
    const std::string& source,
    std::string& error
) {
    std::vector<CatalogueApp> apps;
    XmlReader reader(xml);

    // Where we are. The catalogue nests shallowly and predictably, so
    // a path of element names is enough to tell <name> of the app
    // from <name> of its developer.
    std::vector<std::string> path;
    // Elements carrying xml:lang (a translation), and everything
    // inside them, are skipped. Depth at which skipping started.
    std::size_t skipFrom = 0;
    bool skipping = false;

    bool inApp = false;
    bool keep = false;
    CatalogueApp app;
    std::string text;
    std::string description;
    int bestIcon = 0;
    std::string iconType;
    int iconWidth = 0;
    bool iconScaled = false;
    std::string urlType;
    std::string imageType;
    int imageWidth = 0;
    std::string screenshotPick;
    int screenshotPickWidth = 0;
    std::string customKey;
    bool sawRelease = false;

    for (;;) {
        const XmlReader::Event event = reader.next();

        if (event == XmlReader::Event::None) {
            // A catalogue cut short (a download that stopped) ends
            // with elements still open. Say so rather than show a
            // shop with half the apps and no explanation.
            if (!path.empty()) {
                error = "the file ends early, inside <" + path.back() + ">";
            }
            break;
        }
        if (event == XmlReader::Event::Error) {
            error = "line " + std::to_string(reader.line()) + ": " +
                    reader.error();
            break;
        }

        if (event == XmlReader::Event::StartElement) {
            const std::string& name = reader.name();

            path.push_back(name);

            if (skipping) {
                continue;
            }
            if (reader.hasAttribute("xml:lang")) {
                skipping = true;
                skipFrom = path.size();
                continue;
            }

            // Inline markup inside a paragraph keeps the paragraph's
            // text going.
            if (!inlineMarkup(name)) {
                text.clear();
            }

            if (name == "component" && path.size() == 2) {
                inApp = true;
                keep = listedApp(reader.attribute("type"));
                app = CatalogueApp{};
                app.source = source;
                description.clear();
                bestIcon = 0;
                sawRelease = false;
                continue;
            }
            if (!inApp || !keep) {
                continue;
            }

            if (name == "icon") {
                iconType = reader.attribute("type");
                iconWidth = std::atoi(reader.attribute("width").c_str());
                iconScaled = reader.hasAttribute("scale");
            } else if (name == "url") {
                urlType = reader.attribute("type");
            } else if (name == "image") {
                imageType = reader.attribute("type");
                imageWidth = std::atoi(reader.attribute("width").c_str());
            } else if (name == "screenshot") {
                screenshotPick.clear();
                screenshotPickWidth = 0;
            } else if (name == "value") {
                customKey = reader.attribute("key");
            } else if (name == "release" && !sawRelease) {
                // Newest first in both catalogues.
                sawRelease = true;
                app.latestRelease = reader.attribute("version");
            } else if (name == "bundle" &&
                       reader.attribute("type") == "flatpak") {
                urlType = "bundle";
            }
            continue;
        }

        if (event == XmlReader::Event::Text) {
            if (!skipping) {
                text += reader.text();
            }
            continue;
        }

        // EndElement.
        const std::string name = path.empty() ? "" : path.back();
        const std::size_t depth = path.size();

        if (!path.empty()) {
            path.pop_back();
        }
        if (skipping) {
            if (depth == skipFrom) {
                skipping = false;
            }
            continue;
        }
        if (!inApp) {
            continue;
        }

        if (name == "component" && depth == 2) {
            inApp = false;
            if (keep && !app.id.empty() && !app.name.empty()) {
                // Paragraph and list-item ends are '\n' in
                // description; each line is squeezed on its own.
                std::string line;
                for (std::size_t i = 0; i <= description.size(); ++i) {
                    if (i < description.size() && description[i] != '\n') {
                        line.push_back(description[i]);
                        continue;
                    }
                    const std::string done = squeeze(line);
                    if (!done.empty()) {
                        if (!app.description.empty()) {
                            app.description += "\n";
                        }
                        app.description += done;
                    }
                    line.clear();
                }
                if (app.package.empty() && source == "flathub") {
                    app.package = app.id;
                }
                apps.push_back(std::move(app));
            }
            continue;
        }
        if (!keep || inlineMarkup(name)) {
            continue;
        }

        const std::string parent = path.empty() ? "" : path.back();
        const std::string value = squeeze(text);

        if (name == "id" && parent == "component") {
            app.id = value;
            if (endsWith(app.id, ".desktop")) {
                app.id.resize(app.id.size() - 8);
            }
        } else if (name == "name" && parent == "component") {
            app.name = value;
        } else if (name == "summary" && parent == "component") {
            app.summary = value;
        } else if (name == "name" && parent == "developer") {
            app.developer = value;
        } else if (name == "developer_name" && parent == "component") {
            if (app.developer.empty()) {
                app.developer = value;
            }
        } else if (name == "pkgname") {
            app.package = value;
        } else if (name == "project_license") {
            app.license = value;
        } else if (name == "category") {
            app.categories.push_back(value);
        } else if (name == "keyword") {
            app.keywords.push_back(lower(value));
        } else if (name == "url" && parent == "component") {
            if (urlType == "homepage") {
                app.homepage = value;
            }
        } else if (name == "bundle") {
            app.flatpakRef = value;
        } else if (name == "icon") {
            // The plain (not doubled) cached icon, 128 if there is
            // one: the file sits in icons/<w>x<h>/.
            if (iconType == "cached" && !iconScaled &&
                (iconWidth == 128 || iconWidth == 64) &&
                iconWidth > bestIcon) {
                bestIcon = iconWidth;
                app.icon = value;
                app.iconSize = iconWidth;
            }
        } else if (name == "image") {
            // The thumbnail nearest 624 wide: big enough for the app
            // page, small enough for a slow connection.
            if (imageType == "thumbnail" &&
                (screenshotPick.empty() ||
                 std::abs(imageWidth - 624) <
                     std::abs(screenshotPickWidth - 624))) {
                screenshotPick = value;
                screenshotPickWidth = imageWidth;
            }
        } else if (name == "screenshot") {
            if (!screenshotPick.empty() && app.screenshots.size() < 4) {
                app.screenshots.push_back(screenshotPick);
            }
        } else if (name == "value") {
            if (customKey == "flathub::verification::verified") {
                app.verified = value == "true";
            } else if (customKey == "flathub::verification::website" ||
                       customKey == "flathub::verification::login_name") {
                app.verifiedBy = value;
            }
        } else if (path.size() >= 3 && path[2] == "description") {
            // Inside <description>: text of <p> and <li>.
            if (name == "p") {
                description += value + "\n";
            } else if (name == "li") {
                description += "• " + value + "\n";
            }
        }
        text.clear();
    }

    return apps;
}

std::vector<ShopEntry> groupCatalogue(
    const std::vector<CatalogueApp>& apps
) {
    std::map<std::string, std::size_t> where;
    std::vector<ShopEntry> entries;

    for (const CatalogueApp& app : apps) {
        const std::string key = catalogueKey(app.id);
        auto found = where.find(key);

        if (found == where.end()) {
            where.emplace(key, entries.size());
            entries.push_back(ShopEntry{key, {&app}});
            continue;
        }

        auto& offers = entries[found->second].offers;

        // One offer per source: Fedora lists a few apps twice.
        bool already = false;
        for (const CatalogueApp* offer : offers) {
            already = already || offer->source == app.source;
        }
        if (already) {
            continue;
        }
        offers.push_back(&app);
        std::stable_sort(offers.begin(), offers.end(),
            [](const CatalogueApp* a, const CatalogueApp* b) {
                return a->source == "flathub" && b->source != "flathub";
            });
    }
    return entries;
}

std::vector<std::size_t> searchCatalogue(
    const std::vector<ShopEntry>& entries,
    const std::string& query,
    std::size_t limit
) {
    const std::string wanted = lower(squeeze(query));
    std::vector<std::pair<int, std::size_t>> scored;

    if (wanted.empty()) {
        return {};
    }

    for (std::size_t index = 0; index < entries.size(); ++index) {
        int score = 0;

        for (const CatalogueApp* offer : entries[index].offers) {
            const std::string name = lower(offer->name);
            int here = 0;

            if (name == wanted) {
                here = 100;
            } else if (name.rfind(wanted, 0) == 0) {
                here = 80;
            } else if (name.find(" " + wanted) != std::string::npos) {
                here = 70;
            } else if (name.find(wanted) != std::string::npos) {
                here = 55;
            } else if (lower(offer->id).find(wanted) !=
                       std::string::npos) {
                here = 45;
            } else if (std::find(offer->keywords.begin(),
                                 offer->keywords.end(), wanted) !=
                       offer->keywords.end()) {
                here = 40;
            } else if (lower(offer->summary).find(wanted) !=
                       std::string::npos) {
                here = 25;
            } else if (everyWord(wanted, name + " " +
                                 lower(offer->summary) + " " +
                                 joined(offer->keywords))) {
                // "edit video" finds "Video editor": every word is
                // there, in any order.
                here = 15;
            }
            score = std::max(score, here);
        }

        if (score == 0) {
            continue;
        }
        // Ties: the app in more sources, then a verified one.
        score = score * 10 +
                static_cast<int>(entries[index].offers.size()) * 2;
        for (const CatalogueApp* offer : entries[index].offers) {
            score += offer->verified ? 1 : 0;
        }
        scored.emplace_back(score, index);
    }

    std::stable_sort(scored.begin(), scored.end(),
        [&](const auto& a, const auto& b) {
            if (a.first != b.first) {
                return a.first > b.first;
            }
            return lower(entries[a.second].first().name) <
                   lower(entries[b.second].first().name);
        });

    std::vector<std::size_t> result;
    for (const auto& item : scored) {
        if (result.size() >= limit) {
            break;
        }
        result.push_back(item.second);
    }
    return result;
}

const std::vector<std::string>& shopCategories() {
    static const std::vector<std::string> names = {
        "Audio and video", "Graphics", "Office", "Education",
        "Development", "Games", "Internet", "Utilities",
    };
    return names;
}

std::string shopCategory(const std::vector<std::string>& categories) {
    // First match wins, in this order: a game that is also
    // "Graphics" belongs with games.
    static const std::vector<std::pair<std::string, std::string>> map = {
        {"Game", "Games"},
        {"Development", "Development"},
        {"Education", "Education"},
        {"Science", "Education"},
        {"AudioVideo", "Audio and video"},
        {"Audio", "Audio and video"},
        {"Video", "Audio and video"},
        {"Graphics", "Graphics"},
        {"Office", "Office"},
        {"Network", "Internet"},
        {"Utility", "Utilities"},
        {"System", "Utilities"},
    };

    for (const auto& [freedesktop, ours] : map) {
        if (std::find(categories.begin(), categories.end(),
                      freedesktop) != categories.end()) {
            return ours;
        }
    }
    return "";
}

std::string sourceKind(const CatalogueApp& app) {
    return app.source == "flathub" ? "Flatpak app" : "System package";
}

std::string sourceRestart(const CatalogueApp& app) {
    // Nexus-CORE is image based: a package added to the system joins
    // the next deployment and starts working after a restart.
    return app.source == "flathub" ? "not needed" : "needed once";
}

SourceSuggestion suggestSource(const ShopEntry& entry) {
    const CatalogueApp* flathub = nullptr;
    const CatalogueApp* fedora = nullptr;

    for (const CatalogueApp* offer : entry.offers) {
        if (offer->source == "flathub") {
            flathub = offer;
        } else if (offer->source == "fedora") {
            fedora = offer;
        }
    }

    SourceSuggestion suggestion;

    if (flathub != nullptr && (flathub->verified || fedora == nullptr)) {
        suggestion.source = "flathub";
        if (flathub->verified) {
            const std::string who = flathub->developer.empty()
                ? "the developer" : flathub->developer;
            suggestion.reasons.push_back(
                "Published by " + who + " itself, as Flathub confirms");
        } else {
            suggestion.reasons.push_back(
                "Packaged for Flathub by volunteers, not by the "
                "developer; Flathub reviews it before listing it");
        }
        suggestion.reasons.push_back("Ready to open at once, no restart");
        suggestion.reasons.push_back("Leaves the system image as it is");
        suggestion.reasons.push_back("Removing it removes all of it");
        if (fedora != nullptr) {
            suggestion.otherNote =
                "The Fedora package is just as trustworthy, but it "
                "joins the system image: it starts working after the "
                "next restart, and every system update carries it.";
        }
        return suggestion;
    }

    if (fedora != nullptr) {
        suggestion.source = "fedora";
        suggestion.reasons.push_back(
            "Built and signed by Fedora's packagers");
        suggestion.reasons.push_back(
            "Part of the system: updated with it, kept in the boot "
            "menu with every earlier version");
        suggestion.reasons.push_back(
            "Starts working after the next restart");
        if (flathub != nullptr) {
            suggestion.otherNote =
                "Flathub has it too, packaged by volunteers rather "
                "than by the developer. It needs no restart; pick it "
                "if that matters more.";
        }
    }
    return suggestion;
}

}
