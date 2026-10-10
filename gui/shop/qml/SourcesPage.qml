import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "."

// Where the apps come from: known sources only, each said plainly.
Flickable {
    id: root
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
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 16
            ColumnLayout {
                spacing: 4
                Layout.fillWidth: true
                Text { text: "Sources"; color: Theme.bright; font.pixelSize: 28; font.bold: true }
                Text {
                    text: "Nexus Shop shows apps from these sources and nowhere else. None can be added from here: a source Nexus does not know is not one it can vouch for."
                    color: Theme.dim
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
            Button2 {
                text: shop.loading ? "Reading…" : "Refresh lists"
                enabled: !shop.loading
                onClicked: shop.refreshLists()
            }
        }

        Repeater {
            model: shop.sources
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: card.implicitHeight + 36
                radius: 14
                color: Theme.panel
                border.color: Theme.line
                ColumnLayout {
                    id: card
                    x: 20; y: 18
                    width: parent.width - 40
                    spacing: 8
                    RowLayout {
                        spacing: 10
                        Text { text: "●"; color: modelData.ready ? "#4ade80" : Theme.warn; font.pixelSize: 13 }
                        Text { text: modelData.name; color: Theme.bright; font.pixelSize: 18; font.bold: true }
                        Chip { text: modelData.ready ? "available" : "not available"; good: modelData.ready; strong: !modelData.ready }
                    }
                    Text {
                        text: modelData.about || ""
                        visible: text.length > 0
                        color: Theme.text
                        font.pixelSize: 14
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                    Text {
                        text: modelData.detail || ""
                        color: Theme.dim
                        font.pixelSize: 13
                        wrapMode: Text.WordWrap
                        Layout.fillWidth: true
                    }
                }
            }
        }

        Text {
            Layout.topMargin: 8
            text: "Not here on purpose: installers from websites, and anything that asks you to add a source by hand. Fedora's own Flatpaks are not listed yet: their app list is not on this computer in a form the Shop can read."
            color: Theme.dim
            font.pixelSize: 12
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }
}
