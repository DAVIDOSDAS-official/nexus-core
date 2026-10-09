import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "."

// Every change Nexus made, from the Shop or from a terminal: the same
// records as `nexus history`, newest first.
Flickable {
    id: root
    property var records: shop.history()
    Connections { target: shop; function onJobChanged() { if (!shop.jobRunning) root.records = shop.history() } }

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
        spacing: 10

        Text { text: "History"; color: Theme.bright; font.pixelSize: 28; font.bold: true }
        Text {
            text: "Every change Nexus made to this computer's software, from here or from a terminal. The same list as  nexus history."
            color: Theme.dim
            font.pixelSize: 13
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            Layout.bottomMargin: 8
        }
        Text {
            visible: root.records.length === 0
            text: "Nothing recorded yet."
            color: Theme.dim
            font.pixelSize: 14
        }
        Repeater {
            model: root.records
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: row.implicitHeight + 28
                radius: 12
                color: Theme.panel
                border.color: Theme.line
                RowLayout {
                    id: row
                    x: 16; y: 14
                    width: parent.width - 32
                    spacing: 16
                    Text {
                        text: modelData.when
                        color: Theme.dim
                        font.family: Theme.mono
                        font.pixelSize: 12
                        Layout.preferredWidth: 150
                        Layout.alignment: Qt.AlignTop
                    }
                    ColumnLayout {
                        spacing: 2
                        Layout.fillWidth: true
                        Text { text: modelData.what; color: Theme.bright; font.pixelSize: 15; font.bold: true; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        Text { text: modelData.resolved; visible: text.length > 0; color: Theme.dim; font.pixelSize: 13; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    }
                    Chip { text: modelData.outcome || "?"; good: modelData.ok; strong: !modelData.ok; Layout.alignment: Qt.AlignTop }
                }
            }
        }
    }
}
