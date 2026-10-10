import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "."

// Search results or a category: a grid that only builds what is on
// screen, so a category of eight hundred games stays light.
Item {
    id: root
    property string title: ""
    property string subtitle: ""
    property var apps: []
    signal openApp(string key)

    ColumnLayout {
        anchors.fill: parent
        anchors.leftMargin: 32
        anchors.rightMargin: 16
        anchors.topMargin: 24
        spacing: 16

        ColumnLayout {
            spacing: 4
            Text {
                text: root.title
                color: Theme.bright
                font.pixelSize: 24
                font.bold: true
            }
            Text {
                text: root.subtitle
                color: Theme.dim
                font.pixelSize: 13
                visible: text.length > 0
            }
        }

        GridView {
            id: grid
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            readonly property int columns: Math.max(1, Math.floor(width / 276))
            cellWidth: Math.floor(width / columns)
            cellHeight: 148
            model: root.apps
            ScrollBar.vertical: ScrollBar {}
            Text {
                visible: root.apps.length === 0
                width: grid.width - 16
                text: "Nothing in the sources matches that. Try another word, or what the app does (“video editor”)."
                color: Theme.dim
                font.pixelSize: 14
                wrapMode: Text.WordWrap
            }
            delegate: Item {
                width: grid.cellWidth
                height: grid.cellHeight
                AppTile {
                    anchors.fill: parent
                    anchors.rightMargin: 16
                    anchors.bottomMargin: 16
                    app: modelData
                    onClicked: root.openApp(modelData.key)
                }
            }
        }

    }
}
