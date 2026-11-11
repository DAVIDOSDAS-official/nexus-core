import QtQuick
import QtQuick.Layouts
import "."

// Why a button is greyed: another change is running.
Text {
    textFormat: Text.PlainText
    visible: shop.jobRunning
    text: "Waiting for “" + shop.jobTitle + "” to finish: one change at a time."
    color: Theme.dim
    font.pixelSize: 12
    wrapMode: Text.WordWrap
    Layout.fillWidth: true
}
