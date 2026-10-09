import QtQuick
import QtQuick.Controls.Basic
import "."

// The Shop's buttons: primary (orange) or quiet.
Button {
    id: root
    property bool primary: false
    property bool danger: false
    implicitHeight: 40
    leftPadding: 18
    rightPadding: 18
    hoverEnabled: true
    contentItem: Text {
        text: root.text
        color: root.primary ? "#0b0d0c" : (root.danger ? "#fca5a5" : Theme.bright)
        font.pixelSize: 14
        font.bold: root.primary
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        opacity: root.enabled ? 1 : 0.45
    }
    background: Rectangle {
        radius: 10
        color: root.primary ? (root.hovered ? "#fb8a3c" : Theme.accent)
                            : (root.hovered ? Theme.lineStrong : Theme.line)
        opacity: root.enabled ? 1 : 0.5
        border.color: root.activeFocus ? Theme.bright : "transparent"
    }
}
