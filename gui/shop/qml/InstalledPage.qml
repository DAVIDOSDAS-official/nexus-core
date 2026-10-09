import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "."

// Flatpak apps on this computer, with Open and Remove.
Flickable {
    id: root
    signal openApp(string key)
    signal removeApp(string appId, string name)
    property var apps: shop.installedApps()
    Connections { target: shop; function onChanged() { root.apps = shop.installedApps() } }

    contentWidth: width
    contentHeight: column.implicitHeight + 64
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar {}

    ColumnLayout {
        id: column
        x: 32
        y: 24
        width: Math.min(root.width - 64, 900)
        spacing: 12

        Text {
            text: root.apps.length === 1 ? "1 app installed" : root.apps.length + " apps installed"
            color: Theme.bright
            font.pixelSize: 28
            font.bold: true
        }
        Text {
            text: "Apps from Flathub. Programs that are part of the system itself are updated with it, under Updates."
            color: Theme.dim
            font.pixelSize: 13
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Layout.bottomMargin: 8
        }
        Repeater {
            model: root.apps
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 72
                radius: 12
                color: Theme.panel
                border.color: Theme.line
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    spacing: 14
                    AppIcon { source: modelData.icon || ""; name: modelData.name || ""; size: 40 }
                    ColumnLayout {
                        spacing: 0
                        Layout.fillWidth: true
                        Text { text: modelData.name; color: Theme.bright; font.pixelSize: 15; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                        Text { text: modelData.summary || modelData.appId; color: Theme.dim; font.pixelSize: 13; elide: Text.ElideRight; Layout.fillWidth: true }
                    }
                    Button2 { text: "Details"; visible: (modelData.key || "").length > 0; onClicked: root.openApp(modelData.key) }
                    Button2 { text: "Open"; onClicked: shop.launch(modelData.appId) }
                    Button2 { text: "Remove"; danger: true; enabled: !shop.jobRunning; onClicked: root.removeApp(modelData.appId, modelData.name) }
                }
            }
        }
    }
}
