import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "."

// What a change is doing, in the tools' own words, while it runs and
// after. Stays until closed when it is over, so a failure is read.
Rectangle {
    id: root
    visible: shop.jobRunning || shop.jobResult.length > 0
    implicitHeight: visible ? column.implicitHeight + 28 : 0
    color: Theme.panel
    Rectangle { width: parent.width; height: 1; color: Theme.line }

    readonly property var lastLines: {
        const all = shop.jobLog.split("\n")
        return all.slice(Math.max(0, all.length - 6)).join("\n")
    }

    ColumnLayout {
        id: column
        x: 32
        y: 14
        width: parent.width - 64
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            BusyIndicator { visible: shop.jobRunning; running: shop.jobRunning; implicitWidth: 24; implicitHeight: 24 }
            Text {
                text: shop.jobRunning ? shop.jobTitle + "…"
                    : shop.jobResult === "done" ? shop.jobTitle + ": done"
                    : shop.jobResult === "cancelled" ? shop.jobTitle + ": cancelled, nothing changed"
                    : shop.jobTitle + ": did not complete"
                color: shop.jobResult === "failed" ? "#fca5a5"
                     : shop.jobResult === "done" ? Theme.ok : Theme.bright
                font.pixelSize: 15
                font.bold: true
                Layout.fillWidth: true
            }
            Button2 {
                visible: !shop.jobRunning
                text: "Close"
                onClicked: shop.clearJob()
            }
        }
        Text {
            text: root.lastLines
            visible: text.length > 0
            color: Theme.dim
            font.family: Theme.mono
            font.pixelSize: 12
            wrapMode: Text.WrapAnywhere
            Layout.fillWidth: true
        }
    }
}
