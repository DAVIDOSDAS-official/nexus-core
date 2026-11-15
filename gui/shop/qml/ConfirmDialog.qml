import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "."

// "Nothing silently changes the system": every change is said first.
Popup {
    id: root
    property string title: ""
    property string subtitle: ""
    property var will: []
    property var wont: []
    property string command: ""
    property string actionText: "Install"
    property bool dangerous: false
    property var onAccept: null
    // What a Flatpak asks to reach (0.1.97). permissionsState: "" (not
    // an app install), "reading", "read" or "unread". The action waits
    // for "read": nothing is installed on permissions nobody saw.
    property string permissionsState: ""
    property var permissions: []
    readonly property bool anyBroad: {
        for (let i = 0; i < permissions.length; ++i) if (permissions[i].broad) return true
        return false
    }

    modal: true
    focus: true
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    width: Math.min(620, (parent ? parent.width : 620) - 48)
    padding: 28
    background: Rectangle { radius: 20; color: Theme.panel; border.color: Theme.lineStrong }

    function ask(options) {
        title = options.title
        subtitle = options.subtitle || ""
        will = options.will || []
        wont = options.wont || []
        command = options.command || ""
        actionText = options.actionText || "Install"
        dangerous = options.dangerous || false
        onAccept = options.onAccept
        permissionsState = options.permissionsState || ""
        permissions = []
        open()
    }
    function setPermissions(items, read) {
        permissions = items
        permissionsState = read ? "read" : "unread"
    }

    contentItem: ColumnLayout {
        spacing: 18
        ColumnLayout {
            spacing: 4
            Text { textFormat: Text.PlainText; text: root.title; color: Theme.bright; font.pixelSize: 22; font.bold: true; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Text { textFormat: Text.PlainText; text: root.subtitle; visible: text.length > 0; color: Theme.dim; font.pixelSize: 14; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }
        ColumnLayout {
            spacing: 8
            visible: root.will.length > 0
            Text { textFormat: Text.PlainText; text: "WHAT NEXUS WILL DO"; color: Theme.accentText; font.pixelSize: 12; font.bold: true }
            Repeater {
                model: root.will
                RowLayout {
                    spacing: 10
                    Layout.fillWidth: true
                    Text { textFormat: Text.PlainText; text: (index + 1) + "."; color: Theme.dim; font.pixelSize: 14 }
                    Text { textFormat: Text.PlainText; text: modelData; color: Theme.text; font.pixelSize: 14; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                }
            }
        }
        Rectangle {
            visible: root.wont.length > 0
            Layout.fillWidth: true
            implicitHeight: wontColumn.implicitHeight + 28
            radius: 12
            color: Theme.panel2
            ColumnLayout {
                id: wontColumn
                x: 14; y: 14
                width: parent.width - 28
                spacing: 6
                Text { textFormat: Text.PlainText; text: "IT WILL NOT"; color: Theme.dim; font.pixelSize: 12; font.bold: true }
                Repeater {
                    model: root.wont
                    Text { textFormat: Text.PlainText; text: "✕  " + modelData; color: Theme.text; font.pixelSize: 14; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                }
            }
        }
        ColumnLayout {
            spacing: 6
            visible: root.permissionsState.length > 0
            Layout.fillWidth: true
            Text { textFormat: Text.PlainText; text: "IT CAN REACH"; color: Theme.accentText; font.pixelSize: 12; font.bold: true }
            Text {
                textFormat: Text.PlainText
                visible: root.permissionsState === "reading"
                text: "Reading what it asks for from Flathub..."
                color: Theme.dim; font.pixelSize: 14
            }
            Text {
                textFormat: Text.PlainText
                visible: root.permissionsState === "unread"
                text: "What it asks for could not be read from Flathub, so it is not installed. Check the connection and try again."
                color: Theme.warn; font.pixelSize: 14; wrapMode: Text.WordWrap; Layout.fillWidth: true
            }
            Text {
                textFormat: Text.PlainText
                visible: root.permissionsState === "read" && root.permissions.length === 0
                text: "Nothing outside its sandbox."
                color: Theme.text; font.pixelSize: 14
            }
            Repeater {
                model: root.permissionsState === "read" ? root.permissions : []
                Text {
                    textFormat: Text.PlainText
                    text: (modelData.broad ? "!  " : "•  ") + modelData.text
                    color: modelData.broad ? Theme.warn : Theme.text
                    font.pixelSize: 14; wrapMode: Text.WordWrap; Layout.fillWidth: true
                }
            }
            Text {
                textFormat: Text.PlainText
                visible: root.permissionsState === "read" && root.anyBroad
                text: "Marked ! is more than an app usually needs. Install only if you trust it with that."
                color: Theme.warn; font.pixelSize: 13; wrapMode: Text.WordWrap; Layout.fillWidth: true
            }
        }
        Text {
            textFormat: Text.PlainText
            visible: root.command.length > 0
            text: "Runs:  " + root.command
            color: Theme.dim
            font.family: Theme.mono
            font.pixelSize: 12
            wrapMode: Text.WrapAnywhere
            Layout.fillWidth: true
        }
        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 12
            Button2 { text: "Cancel"; onClicked: root.close() }
            Button2 {
                text: root.actionText
                primary: !root.dangerous && !root.anyBroad
                danger: root.dangerous || root.anyBroad
                enabled: root.permissionsState === "" || root.permissionsState === "read"
                onClicked: { root.close(); if (root.onAccept) root.onAccept() }
            }
        }
    }
}
