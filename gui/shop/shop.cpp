#include "shop.hpp"

#include <nexus/system/compression.hpp>
#include <nexus/system/process.hpp>

#include <QClipboard>
#include <QDateTime>
#include <QDesktopServices>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QMetaObject>
#include <QUrl>

#include <algorithm>
#include <map>

using nexus::system::CatalogueApp;
using nexus::system::ShopEntry;

namespace {

QString q(const std::string& text) {
    return QString::fromStdString(text);
}

QString env(const char* name, const QString& fallback) {
    const QByteArray value = qgetenv(name);
    return value.isEmpty() ? fallback : QString::fromLocal8Bit(value);
}

QString sourceName(const std::string& source) {
    return source == "flathub" ? QStringLiteral("Flathub")
                               : QStringLiteral("Fedora");
}

// When a catalogue file was written, in words.
QString age(const QString& path) {
    const QFileInfo info(path);
    if (!info.exists()) {
        return {};
    }
    const QDateTime when = info.lastModified();
    const qint64 days = when.daysTo(QDateTime::currentDateTime());
    if (days == 0) {
        return QStringLiteral("today");
    }
    if (days == 1) {
        return QStringLiteral("yesterday");
    }
    return QLocale(QLocale::English).toString(when.date(), "d MMMM");
}

// The apps Home opens with. Chosen by the Nexus project, and said so
// on the page: there is no download count or rating behind them.
const std::vector<std::string>& pickKeys() {
    static const std::vector<std::string> keys = {
        "com.obsproject.studio", "org.videolan.vlc",
        "org.libreoffice.libreoffice", "org.geogebra.geogebra",
        "org.gimp.gimp", "org.kde.krita",
        "org.inkscape.inkscape", "org.kde.kdenlive",
    };
    return keys;
}

}

Shop::Shop(QObject* parent) : QObject(parent) {
    // Paths can be pointed elsewhere, for testing on another machine.
    fedoraFile_ = env("NEXUS_SHOP_FEDORA",
        "/usr/share/swcatalog/xml/fedora.xml.gz");
    fedoraIcons_ = env("NEXUS_SHOP_FEDORA_ICONS",
        "/usr/share/swcatalog/icons/fedora");
    flathubDir_ = env("NEXUS_SHOP_FLATHUB",
        "/var/lib/flatpak/appstream/flathub/x86_64/active");

    // Movement where the desktop is meant to show off (KDE), stillness
    // on minimal. NEXUS_SHOP_MOTION=1 or 0 decides it either way.
    const QByteArray forced = qgetenv("NEXUS_SHOP_MOTION");
    if (!forced.isEmpty()) {
        motion_ = forced == "1";
    } else {
        motion_ = qgetenv("XDG_CURRENT_DESKTOP").contains("KDE");
    }
}

Shop::~Shop() {
    if (worker_.joinable()) {
        worker_.join();
    }
}

void Shop::start() {
    // Reading 77 MB of XML takes a second or two on the Asus; the
    // window shows while it happens.
    worker_ = std::thread([this] { load(); });
}

void Shop::load() {
    auto loaded = std::make_unique<Loaded>();
    const QString flathubFile = flathubDir_ + "/appstream.xml.gz";

    // Flathub's catalogue is only downloaded when asked. Once a day is
    // enough: it is the list of apps, not the apps. No password: it
    // goes through Flatpak's own helper, which allows it.
    const QFileInfo flathubInfo(flathubFile);
    QString flathubNote;
    if (qgetenv("NEXUS_SHOP_NO_REFRESH").isEmpty() &&
        (!flathubInfo.exists() ||
         flathubInfo.lastModified().secsTo(QDateTime::currentDateTime()) >
             24 * 3600) &&
        nexus::system::commandExists("flatpak")) {
        const auto ran = nexus::system::runCommand(
            "flatpak update --appstream flathub --noninteractive", true,
            false);
        if (!ran.ran || ran.exitCode != 0) {
            flathubNote = QStringLiteral("could not refresh (offline?)");
        }
    }

    auto readOne = [&](const QString& path, const std::string& source,
                       const QString& label) {
        QVariantMap state;
        state["name"] = label;
        std::string document;
        std::string reason;
        if (!QFileInfo::exists(path)) {
            state["ready"] = false;
            state["detail"] = source == "flathub"
                ? QStringLiteral("list not downloaded yet")
                : QStringLiteral("list not installed");
            loaded->sources.push_back(state);
            return;
        }
        if (!nexus::system::readPossiblyCompressed(
                path.toStdString(), document, reason)) {
            state["ready"] = false;
            state["detail"] = q(reason);
            loaded->sources.push_back(state);
            return;
        }
        std::string error;
        auto apps = nexus::system::parseCatalogue(document, source, error);
        document.clear();
        document.shrink_to_fit();

        state["ready"] = error.empty();
        QString detail = QString::number(apps.size()) +
            QStringLiteral(" apps, list from ") + age(path);
        if (!error.empty()) {
            detail = QStringLiteral("list damaged: ") + q(error);
        } else if (source == "flathub" && !flathubNote.isEmpty()) {
            detail += ", " + flathubNote;
        }
        state["detail"] = detail;
        loaded->sources.push_back(state);
        for (auto& app : apps) {
            loaded->apps.push_back(std::move(app));
        }
    };

    readOne(flathubFile, "flathub", QStringLiteral("Flathub"));
    readOne(fedoraFile_, "fedora", QStringLiteral("Fedora"));

    // Grouped only after every app is in place: the entries point
    // into the vector, which must not grow afterwards.
    loaded->entries = nexus::system::groupCatalogue(loaded->apps);

    // What is already here, to say "Installed" instead of offering it.
    const auto flatpaks = nexus::system::runCommand(
        "flatpak list --app --columns=application 2>/dev/null", false);
    for (const auto& line : flatpaks.lines) {
        loaded->installedFlatpaks.insert(q(line).trimmed());
    }
    const auto packages = nexus::system::runCommand(
        "rpm -qa --qf '%{NAME}\\n' 2>/dev/null", false);
    for (const auto& line : packages.lines) {
        loaded->installedPackages.insert(q(line).trimmed());
    }

    Loaded* handed = loaded.release();
    QMetaObject::invokeMethod(this, [this, handed] { finish(handed); },
                              Qt::QueuedConnection);
}

void Shop::finish(Loaded* loaded) {
    data_.reset(loaded);
    sources_ = data_->sources;
    loading_ = false;
    emit changed();
}

QStringList Shop::categories() const {
    QStringList names;
    for (const auto& name : nexus::system::shopCategories()) {
        names << q(name);
    }
    return names;
}

QString Shop::iconFor(const CatalogueApp& app) const {
    if (app.icon.empty() || app.iconSize == 0) {
        return {};
    }
    const QString size = QString::number(app.iconSize) + "x" +
                         QString::number(app.iconSize);
    const QString base = app.source == "flathub"
        ? flathubDir_ + "/icons" : fedoraIcons_;
    const QString path = base + "/" + size + "/" + q(app.icon);
    return QFileInfo::exists(path)
        ? QUrl::fromLocalFile(path).toString() : QString();
}

bool Shop::installed(const CatalogueApp& app) const {
    if (!data_) {
        return false;
    }
    return app.source == "flathub"
        ? data_->installedFlatpaks.contains(q(app.package))
        : data_->installedPackages.contains(q(app.package));
}

QVariantMap Shop::summary(const ShopEntry& entry) const {
    QVariantMap item;
    const CatalogueApp& first = entry.first();
    QStringList sources;
    bool verified = false;
    bool isInstalled = false;
    QString icon;

    for (const CatalogueApp* offer : entry.offers) {
        sources << sourceName(offer->source);
        verified = verified || offer->verified;
        isInstalled = isInstalled || installed(*offer);
        if (icon.isEmpty()) {
            icon = iconFor(*offer);
        }
    }

    item["key"] = q(entry.key);
    item["name"] = q(first.name);
    item["summary"] = q(first.summary);
    item["icon"] = icon;
    item["sources"] = sources.join(QStringLiteral(" · "));
    item["sourceCount"] = static_cast<int>(entry.offers.size());
    item["verified"] = verified;
    item["installed"] = isInstalled;
    return item;
}

const ShopEntry* Shop::find(const QString& key) const {
    if (!data_) {
        return nullptr;
    }
    const std::string wanted = key.toStdString();
    for (const ShopEntry& entry : data_->entries) {
        if (entry.key == wanted) {
            return &entry;
        }
    }
    return nullptr;
}

QVariantList Shop::search(const QString& query) const {
    QVariantList list;
    if (!data_) {
        return list;
    }
    for (std::size_t index : nexus::system::searchCatalogue(
             data_->entries, query.toStdString(), 80)) {
        list << summary(data_->entries[index]);
    }
    return list;
}

QVariantList Shop::inCategory(const QString& category) const {
    QVariantList list;
    if (!data_) {
        return list;
    }
    const std::string wanted = category.toStdString();
    std::vector<const ShopEntry*> found;
    for (const ShopEntry& entry : data_->entries) {
        if (nexus::system::shopCategory(entry.first().categories) == wanted) {
            found.push_back(&entry);
        }
    }
    // Alphabetical: there is no download count or rating to rank by,
    // and inventing one would be the first untrue thing in the Shop.
    std::sort(found.begin(), found.end(),
        [](const ShopEntry* a, const ShopEntry* b) {
            return QString::compare(q(a->first().name), q(b->first().name),
                                    Qt::CaseInsensitive) < 0;
        });
    for (const ShopEntry* entry : found) {
        list << summary(*entry);
    }
    return list;
}

QVariantList Shop::picks() const {
    QVariantList list;
    for (const std::string& key : pickKeys()) {
        if (const ShopEntry* entry = find(q(key))) {
            list << summary(*entry);
        }
    }
    return list;
}

QVariantMap Shop::details(const QString& key) const {
    QVariantMap map;
    const ShopEntry* entry = find(key);
    if (entry == nullptr) {
        return map;
    }

    map = summary(*entry);

    // Text comes from whichever source has the most of it.
    const CatalogueApp* richest = &entry->first();
    for (const CatalogueApp* offer : entry->offers) {
        if (offer->description.size() > richest->description.size()) {
            richest = offer;
        }
    }
    map["description"] = q(richest->description);

    QString developer;
    QString homepage;
    QString license;
    QStringList screenshots;
    for (const CatalogueApp* offer : entry->offers) {
        if (developer.isEmpty()) developer = q(offer->developer);
        if (homepage.isEmpty()) homepage = q(offer->homepage);
        if (license.isEmpty()) license = q(offer->license);
        if (screenshots.isEmpty()) {
            for (const auto& shot : offer->screenshots) {
                screenshots << q(shot);
            }
        }
    }
    map["developer"] = developer;
    map["homepage"] = homepage;
    map["license"] = license;
    map["screenshots"] = screenshots;

    const auto suggestion = nexus::system::suggestSource(*entry);
    QStringList reasons;
    for (const auto& reason : suggestion.reasons) {
        reasons << q(reason);
    }
    map["suggested"] = sourceName(suggestion.source);
    map["reasons"] = reasons;
    map["otherNote"] = q(suggestion.otherNote);

    QVariantList offers;
    for (const CatalogueApp* offer : entry->offers) {
        QVariantMap row;
        const bool flathub = offer->source == "flathub";
        row["source"] = sourceName(offer->source);
        row["suggested"] = offer->source == suggestion.source;
        row["kind"] = q(nexus::system::sourceKind(*offer));
        row["restart"] = q(nexus::system::sourceRestart(*offer));
        row["verified"] = offer->verified;
        row["who"] = flathub
            ? (offer->verified
                   ? QStringLiteral("the developer, confirmed by Flathub")
                   : QStringLiteral("volunteers, reviewed by Flathub"))
            : QStringLiteral("Fedora's packagers, signed by Fedora");
        row["installed"] = installed(*offer);
        // What to type until the Shop installs by itself (step 2).
        // Fedora packages go through nexus, which shows its plan and
        // changes nothing without --apply.
        row["command"] = flathub
            ? QStringLiteral("flatpak install flathub ") + q(offer->package)
            : QStringLiteral("nexus install ") + q(offer->package);
        offers << row;
    }
    map["offers"] = offers;
    return map;
}

void Shop::copyText(const QString& text) const {
    QGuiApplication::clipboard()->setText(text);
}

void Shop::openLink(const QString& url) const {
    const QUrl target(url);
    // Only web pages: a catalogue is someone else's text.
    if (target.scheme() == "https" || target.scheme() == "http") {
        QDesktopServices::openUrl(target);
    }
}
