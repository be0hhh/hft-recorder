import QtQuick
import QtQuick.Controls

Rectangle {
    id: scaleRoot

    property var chart: null
    property int tickCount: 0
    property color scaleColor: "#26262b"
    property color borderColor: "#49494f"
    property color mutedTextColor: "#aaaaaf"

    width: 88
    color: scaleRoot.scaleColor
    border.color: scaleRoot.borderColor
    border.width: 1

    Repeater {
        model: scaleRoot.tickCount
        delegate: Item {
            required property int index
            property real tickRatio: index / Math.max(1, scaleRoot.tickCount - 1)
            width: scaleRoot.width
            height: 20
            y: (scaleRoot.height - height) * tickRatio

            Label {
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.verticalCenter: parent.verticalCenter
                text: scaleRoot.chart ? scaleRoot.chart.formatPriceScaleLabel(index, scaleRoot.tickCount) : ""
                color: scaleRoot.mutedTextColor
                font.pixelSize: 12
            }
        }
    }
}
