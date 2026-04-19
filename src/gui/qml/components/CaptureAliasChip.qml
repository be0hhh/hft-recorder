import QtQuick
import QtQuick.Controls

Button {
    id: chip

    property bool active: false
    property bool required: false
    required property color panelColor
    required property color borderColor
    required property color textColor
    required property color accentRequiredColor
    required property color accentOptionalColor

    background: Rectangle {
        radius: 14
        color: chip.required ? chip.accentRequiredColor
            : (chip.active ? chip.accentOptionalColor : chip.panelColor)
        border.color: chip.required ? chip.accentRequiredColor
            : (chip.active ? chip.accentOptionalColor : chip.borderColor)
        border.width: 1
    }

    contentItem: Text {
        text: chip.text
        color: (chip.required || chip.active) ? "#071419" : chip.textColor
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
