import QtQuick
import QtQuick.Controls

Rectangle {
    id: badge

    property string badgeText: ''
    required property color panelColor
    required property color borderColor
    required property color textColor
    required property color accentRequiredColor

    radius: 12
    color: Qt.rgba(badge.panelColor.r, badge.panelColor.g, badge.panelColor.b, 0.96)
    border.color: Qt.rgba(badge.accentRequiredColor.r, badge.accentRequiredColor.g, badge.accentRequiredColor.b, 0.18)
    border.width: 1
    implicitHeight: 28
    implicitWidth: badgeLabel.implicitWidth + 18

    Label {
        id: badgeLabel
        anchors.centerIn: parent
        text: badge.badgeText
        color: badge.textColor
        font.bold: true
    }
}
