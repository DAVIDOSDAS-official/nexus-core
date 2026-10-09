import QtQuick
import QtQuick.Controls.Basic
import "."

AbstractButton {
    id: root
    property bool current: false
    hoverEnabled: true
    implicitHeight: 36
    implicitWidth: 200
    leftPadding: 14
    background: Rectangle {
        radius: 10
        color: root.current ? Theme.accentSoft : (root.hovered ? Theme.panel2 : "transparent")
        border.color: root.activeFocus ? Theme.accent : "transparent"
    }
    contentItem: Text {
        text: root.text
        color: root.current ? Theme.accentText : Theme.text
        font.pixelSize: 14
        font.bold: root.current
        verticalAlignment: Text.AlignVCenter
    }
}
