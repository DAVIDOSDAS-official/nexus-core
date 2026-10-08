import QtQuick
import "."

// The app's own icon from the catalogue, or its first letter when
// the catalogue has none.
Item {
    id: root
    property string source: ""
    property string name: ""
    property int size: 48
    width: size
    height: size

    Rectangle {
        anchors.fill: parent
        radius: root.size * 0.22
        color: Theme.panel2
        border.color: Theme.line
        visible: image.status !== Image.Ready
        Text {
            anchors.centerIn: parent
            text: root.name.length > 0 ? root.name.charAt(0).toUpperCase() : "?"
            color: Theme.accentText
            font.pixelSize: root.size * 0.45
            font.bold: true
        }
    }
    Image {
        id: image
        anchors.fill: parent
        source: root.source
        sourceSize.width: root.size * 2
        sourceSize.height: root.size * 2
        fillMode: Image.PreserveAspectFit
        asynchronous: true
        smooth: true
        mipmap: true
    }
}
