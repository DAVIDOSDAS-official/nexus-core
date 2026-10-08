#include "shop.hpp"

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QTimer>

#include <cstdio>

// Nexus Shop: apps from Flathub and Fedora, side by side, with what
// each one would do to this computer. The window; shop.cpp does the
// reading.
int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName("Nexus Shop");
    QGuiApplication::setDesktopFileName("nexus-shop");

    Shop shop;
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("shop", &shop);
    engine.load(QUrl("qrc:/shop/qml/Main.qml"));
    if (engine.rootObjects().isEmpty()) {
        std::fprintf(stderr, "nexus-shop: the window could not be built\n");
        return 1;
    }
    shop.start();

    // For checking the look without a screen: NEXUS_SHOP_SCREENSHOT=
    // file.png saves the window once the lists are read, then quits.
    // NEXUS_SHOP_OPEN=<app id> opens that app's page first.
    const QString shot = qEnvironmentVariable("NEXUS_SHOP_SCREENSHOT");
    if (!shot.isEmpty()) {
        auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
        QObject::connect(&shop, &Shop::changed, window, [window, shot] {
            const QString open = qEnvironmentVariable("NEXUS_SHOP_OPEN");
            const QString find = qEnvironmentVariable("NEXUS_SHOP_SEARCH");
            if (!open.isEmpty()) {
                QMetaObject::invokeMethod(window, "openApp", Q_ARG(QVariant, open));
            } else if (!find.isEmpty()) {
                QObject* field = nullptr;
                for (QObject* child : window->findChildren<QObject*>()) {
                    if (child->property("placeholderText").isValid() &&
                        child->property("text").isValid()) {
                        field = child;
                        break;
                    }
                }
                if (field) field->setProperty("text", find);
            }
            QTimer::singleShot(1500, window, [window, shot] {
                window->grabWindow().save(shot);
                QGuiApplication::quit();
            });
        });
    }
    return app.exec();
}
