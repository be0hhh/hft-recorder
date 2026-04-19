import QtQuick
import QtQuick.Controls

Rectangle {
    id: badge

    property string badgeText: ""
    required property color accentRequiredColor

    radius: 12
    color: "#102c34"
    border.color: badge.accentRequiredColor
    border.width: 1
    implicitHeight: 28
    implicitWidth: badgeLabel.implicitWidth + 18

    Label {
        id: badgeLabel
        anchors.centerIn: parent
        text: badge.badgeText
        color: badge.accentRequiredColor
        font.bold: true
    }
}
