import QtQuick

Rectangle {
    id: scaleRoot

    property var chart: null
    property int tickCount: 0
    property color scaleColor: "#26262b"
    property color borderColor: "#49494f"
    property color mutedTextColor: "#aaaaaf"
    property int minScaleWidth: 104
    property int labelLeftPadding: 8
    property int labelRightPadding: 10
    property int labelPixelSize: 12
    property var ticks: {
        if (!scaleRoot.chart)
            return []

        const priceMinE8 = scaleRoot.chart.priceMinE8
        const priceMaxE8 = scaleRoot.chart.priceMaxE8
        void priceMinE8
        void priceMaxE8
        return scaleRoot.chart.priceScaleTicks(scaleRoot.tickCount)
    }
    property string widestLabelText: {
        if (!scaleRoot.ticks || scaleRoot.ticks.length === 0)
            return "0.00000000"

        let widest = scaleRoot.ticks[0].text
        for (let i = 1; i < scaleRoot.ticks.length; ++i) {
            const text = scaleRoot.ticks[i].text
            if (text.length > widest.length)
                widest = text
        }
        return widest
    }

    width: Math.max(
        scaleRoot.minScaleWidth,
        Math.ceil(labelMetrics.advanceWidth) + scaleRoot.labelLeftPadding + scaleRoot.labelRightPadding + 2)
    color: scaleRoot.scaleColor
    border.color: scaleRoot.borderColor
    border.width: 1

    TextMetrics {
        id: labelMetrics
        font.pixelSize: scaleRoot.labelPixelSize
        text: scaleRoot.widestLabelText
    }

    Repeater {
        model: scaleRoot.ticks
        delegate: Item {
            property var tickData: modelData
            width: scaleRoot.width
            height: 20
            y: (scaleRoot.height - height) * tickData.ratio

            Text {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.leftMargin: scaleRoot.labelLeftPadding
                anchors.rightMargin: scaleRoot.labelRightPadding
                anchors.verticalCenter: parent.verticalCenter
                clip: true
                text: tickData.text
                color: scaleRoot.mutedTextColor
                font.pixelSize: scaleRoot.labelPixelSize
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
                renderType: Text.NativeRendering
            }
        }
    }
}
