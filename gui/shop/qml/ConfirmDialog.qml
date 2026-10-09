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
        open()
    }

    contentItem: ColumnLayout {
        spacing: 18
        ColumnLayout {
            spacing: 4
            Text { text: root.title; color: Theme.bright; font.pixelSize: 22; font.bold: true; wrapMode: Text.WordWrap; Layout.fillWidth: true }
            Text { text: root.subtitle; visible: text.length > 0; color: Theme.dim; font.pixelSize: 14; wrapMode: Text.WordWrap; Layout.fillWidth: true }
        }
        ColumnLayout {
            spacing: 8
            visible: root.will.length > 0
            Text { text: "WHAT NEXUS WILL DO"; color: Theme.accentText; font.pixelSize: 12; font.bold: true }
            Repeater {
                model: root.will
                RowLayout {
                    spacing: 10
                    Layout.fillWidth: true
                    Text { text: (index + 1) + "."; color: Theme.dim; font.pixelSize: 14 }
                    Text { text: modelData; color: Theme.text; font.pixelSize: 14; wrapMode: Text.WordWrap; Layout.fillWidth: true }
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
                Text { text: "IT WILL NOT"; color: Theme.dim; font.pixelSize: 12; font.bold: true }
                Repeater {
                    model: root.wont
                    Text { text: "✕  " + modelData; color: Theme.text; font.pixelSize: 14; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                }
            }
        }
        Text {
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
                primary: !root.dangerous
                danger: root.dangerous
                onClicked: { root.close(); if (root.onAccept) root.onAccept() }
            }
        }
    }
}
