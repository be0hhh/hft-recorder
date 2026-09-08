import QtQuick
import QtQuick.Layouts

Rectangle {
    id: button

    property string text: ""
    property bool enabledValue: true
    property color accent: "#51a7ff"
    property color textColor: "#d7dce6"
    property color mutedTextColor: "#8a92a0"
    property color panelColor: "#1c2028"
    property color panelDeepColor: "#151820"
    property color borderColor: "#343946"

    signal clicked()

    radius: 6
    implicitWidth: Math.max(76, label.implicitWidth + 18)
    implicitHeight: 30
    Layout.minimumWidth: implicitWidth
    Layout.preferredWidth: implicitWidth
    Layout.preferredHeight: implicitHeight
    color: enabledValue ? (mouse.containsMouse ? "#2b303a" : panelColor) : panelDeepColor
    border.color: enabledValue ? accent : borderColor
    border.width: 1
    opacity: enabledValue ? 1.0 : 0.5

    Text {
        id: label
        anchors.centerIn: parent
        width: parent.width - 12
        text: button.text
        color: button.enabledValue ? button.textColor : button.mutedTextColor
        font.pixelSize: 11
        font.bold: true
        elide: Text.ElideRight
        horizontalAlignment: Text.AlignHCenter
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        enabled: button.enabledValue
        onClicked: button.clicked()
    }
}
