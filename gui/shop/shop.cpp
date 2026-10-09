#include "shop.hpp"

#include <nexus/system/compression.hpp>
#include <nexus/system/process.hpp>
#include <nexus/system/transaction_log.hpp>
#include <nexus/system/update.hpp>

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

#include <malloc.h>

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
    if (checker_.joinable()) {
        checker_.join();
    }
    if (job_ != nullptr) {
        // A change in progress is left to finish on its own: killing
        // flatpak half-way is worse than letting it complete.
        job_->disconnect(this);
        job_->setParent(nullptr);
    }
}

void Shop::readInstalled(QSet<QString>& flatpaks,
                         QHash<QString, QString>& names,
                         QSet<QString>& packages) const {
    flatpaks.clear();
    names.clear();
    packages.clear();
    const auto listed = nexus::system::runCommand(
        "flatpak list --app --columns=application,name 2>/dev/null", false);
    for (const auto& line : listed.lines) {
        const QStringList fields = q(line).split('\t');
        const QString id = fields.value(0).trimmed();
        if (id.isEmpty()) {
            continue;
        }
        flatpaks.insert(id);
        names.insert(id, fields.value(1).trimmed());
    }
    const auto rpms = nexus::system::runCommand(
        "rpm -qa --qf '%{NAME}\\n' 2>/dev/null", false);
    for (const auto& line : rpms.lines) {
        packages.insert(q(line).trimmed());
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
    readInstalled(loaded->installedFlatpaks, loaded->flatpakNames,
                  loaded->installedPackages);

    Loaded* handed = loaded.release();
    QMetaObject::invokeMethod(this, [this, handed] { finish(handed); },
                              Qt::QueuedConnection);
}

void Shop::finish(Loaded* loaded) {
    data_.reset(loaded);
    sources_ = data_->sources;
    loading_ = false;
    // Reading the lists held 77 MB of XML for a second; hand the freed
    // memory back to the system rather than keep it for nothing.
    ::malloc_trim(0);
    emit changed();
    checkUpdates();
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
        row["appId"] = flathub ? q(offer->package) : QString();
        row["flathub"] = flathub;
        // What to type until the Shop installs by itself (step 2).
        // Fedora packages go through nexus, which shows its plan and
        // changes nothing without --apply.
        row["command"] = flathub
            ? QStringLiteral("nexus app install ") + q(offer->package) +
                  QStringLiteral(" --apply")
            : QStringLiteral("sudo nexus install ") + q(offer->package) +
                  QStringLiteral(" --apply");
        offers << row;
    }
    map["offers"] = offers;
    return map;
}

// ---------------------------------------------------------------- updates

int Shop::updateCount() const {
    int count = updates_.value("apps").toList().size();
    if (updates_.value("system").toString() == "new") {
        count += 1;
    }
    return count;
}

void Shop::checkUpdates() {
    if (checking_.exchange(true)) {
        return;
    }
    if (checker_.joinable()) {
        checker_.join();
    }
    QVariantMap state = updates_;
    state["state"] = QStringLiteral("checking");
    updates_ = state;
    emit updatesChanged();

    // The same check as `nexus update`, so the Shop and the terminal
    // can never disagree. A few seconds: it asks the registry.
    checker_ = std::thread([this] {
        const auto ran = nexus::system::runCommand(
            "nexus update --lines 2>/dev/null", false, false);
        auto* lines = new nexus::system::UpdateLines(
            nexus::system::parseUpdateLines(ran.text));
        const bool ok = ran.ran && ran.exitCode == 0;
        QMetaObject::invokeMethod(this, [this, lines, ok] {
            std::unique_ptr<nexus::system::UpdateLines> owned(lines);
            QVariantMap state;
            state["state"] = ok ? QStringLiteral("ready")
                                : QStringLiteral("error");
            state["running"] = q(owned->running);
            state["runningDay"] = q(owned->runningDay);
            state["staged"] = q(owned->staged);
            state["system"] = q(owned->system);
            state["version"] = q(owned->version);
            state["day"] = q(owned->day);
            state["diff"] = q(owned->diff);
            QVariantList apps;
            for (const auto& app : owned->apps) {
                QVariantMap row;
                const QString id = q(app.id);
                row["id"] = id;
                row["version"] = q(app.version);
                row["name"] = id;
                row["icon"] = QString();
                if (const ShopEntry* entry = find(id.toLower())) {
                    const QVariantMap brief = summary(*entry);
                    row["name"] = brief.value("name");
                    row["icon"] = brief.value("icon");
                } else if (data_ && data_->flatpakNames.contains(id)) {
                    row["name"] = data_->flatpakNames.value(id);
                }
                apps << row;
            }
            state["apps"] = apps;
            updates_ = state;
            checking_ = false;
            emit updatesChanged();
        }, Qt::QueuedConnection);
    });
}

// ------------------------------------------------------------------ jobs

void Shop::runJob(const QString& title, const QString& program,
                  const QStringList& arguments) {
    if (job_ != nullptr) {
        return;  // one change at a time
    }
    jobTitle_ = title;
    jobLog_.clear();
    jobResult_.clear();

    job_ = new QProcess(this);
    job_->setProcessChannelMode(QProcess::MergedChannels);
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert("LC_ALL", "C.UTF-8");
    job_->setProcessEnvironment(environment);

    connect(job_, &QProcess::readyRead, this, [this] {
        // Flatpak redraws its progress line with \r; each redraw is
        // shown as the newest line.
        QString text = QString::fromLocal8Bit(job_->readAll());
        text.replace('\r', '\n');
        jobLog_ += text;
        QStringList lines = jobLog_.split('\n');
        lines.removeAll(QString());
        if (lines.size() > 200) {
            lines = lines.mid(lines.size() - 200);
        }
        jobLog_ = lines.join('\n');
        emit jobChanged();
    });
    connect(job_, &QProcess::finished, this,
            [this](int code, QProcess::ExitStatus status) {
        const bool ok = status == QProcess::NormalExit && code == 0;
        // pkexec answers 126 when the password window was closed.
        if (code == 126 || code == 127) {
            jobResult_ = QStringLiteral("cancelled");
        } else {
            jobResult_ = ok ? QStringLiteral("done")
                            : QStringLiteral("failed");
        }
        job_->deleteLater();
        job_ = nullptr;
        if (data_) {
            readInstalled(data_->installedFlatpaks, data_->flatpakNames,
                          data_->installedPackages);
        }
        emit jobChanged();
        emit changed();
        checkUpdates();
    });
    connect(job_, &QProcess::errorOccurred, this,
            [this](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || job_ == nullptr) {
            return;
        }
        jobLog_ += QStringLiteral("\nCould not start: ") + job_->program();
        jobResult_ = QStringLiteral("failed");
        job_->deleteLater();
        job_ = nullptr;
        emit jobChanged();
    });

    emit jobChanged();
    job_->start(program, arguments);
}

void Shop::install(const QString& appId) {
    if (!nexus::system::isFlatpakId(appId.toStdString())) {
        return;
    }
    runJob(QStringLiteral("Installing ") + appId, QStringLiteral("nexus"),
           {"app", "install", appId, "--apply"});
}

void Shop::remove(const QString& appId) {
    if (!nexus::system::isFlatpakId(appId.toStdString())) {
        return;
    }
    runJob(QStringLiteral("Removing ") + appId, QStringLiteral("nexus"),
           {"app", "remove", appId, "--apply"});
}

void Shop::updateEverything() {
    // The system half needs root; pkexec asks for the password in a
    // window, the same as sudo asks in a terminal.
    runJob(QStringLiteral("Updating"), QStringLiteral("pkexec"),
           {"nexus", "update", "--apply", "--yes"});
}

void Shop::clearJob() {
    if (job_ != nullptr) {
        return;
    }
    jobTitle_.clear();
    jobLog_.clear();
    jobResult_.clear();
    emit jobChanged();
}

void Shop::launch(const QString& appId) const {
    if (nexus::system::isFlatpakId(appId.toStdString())) {
        QProcess::startDetached(QStringLiteral("flatpak"),
                                {"run", appId});
    }
}

// ------------------------------------------------------- installed, history

QVariantList Shop::installedApps() const {
    QVariantList list;
    if (!data_) {
        return list;
    }
    QStringList ids = data_->installedFlatpaks.values();
    std::sort(ids.begin(), ids.end(), [this](const QString& a, const QString& b) {
        return QString::compare(data_->flatpakNames.value(a, a),
                                data_->flatpakNames.value(b, b),
                                Qt::CaseInsensitive) < 0;
    });
    for (const QString& id : ids) {
        QVariantMap item;
        if (const ShopEntry* entry = find(id.toLower())) {
            item = summary(*entry);
        } else {
            item["key"] = QString();
            item["name"] = data_->flatpakNames.value(id, id);
            item["summary"] = QString();
            item["icon"] = QString();
        }
        item["appId"] = id;
        list << item;
    }
    return list;
}

QVariantList Shop::history() const {
    QVariantList list;
    auto records = nexus::system::readAllTransactions();
    std::sort(records.begin(), records.end(),
              [](const auto& a, const auto& b) { return a.when > b.when; });
    for (const auto& record : records) {
        QVariantMap row;
        const QDateTime when =
            QDateTime::fromString(q(record.when), Qt::ISODate).toLocalTime();
        row["when"] = when.isValid()
            ? QLocale(QLocale::English).toString(when, "d MMMM, HH:mm")
            : q(record.when);
        row["what"] = q(record.request);
        row["resolved"] = q(record.resolved);
        row["outcome"] = record.unfinished ? QStringLiteral("never finished")
                                           : q(record.outcome);
        row["ok"] = record.succeeded && !record.unfinished;
        list << row;
    }
    return list;
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
