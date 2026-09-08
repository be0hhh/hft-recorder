import QtQuick
import QtQuick.Controls

Button {
    id: control

    required property color panelAltColor
    required property color borderColor
    required property color textColor
    required property color mutedTextColor

    background: Rectangle {
        radius: 8
        color: control.enabled ? control.panelAltColor : "#232326"
        border.color: control.enabled ? control.borderColor : "#313136"
        border.width: 1
    }

    contentItem: Text {
        text: control.text
        color: control.enabled ? control.textColor : control.mutedTextColor
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
