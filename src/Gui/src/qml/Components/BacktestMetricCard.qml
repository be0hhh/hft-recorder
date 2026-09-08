import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: card

    property var metric: ({})
    property bool selected: false
    property color textColor: "#d7dce6"
    property color mutedTextColor: "#8a92a0"
    property color panelColor: "#1c2028"
    property color panelDeepColor: "#151820"
    property color borderColor: "#343946"
    property color accentColor: "#51a7ff"

    signal clicked(string key)

    width: 146
    height: 58
    radius: 7
    color: metricMouse.containsMouse ? panelColor : panelDeepColor
    border.color: selected ? accentColor : (metric.primary ? "#4a9aa0" : borderColor)
    border.width: selected ? 2 : 1

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 2

        Label {
            text: card.metric.group || "Metric"
            color: card.mutedTextColor
            font.pixelSize: 9
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        Label {
            text: card.metric.value || ""
            color: card.textColor
            font.pixelSize: 15
            font.bold: true
            elide: Text.ElideRight
            Layout.fillWidth: true
        }

        Label {
            text: card.metric.label || ""
            color: card.mutedTextColor
            font.pixelSize: 10
            elide: Text.ElideRight
            Layout.fillWidth: true
        }
    }

    MouseArea {
        id: metricMouse
        anchors.fill: parent
        hoverEnabled: true
        onClicked: card.clicked(card.metric.key || "")
    }
}
