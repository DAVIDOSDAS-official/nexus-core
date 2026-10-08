import QtQuick
import "."

Rectangle {
    id: root
    property alias text: label.text
    property bool good: false
    property bool strong: false
    implicitWidth: label.implicitWidth + 16
    implicitHeight: 22
    radius: 6
    color: good ? Theme.okSoft : (strong ? Theme.accentSoft : "#1b201e")
    Text {
        id: label
        anchors.centerIn: parent
        font.pixelSize: 12
        color: root.good ? Theme.ok : (root.strong ? Theme.accentText : Theme.dim)
    }
}
