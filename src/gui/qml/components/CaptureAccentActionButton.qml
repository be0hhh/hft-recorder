import QtQuick
import QtQuick.Controls

Button {
    id: control

    required property color accentColor
    required property color actionTextColor
    required property color mutedTextColor

    leftPadding: 16
    rightPadding: 16
    topPadding: 8
    bottomPadding: 8

    background: Item {
        implicitWidth: 140
        implicitHeight: 40

        Rectangle {
            anchors.fill: parent
            anchors.margins: -3
            radius: 11
            color: Qt.rgba(control.accentColor.r, control.accentColor.g, control.accentColor.b, 0.22)
            visible: control.enabled
        }

        Rectangle {
            anchors.fill: parent
            radius: 8
            color: control.enabled ? control.accentColor : "#232326"
            border.color: control.enabled ? Qt.lighter(control.accentColor, 1.1) : "#313136"
            border.width: 1
        }
    }

    contentItem: Text {
        text: control.text
        color: control.enabled ? control.actionTextColor : control.mutedTextColor
        font.bold: true
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
