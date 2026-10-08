import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "."

Flickable {
    id: root
    signal openApp(string key)
    signal openCategory(string name)

    contentWidth: width
    contentHeight: column.implicitHeight + 64
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar {}

    property var picks: shop.loading ? [] : shop.picks()
    readonly property int columns: Math.max(1, Math.floor((width - 64 + 16) / 260))

    ColumnLayout {
        id: column
        x: 32
        y: 24
        width: root.width - 64
        spacing: 28

        // While the lists are read.
        Rectangle {
            visible: shop.loading
            Layout.fillWidth: true
            implicitHeight: 64
            radius: 14
            color: Theme.panel2
            border.color: Theme.line
            RowLayout {
                anchors.fill: parent
                anchors.margins: 20
                spacing: 14
                BusyIndicator { running: shop.loading; implicitWidth: 28; implicitHeight: 28 }
                Text {
                    text: "Reading the app lists from Flathub and Fedora…"
                    color: Theme.text
                    font.pixelSize: 15
                    Layout.fillWidth: true
                }
            }
        }

        ColumnLayout {
            spacing: 6
            Layout.fillWidth: true
            visible: !shop.loading
            Text {
                text: "Nexus picks"
                color: Theme.bright
                font.pixelSize: 22
                font.bold: true
            }
            Text {
                text: "Chosen by the Nexus project, not ranked by downloads or ratings."
                color: Theme.dim
                font.pixelSize: 13
            }
        }

        GridLayout {
            visible: !shop.loading
            Layout.fillWidth: true
            columns: root.columns
            columnSpacing: 16
            rowSpacing: 16
            Repeater {
                model: root.picks
                AppTile {
                    app: modelData
                    Layout.fillWidth: true
                    Layout.preferredWidth: 240
                    onClicked: root.openApp(modelData.key)
                }
            }
        }

        Text {
            text: "Explore"
            color: Theme.bright
            font.pixelSize: 22
            font.bold: true
            visible: !shop.loading
        }

        GridLayout {
            visible: !shop.loading
            Layout.fillWidth: true
            columns: Math.max(1, Math.floor((root.width - 64 + 12) / 200))
            columnSpacing: 12
            rowSpacing: 12
            Repeater {
                model: shop.categories
                AbstractButton {
                    id: cat
                    hoverEnabled: true
                    Layout.fillWidth: true
                    Layout.preferredWidth: 188
                    implicitHeight: 60
                    text: modelData
                    onClicked: root.openCategory(modelData)
                    scale: shop.motion && hovered ? 1.03 : 1.0
                    Behavior on scale { NumberAnimation { duration: shop.motion ? 140 : 0 } }
                    background: Rectangle {
                        radius: 12
                        color: cat.hovered ? Theme.panel2 : Theme.panel
                        border.color: cat.activeFocus ? Theme.accent : Theme.line
                    }
                    contentItem: Text {
                        text: cat.text
                        color: Theme.bright
                        font.pixelSize: 15
                        font.bold: true
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: 18
                    }
                }
            }
        }
    }
}
