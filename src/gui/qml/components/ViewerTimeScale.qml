import QtQuick

Rectangle {
    id: scaleRoot

    property var chart: null
    property int tickCount: 0
    property color scaleColor: "#26262b"
    property color borderColor: "#49494f"
    property color mutedTextColor: "#aaaaaf"
    property var ticks: {
        if (!scaleRoot.chart)
            return []

        const tsMin = scaleRoot.chart.tsMin
        const tsMax = scaleRoot.chart.tsMax
        void tsMin
        void tsMax
        return scaleRoot.chart.timeScaleTicks(scaleRoot.tickCount)
    }

    height: 38
    color: scaleRoot.scaleColor
    border.color: scaleRoot.borderColor
    border.width: 1

    Repeater {
        model: scaleRoot.ticks
        delegate: Item {
            property var tickData: modelData
            width: 70
            height: scaleRoot.height
            x: (scaleRoot.width - width) * tickData.ratio

            Text {
                anchors.fill: parent
                text: tickData.text
                color: scaleRoot.mutedTextColor
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                renderType: Text.NativeRendering
            }
        }
    }
}
