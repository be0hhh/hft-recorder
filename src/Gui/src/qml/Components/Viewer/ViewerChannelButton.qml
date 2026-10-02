import QtQuick
import QtQuick.Controls

Button {
    id: control

    property bool active: false
    property bool compact: false
    required property color panelColor
    required property color panelAltColor
    required property color borderColor
    required property color textColor
    required property color mutedTextColor
    required property color accentBuyColor

    leftPadding: compact ? 8 : 12
    rightPadding: compact ? 8 : 12
    topPadding: 4
    bottomPadding: 4
    implicitHeight: compact ? 28 : 30
    implicitWidth: Math.max(compact ? 44 : 62, contentItem.implicitWidth + leftPadding + rightPadding)

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
        font.pixelSize: control.compact ? 12 : 13
        font.bold: control.active
        elide: Text.ElideRight
    }
}
