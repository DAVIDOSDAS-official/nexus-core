import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "."

ApplicationWindow {
    id: window
    width: 1120
    height: 740
    minimumWidth: 640
    minimumHeight: 480
    visible: true
    title: "Nexus Shop"
    color: Theme.bg

    property string section: "home"

    function goHome() {
        search.text = ""
        section = "home"
        stack.pop(null)
    }
    function openApp(key) {
        stack.push(appPage, { key: key })
    }
    function openCategory(name) {
        search.text = ""
        section = name
        stack.pop(null)
        stack.push(listPage, { title: name, subtitle: "Every app in Flathub and Fedora in this group, A to Z.", apps: shop.inCategory(name) })
    }
    function runSearch() {
        const words = search.text.trim()
        if (words.length === 0) {
            if (section === "search") goHome()
            return
        }
        const found = shop.search(words)
        const sub = found.length === 0 ? "" : "Searched Flathub and Fedora. Nothing from anywhere else."
        if (section === "search" && stack.depth > 1 && stack.currentItem.objectName === "results") {
            stack.currentItem.title = "“" + words + "”"
            stack.currentItem.apps = found
            stack.currentItem.subtitle = sub
        } else {
            section = "search"
            stack.pop(null)
            stack.push(listPage, { objectName: "results", title: "“" + words + "”", subtitle: sub, apps: found })
        }
    }

    Shortcut { sequence: "Esc"; onActivated: if (stack.depth > 1) stack.pop(); else window.goHome() }
    Shortcut { sequence: "Ctrl+F"; onActivated: search.forceActiveFocus() }

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // Sidebar.
        Rectangle {
            Layout.fillHeight: true
            Layout.preferredWidth: 232
            color: Theme.panel
            Rectangle { anchors.right: parent.right; width: 1; height: parent.height; color: Theme.line }

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 4

                RowLayout {
                    spacing: 10
                    Layout.bottomMargin: 18
                    Layout.leftMargin: 8
                    Canvas {
                        width: 26; height: 26
                        onPaint: {
                            const c = getContext("2d")
                            c.strokeStyle = "#f97316"; c.lineWidth = 2; c.lineJoin = "round"
                            c.beginPath()
                            c.moveTo(4,7); c.lineTo(13,3); c.lineTo(22,7); c.lineTo(22,19); c.lineTo(13,23); c.lineTo(4,19); c.closePath()
                            c.moveTo(4,7); c.lineTo(13,11); c.lineTo(22,7); c.moveTo(13,11); c.lineTo(13,23)
                            c.stroke()
                        }
                    }
                    Text { text: "Nexus Shop"; color: Theme.bright; font.pixelSize: 17; font.bold: true }
                }

                NavButton { text: "Home"; current: window.section === "home"; Layout.fillWidth: true; onClicked: window.goHome() }
                Text { text: "EXPLORE"; color: Theme.dim; font.pixelSize: 11; font.bold: true; Layout.topMargin: 14; Layout.leftMargin: 14; Layout.bottomMargin: 4 }
                Repeater {
                    model: shop.categories
                    NavButton { text: modelData; current: window.section === modelData; Layout.fillWidth: true; enabled: !shop.loading; onClicked: window.openCategory(modelData) }
                }

                Item { Layout.fillHeight: true }

                // Where the apps come from, and how fresh each list is.
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: sourcesColumn.implicitHeight + 24
                    radius: 10
                    color: Theme.panel2
                    ColumnLayout {
                        id: sourcesColumn
                        x: 12; y: 12
                        width: parent.width - 24
                        spacing: 6
                        Text { text: "Sources"; color: Theme.text; font.pixelSize: 13; font.bold: true }
                        Text { visible: shop.loading; text: "reading…"; color: Theme.dim; font.pixelSize: 12 }
                        Repeater {
                            model: shop.sources
                            ColumnLayout {
                                spacing: 0
                                Layout.fillWidth: true
                                RowLayout {
                                    spacing: 6
                                    Text { text: "●"; color: modelData.ready ? "#4ade80" : Theme.warn; font.pixelSize: 11 }
                                    Text { text: modelData.name; color: Theme.text; font.pixelSize: 12 }
                                }
                                Text { text: modelData.detail; color: Theme.dim; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true; leftPadding: 17 }
                            }
                        }
                    }
                }
            }
        }

        // Content.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 32
                Layout.rightMargin: 32
                Layout.topMargin: 24
                implicitHeight: 50
                radius: 14
                color: Theme.panel2
                border.color: search.activeFocus ? Theme.accent : Theme.lineStrong
                TextField {
                    id: search
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 18
                    placeholderText: shop.loading ? "Reading the app lists…" : "Search Flathub and Fedora: an app, or what you need (“video editor”)"
                    placeholderTextColor: Theme.dim
                    color: Theme.bright
                    font.pixelSize: 16
                    enabled: !shop.loading
                    background: Item {}
                    Accessible.name: "Search apps"
                    onTextChanged: typing.restart()
                    onAccepted: { typing.stop(); window.runSearch() }
                }
                Timer { id: typing; interval: 250; onTriggered: window.runSearch() }
            }

            StackView {
                id: stack
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                initialItem: HomePage {
                    onOpenApp: (key) => window.openApp(key)
                    onOpenCategory: (name) => window.openCategory(name)
                }

                pushEnter: shop.motion ? slideIn : null
                pushExit: shop.motion ? fadeOut : null
                popEnter: shop.motion ? fadeIn : null
                popExit: shop.motion ? slideOut : null
                replaceEnter: shop.motion ? slideIn : null
                replaceExit: shop.motion ? fadeOut : null
            }
        }
    }

    // Movement only where the desktop is meant to show off (KDE).
    Transition {
        id: slideIn
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 200; easing.type: Easing.OutCubic }
            NumberAnimation { property: "x"; from: 40; to: 0; duration: 220; easing.type: Easing.OutCubic }
        }
    }
    Transition {
        id: slideOut
        ParallelAnimation {
            NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 160 }
            NumberAnimation { property: "x"; from: 0; to: 40; duration: 180 }
        }
    }
    Transition { id: fadeIn; NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 200 } }
    Transition { id: fadeOut; NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 160 } }

    Component {
        id: listPage
        ListPage { onOpenApp: (key) => window.openApp(key) }
    }
    Component {
        id: appPage
        AppPage { onBack: stack.pop() }
    }
}
