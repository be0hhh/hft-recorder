import QtQuick
import QtQuick.Controls

Button {
    id: control

    required property color panelColor
    required property color panelAltColor
    required property color borderColor
    required property color textColor

    background: Rectangle {
        radius: 7
        color: control.down ? control.panelAltColor : control.panelColor
        border.color: control.borderColor
        border.width: 1
    }

    contentItem: Text {
        text: control.text
        color: control.textColor
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        font.pixelSize: 13
    }
}
