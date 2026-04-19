import QtQuick
import QtQuick.Controls

Rectangle {
    id: scaleRoot

    property var chart: null
    property int tickCount: 0
    property color scaleColor: "#26262b"
    property color borderColor: "#49494f"
    property color mutedTextColor: "#aaaaaf"

    height: 38
    color: scaleRoot.scaleColor
    border.color: scaleRoot.borderColor
    border.width: 1

    Repeater {
        model: scaleRoot.tickCount
        delegate: Item {
            required property int index
            property real tickRatio: index / Math.max(1, scaleRoot.tickCount - 1)
            width: 70
            height: scaleRoot.height
            x: (scaleRoot.width - width) * tickRatio

            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.verticalCenter
                text: scaleRoot.chart ? scaleRoot.chart.formatTimeAt(parent.tickRatio) : ""
                color: scaleRoot.mutedTextColor
                font.pixelSize: 12
            }
        }
    }
}
