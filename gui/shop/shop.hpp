#pragma once

#include <nexus/system/catalogue.hpp>

#include <QObject>
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

public:
    explicit Shop(QObject* parent = nullptr);
    ~Shop() override;

    void start();

    bool loading() const { return loading_; }
    QVariantList sources() const { return sources_; }
    bool motion() const { return motion_; }
    QStringList categories() const;

    Q_INVOKABLE QVariantList search(const QString& query) const;
    Q_INVOKABLE QVariantList inCategory(const QString& category) const;
    Q_INVOKABLE QVariantList picks() const;
    Q_INVOKABLE QVariantMap details(const QString& key) const;
    Q_INVOKABLE void copyText(const QString& text) const;
    Q_INVOKABLE void openLink(const QString& url) const;

signals:
    void changed();

private:
    struct Loaded {
        std::vector<nexus::system::CatalogueApp> apps;
        std::vector<nexus::system::ShopEntry> entries;
        QSet<QString> installedFlatpaks;
        QSet<QString> installedPackages;
        QVariantList sources;
    };

    void load();
    void finish(Loaded* loaded);

    QVariantMap summary(const nexus::system::ShopEntry& entry) const;
    QString iconFor(const nexus::system::CatalogueApp& app) const;
    bool installed(const nexus::system::CatalogueApp& app) const;
    const nexus::system::ShopEntry* find(const QString& key) const;

    bool loading_ = true;
    bool motion_ = false;
    QVariantList sources_;
    std::unique_ptr<Loaded> data_;
    std::thread worker_;

    QString fedoraFile_;
    QString fedoraIcons_;
    QString flathubDir_;
};
