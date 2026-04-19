import QtQuick
import QtQuick.Controls

Item {
    id: overlay

    required property var interaction
    required property var chart
    required property real plotWidth
    required property real plotHeight
    required property color accentBuyColor
    required property color borderColor
    required property color textColor

    anchors.fill: parent

    Rectangle {
        visible: overlay.interaction.rangeSelectionActive || overlay.interaction.selectionCommitted
        x: overlay.interaction.selectionLeft()
        y: overlay.interaction.selectionTop()
        width: overlay.interaction.selectionWidth()
        height: overlay.interaction.selectionHeight()
        color: "#2448c8d3"
        border.color: overlay.accentBuyColor
        border.width: 1
    }

    Rectangle {
        id: selectionSummaryCard
        visible: overlay.interaction.selectionCommitted && overlay.chart.selectionActive && overlay.chart.selectionSummaryText !== ""
        radius: 8
        color: "#f014161a"
        border.color: overlay.accentBuyColor
        border.width: 1
        width: Math.min(Math.max(340, overlay.plotWidth * 0.34), 460)
        x: {
            const desiredRight = overlay.interaction.selectionLeft() + overlay.interaction.selectionWidth() + 12
            const fallbackLeft = overlay.interaction.selectionLeft() - width - 12
            if (desiredRight + width <= overlay.plotWidth - 8)
                return desiredRight
            if (fallbackLeft >= 8)
                return fallbackLeft
            return Math.max(8, overlay.plotWidth - width - 8)
        }
        y: {
            const desiredTop = overlay.interaction.selectionTop()
            const maxY = overlay.plotHeight - height - 8
            return Math.max(8, Math.min(desiredTop, maxY))
        }
        height: summaryText.implicitHeight + 18

        Text {
            id: summaryText
            anchors.fill: parent
            anchors.margins: 12
            text: overlay.chart.selectionSummaryText
            color: overlay.textColor
            font.pixelSize: 12
            font.family: "Consolas"
            wrapMode: Text.Wrap
            lineHeight: 1.18
            lineHeightMode: Text.ProportionalHeight
            textFormat: Text.PlainText
        }
    }
}
