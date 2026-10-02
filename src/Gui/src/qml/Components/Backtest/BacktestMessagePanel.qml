import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: panel

    property string title: ""
    property string message: ""
    property color panelColor: "#151820"
    property color borderColor: "#343946"
    property color titleColor: "#51a7ff"
    property color textColor: "#d7dce6"

    visible: message !== ""
    Layout.fillWidth: true
    Layout.preferredHeight: Math.min(132, Math.max(54, content.implicitHeight + 20))
    color: panelColor
    border.color: borderColor
    radius: 6
    clip: true

    Flickable {
        anchors.fill: parent
        anchors.margins: 10
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        contentWidth: Math.max(1, width)
        contentHeight: content.implicitHeight
        interactive: contentHeight > height

        ColumnLayout {
            id: content
            width: parent.width
            spacing: 4

            Label {
                text: panel.title
                color: panel.titleColor
                font.pixelSize: 12
                font.bold: true
                Layout.fillWidth: true
            }

            Label {
                text: panel.message
                color: panel.textColor
                font.pixelSize: 11
                wrapMode: Text.WrapAnywhere
                Layout.fillWidth: true
            }
        }
    }
}
