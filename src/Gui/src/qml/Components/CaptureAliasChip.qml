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
        color: chip.active
            ? Qt.rgba(chip.accentOptionalColor.r, chip.accentOptionalColor.g, chip.accentOptionalColor.b, 0.08)
            : chip.panelColor
        border.color: chip.required
            ? Qt.rgba(chip.accentRequiredColor.r, chip.accentRequiredColor.g, chip.accentRequiredColor.b, 0.30)
            : (chip.active
                ? Qt.rgba(chip.accentOptionalColor.r, chip.accentOptionalColor.g, chip.accentOptionalColor.b, 0.28)
                : chip.borderColor)
        border.width: 1
    }

    contentItem: Text {
        text: chip.text
        color: chip.textColor
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }
}
