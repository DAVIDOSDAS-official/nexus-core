import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "."

// One app in a grid: Home's picks, a category, search results.
AbstractButton {
    id: root
    property var app: ({})
    hoverEnabled: true
    implicitHeight: 132

    Accessible.name: (app.name || "") + ", " + (app.summary || "")

    scale: shop.motion && hovered ? 1.02 : 1.0
    Behavior on scale { NumberAnimation { duration: shop.motion ? 140 : 0; easing.type: Easing.OutCubic } }

    background: Rectangle {
        radius: 14
        color: root.hovered ? Theme.panel2 : Theme.panel
        border.color: root.activeFocus ? Theme.accent : (root.hovered ? Theme.lineStrong : Theme.line)
        border.width: root.activeFocus ? 2 : 1
        Behavior on color { ColorAnimation { duration: shop.motion ? 140 : 0 } }
    }

    contentItem: ColumnLayout {
        spacing: 10
        RowLayout {
            spacing: 12
            Layout.fillWidth: true
            AppIcon { source: root.app.icon || ""; name: root.app.name || ""; size: 48 }
            ColumnLayout {
                spacing: 2
                Layout.fillWidth: true
                Text {
                    text: root.app.name || ""
                    color: Theme.bright
                    font.pixelSize: 15
                    font.bold: true
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Text {
                    text: root.app.summary || ""
                    color: Theme.dim
                    font.pixelSize: 13
                    elide: Text.ElideRight
                    maximumLineCount: 2
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }
        Flow {
            spacing: 6
            Layout.fillWidth: true
            Chip { text: root.app.sources || "" }
            Chip { visible: root.app.verified === true; text: "verified"; good: true }
            Chip { visible: root.app.installed === true; text: "Installed"; strong: true }
        }
    }
    padding: 16
}
