import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: field

    property string caption: ""
    property alias text: input.text
    property int fieldWidth: 92
    property color textColor: "#d7dce6"
    property color mutedTextColor: "#8a92a0"
    property color panelDeepColor: "#151820"
    property color borderColor: "#343946"

    signal edited(string value)

    Layout.preferredWidth: fieldWidth
    Layout.minimumWidth: fieldWidth
    Layout.preferredHeight: 42

    ColumnLayout {
        anchors.fill: parent
        spacing: 2

        Label {
            text: field.caption
            color: field.mutedTextColor
            font.pixelSize: 10
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        TextField {
            id: input
            Layout.fillWidth: true
            Layout.preferredHeight: 24
            color: field.textColor
            font.pixelSize: 12
            selectByMouse: true
            background: Rectangle {
                radius: 5
                color: field.panelDeepColor
                border.color: field.borderColor
                border.width: 1
            }
            onEditingFinished: field.edited(text)
        }
    }
}
