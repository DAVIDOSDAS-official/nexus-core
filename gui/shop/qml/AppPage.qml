import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "."

// Everything about one app: what each source offers, which one Nexus
// suggests and why, and the description.
Flickable {
    id: root
    property string key: ""
    property var app: key.length > 0 ? shop.details(key) : ({})
    signal back()

    contentWidth: width
    contentHeight: column.implicitHeight + 64
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar {}

    ColumnLayout {
        id: column
        x: 32
        y: 16
        width: Math.min(root.width - 64, 980)
        spacing: 24

        Button {
            text: "‹  Back"
            flat: true
            onClicked: root.back()
            contentItem: Text { text: parent.text; color: Theme.dim; font.pixelSize: 14 }
            background: Rectangle { color: "transparent" }
        }

        // Header.
        RowLayout {
            spacing: 20
            Layout.fillWidth: true
            AppIcon { source: root.app.icon || ""; name: root.app.name || ""; size: 88 }
            ColumnLayout {
                spacing: 6
                Layout.fillWidth: true
                Text {
                    text: root.app.name || ""
                    color: Theme.bright
                    font.pixelSize: 28
                    font.bold: true
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Text {
                    text: root.app.summary || ""
                    color: Theme.dim
                    font.pixelSize: 15
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
                Flow {
                    spacing: 6
                    Layout.fillWidth: true
                    Chip { visible: (root.app.developer || "").length > 0; text: "by " + (root.app.developer || "") }
                    Chip { visible: (root.app.license || "").length > 0; text: root.app.license || "" }
                    Chip { visible: root.app.installed === true; text: "Installed"; strong: true }
                }
            }
        }

        // The suggestion.
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: suggestion.implicitHeight + 44
            radius: 16
            color: Theme.panel2
            border.color: Theme.accentLine
            ColumnLayout {
                id: suggestion
                x: 22
                y: 22
                width: parent.width - 44
                spacing: 12
                RowLayout {
                    spacing: 12
                    Chip { text: "NEXUS SUGGESTS"; strong: true }
                    Text {
                        text: root.app.suggested || ""
                        color: Theme.bright
                        font.pixelSize: 20
                        font.bold: true
                    }
                }
                Repeater {
                    model: root.app.reasons || []
                    RowLayout {
                        spacing: 10
                        Layout.fillWidth: true
                        Text { text: "✓"; color: Theme.ok; font.pixelSize: 14; Layout.alignment: Qt.AlignTop }
                        Text {
                            text: modelData
                            color: Theme.text
                            font.pixelSize: 14
                            wrapMode: Text.WordWrap
                            Layout.fillWidth: true
                        }
                    }
                }
                Text {
                    visible: (root.app.otherNote || "").length > 0
                    text: root.app.otherNote || ""
                    color: Theme.dim
                    font.pixelSize: 13
                    wrapMode: Text.WordWrap
                    Layout.fillWidth: true
                }
            }
        }

        // Every source, side by side.
        Text {
            text: (root.app.sourceCount || 0) > 1 ? "Every source, side by side" : "Where it comes from"
            color: Theme.dim
            font.pixelSize: 15
            font.bold: true
        }
        Repeater {
            model: root.app.offers || []
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: offer.implicitHeight + 32
                radius: 12
                color: modelData.suggested ? Theme.panel2 : Theme.panel
                border.color: Theme.line
                ColumnLayout {
                    id: offer
                    x: 18
                    y: 16
                    width: parent.width - 36
                    spacing: 10
                    Flow {
                        Layout.fillWidth: true
                        spacing: 10
                        Text {
                            text: modelData.source
                            color: Theme.bright
                            font.pixelSize: 16
                            font.bold: true
                        }
                        Chip { visible: modelData.suggested; text: "suggested"; strong: true }
                        Chip { visible: modelData.verified; text: "verified developer"; good: true }
                        Chip { visible: modelData.installed; text: "Installed"; strong: true }
                    }
                    GridLayout {
                        columns: 2
                        columnSpacing: 18
                        rowSpacing: 6
                        Text { text: "Kind"; color: Theme.dim; font.pixelSize: 13 }
                        Text { text: modelData.kind; color: Theme.text; font.pixelSize: 13 }
                        Text { text: "Packaged by"; color: Theme.dim; font.pixelSize: 13 }
                        Text { text: modelData.who; color: Theme.text; font.pixelSize: 13 }
                        Text { text: "Restart"; color: Theme.dim; font.pixelSize: 13 }
                        Text { text: modelData.restart; color: modelData.restart === "not needed" ? Theme.text : Theme.warn; font.pixelSize: 13 }
                    }
                    RowLayout {
                        visible: !modelData.installed
                        Layout.fillWidth: true
                        spacing: 10
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: 38
                            radius: 8
                            color: Theme.bg
                            border.color: Theme.line
                            TextEdit {
                                anchors.fill: parent
                                anchors.leftMargin: 12
                                verticalAlignment: TextEdit.AlignVCenter
                                text: modelData.command
                                readOnly: true
                                selectByMouse: true
                                color: Theme.bright
                                font.family: Theme.mono
                                font.pixelSize: 13
                            }
                        }
                        Button {
                            id: copy
                            text: "Copy"
                            implicitHeight: 38
                            onClicked: { shop.copyText(modelData.command); text = "Copied" }
                            contentItem: Text { text: copy.text; color: Theme.bright; font.pixelSize: 13; horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter }
                            background: Rectangle { radius: 8; color: copy.hovered ? Theme.lineStrong : Theme.line }
                        }
                    }
                }
            }
        }
        Text {
            text: "The Shop does not install yet: that comes next. Until then, copy the command and paste it in a terminal. Nothing above has changed this computer."
            color: Theme.dim
            font.pixelSize: 13
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }

        // Screenshots, from the catalogue's web addresses. They need
        // the network; without it this row stays empty.
        Flow {
            id: shots
            Layout.fillWidth: true
            spacing: 12
            visible: (root.app.screenshots || []).length > 0
            Repeater {
                model: (root.app.screenshots || []).slice(0, 3)
                Rectangle {
                    width: Math.min(312, (shots.width - 24) / 3)
                    height: width * 0.62
                    radius: 10
                    color: Theme.panel2
                    clip: true
                    Image {
                        anchors.fill: parent
                        source: modelData
                        asynchronous: true
                        fillMode: Image.PreserveAspectCrop
                        sourceSize.width: 640
                    }
                }
            }
        }

        Text {
            text: "About"
            color: Theme.dim
            font.pixelSize: 15
            font.bold: true
            visible: (root.app.description || "").length > 0
        }
        Text {
            text: root.app.description || ""
            color: Theme.text
            font.pixelSize: 14
            lineHeight: 1.3
            wrapMode: Text.WordWrap
            textFormat: Text.PlainText
            Layout.fillWidth: true
        }
        Text {
            visible: (root.app.homepage || "").length > 0
            text: "Website: " + (root.app.homepage || "")
            color: Theme.accentText
            font.pixelSize: 14
            MouseArea {
                anchors.fill: parent
                cursorShape: Qt.PointingHandCursor
                onClicked: shop.openLink(root.app.homepage)
            }
        }
    }
}
