import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "."

// The system and the apps, one button for both. Same check and same
// rules as `nexus update`: nothing restarts by itself.
Flickable {
    id: root
    signal updateAll()
    property var u: shop.updates
    readonly property bool checking: u.state === "checking"
    readonly property bool systemNew: u.system === "new"
    readonly property var apps: u.apps || []

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
        spacing: 22

        RowLayout {
            Layout.fillWidth: true
            spacing: 16
            ColumnLayout {
                spacing: 4
                Layout.fillWidth: true
                Text {
                    text: root.checking && shop.updateCount === 0 ? "Checking…"
                        : shop.updateCount === 0 ? "Everything is up to date"
                        : shop.updateCount === 1 ? "1 update" : shop.updateCount + " updates"
                    color: Theme.bright
                    font.pixelSize: 28
                    font.bold: true
                }
                Text {
                    text: "Nexus checks once a day and only tells you. Nothing is installed until you press the button, and nothing restarts by itself."
                    color: Theme.dim
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
            Button2 {
                text: root.checking ? "Checking…" : "Check again"
                enabled: !root.checking && !shop.jobRunning
                onClicked: shop.checkUpdates()
            }
            Button2 {
                text: "Update everything"
                primary: true
                enabled: !root.checking && !shop.jobRunning && shop.updateCount > 0
                onClicked: root.updateAll()
            }
        }

        BusyHint {}

        // Could not ask.
        Text {
            visible: u.state === "error" || u.system === "unreachable"
            text: "Could not reach the registry to compare (offline?). Apps are checked separately below."
            color: Theme.warn
            font.pixelSize: 14
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        // The system.
        Rectangle {
            visible: u.system !== undefined && u.system !== "not-image" && (u.system || "").length > 0
            Layout.fillWidth: true
            implicitHeight: sys.implicitHeight + 44
            radius: 16
            color: Theme.panel2
            border.color: root.systemNew ? Theme.accentLine : Theme.line
            ColumnLayout {
                id: sys
                x: 22; y: 22
                width: parent.width - 44
                spacing: 10
                RowLayout {
                    spacing: 12
                    Text {
                        text: "The system"
                        color: Theme.bright
                        font.pixelSize: 18
                        font.bold: true
                    }
                    Chip {
                        visible: root.systemNew
                        text: "used from the next restart"
                        strong: true
                    }
                    Chip {
                        visible: (u.staged || "").length > 0
                        text: "downloaded, waits for a restart"
                        good: true
                    }
                }
                Text {
                    text: "Running Nexus " + (u.running || "?") + ((u.runningDay || "").length ? "  (built " + u.runningDay + ")" : "")
                    color: Theme.text
                    font.pixelSize: 14
                }
                Text {
                    visible: root.systemNew
                    text: "New: Nexus " + (u.version || "") + ((u.day || "").length ? "  (built " + u.day + ")" : "")
                    color: Theme.accentText
                    font.pixelSize: 14
                    font.bold: true
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Text {
                    visible: (u.diff || "").length > 0
                    text: u.diff || ""
                    color: Theme.dim
                    font.pixelSize: 13
                }
                Text {
                    visible: root.systemNew
                    text: "The version running now stays in the boot menu: if something is wrong, pick it there to go back."
                    color: Theme.dim
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Text {
                    visible: !root.systemNew && u.system === "current"
                    text: "Up to date (checked just now)."
                    color: Theme.dim
                    font.pixelSize: 13
                }
            }
        }

        Text {
            text: root.apps.length > 0 ? "Apps · ready at once, no restart" : (root.checking ? "" : "Apps · up to date")
            visible: text.length > 0
            color: Theme.dim
            font.pixelSize: 15
            font.bold: true
        }
        Repeater {
            model: root.apps
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 64
                radius: 12
                color: Theme.panel
                border.color: Theme.line
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 16
                    anchors.rightMargin: 16
                    spacing: 14
                    AppIcon { source: modelData.icon || ""; name: modelData.name || ""; size: 36 }
                    ColumnLayout {
                        spacing: 0
                        Layout.fillWidth: true
                        Text { text: modelData.name; color: Theme.bright; font.pixelSize: 14; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                        Text { text: modelData.id; color: Theme.dim; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true }
                    }
                    Text {
                        visible: (modelData.version || "").length > 0
                        text: "→ " + modelData.version
                        color: Theme.text
                        font.family: Theme.mono
                        font.pixelSize: 13
                    }
                }
            }
        }

        Text {
            text: "“Update everything” asks for your password once (the system part needs it), then runs  nexus update --apply. Every update is written in History."
            color: Theme.dim
            font.pixelSize: 12
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }
}
