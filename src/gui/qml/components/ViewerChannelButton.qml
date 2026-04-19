import QtQuick
import QtQuick.Controls

Button {
    id: control

    property bool active: false
    required property color panelColor
    required property color panelAltColor
    required property color borderColor
    required property color textColor
    required property color mutedTextColor
    required property color accentBuyColor

    background: Rectangle {
        radius: 7
        color: control.active ? control.panelAltColor : control.panelColor
        border.color: control.active ? control.accentBuyColor : control.borderColor
        border.width: 1
    }

    contentItem: Text {
        text: control.text
        color: control.active ? control.textColor : control.mutedTextColor
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        font.pixelSize: 13
        font.bold: control.active
    }
}
