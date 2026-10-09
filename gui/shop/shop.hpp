#pragma once

#include <nexus/system/catalogue.hpp>

#include <QHash>
#include <QObject>
#include <QProcess>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <memory>
#include <thread>
#include <vector>

// What the window asks; everything it shows comes from here.
//
// Step 1 of Nexus Shop only reads: the two catalogues, what is
// installed, and what Nexus suggests. Installing is step 2, through
// `nexus install`, so the history and the rules stay in one place.
class Shop : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool loading READ loading NOTIFY changed)
    Q_PROPERTY(QVariantList sources READ sources NOTIFY changed)
    Q_PROPERTY(bool motion READ motion CONSTANT)
    Q_PROPERTY(QStringList categories READ categories CONSTANT)
    Q_PROPERTY(QVariantMap updates READ updates NOTIFY updatesChanged)
    Q_PROPERTY(int updateCount READ updateCount NOTIFY updatesChanged)
    Q_PROPERTY(bool jobRunning READ jobRunning NOTIFY jobChanged)
    Q_PROPERTY(QString jobTitle READ jobTitle NOTIFY jobChanged)
    Q_PROPERTY(QString jobLog READ jobLog NOTIFY jobChanged)
    Q_PROPERTY(QString jobResult READ jobResult NOTIFY jobChanged)
    Q_PROPERTY(QString startPage READ startPage CONSTANT)

public:
    explicit Shop(QObject* parent = nullptr);
    ~Shop() override;

    void start();

    bool loading() const { return loading_; }
    QVariantList sources() const { return sources_; }
    bool motion() const { return motion_; }
    QStringList categories() const;
    QVariantMap updates() const { return updates_; }
    int updateCount() const;
    bool jobRunning() const { return job_ != nullptr; }
    QString jobTitle() const { return jobTitle_; }
    QString jobLog() const { return jobLog_; }
    QString jobResult() const { return jobResult_; }
    QString startPage() const { return startPage_; }
    void setStartPage(const QString& page) { startPage_ = page; }

    Q_INVOKABLE QVariantList search(const QString& query) const;
    Q_INVOKABLE QVariantList inCategory(const QString& category) const;
    Q_INVOKABLE QVariantList picks() const;
    Q_INVOKABLE QVariantMap details(const QString& key) const;
    Q_INVOKABLE QVariantList installedApps() const;
    Q_INVOKABLE QVariantList history() const;

    // Changes. Each goes through nexus, so it is written in history and
    // follows the same rules as at a terminal.
    Q_INVOKABLE void checkUpdates();
    Q_INVOKABLE void install(const QString& appId);
    Q_INVOKABLE void remove(const QString& appId);
    Q_INVOKABLE void updateEverything();
    Q_INVOKABLE void clearJob();
    Q_INVOKABLE void launch(const QString& appId) const;

    Q_INVOKABLE void copyText(const QString& text) const;
    Q_INVOKABLE void openLink(const QString& url) const;

signals:
    void changed();
    void updatesChanged();
    void jobChanged();

private:
    struct Loaded {
        std::vector<nexus::system::CatalogueApp> apps;
        std::vector<nexus::system::ShopEntry> entries;
        QSet<QString> installedFlatpaks;
        QHash<QString, QString> flatpakNames;
        QSet<QString> installedPackages;
        QVariantList sources;
    };

    void load();
    void finish(Loaded* loaded);
    void runJob(const QString& title, const QString& program,
                const QStringList& arguments);
    void readInstalled(QSet<QString>& flatpaks,
                       QHash<QString, QString>& names,
                       QSet<QString>& packages) const;

    QVariantMap summary(const nexus::system::ShopEntry& entry) const;
    QString iconFor(const nexus::system::CatalogueApp& app) const;
    bool installed(const nexus::system::CatalogueApp& app) const;
    const nexus::system::ShopEntry* find(const QString& key) const;

    bool loading_ = true;
    bool motion_ = false;
    QVariantList sources_;
    std::unique_ptr<Loaded> data_;
    std::thread worker_;
    std::thread checker_;
    std::atomic<bool> checking_{false};

    QVariantMap updates_;
    QProcess* job_ = nullptr;
    QString jobTitle_;
    QString jobLog_;
    QString jobResult_;
    QString jobKind_;
    QString startPage_ = QStringLiteral("home");

    QString fedoraFile_;
    QString fedoraIcons_;
    QString flathubDir_;
};
