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
        visible: overlay.interaction.measurementMode !== 'middle_line'
                 && (overlay.interaction.rangeSelectionActive || overlay.interaction.selectionCommitted)
        x: overlay.interaction.selectionLeft()
        y: overlay.interaction.selectionTop()
        width: overlay.interaction.selectionWidth()
        height: overlay.interaction.selectionHeight()
        color: overlay.interaction.measurementMode === 'ctrl_hilo' ? '#26d38b24' : '#2448c8d3'
        border.color: overlay.accentBuyColor
        border.width: 1
    }

    Canvas {
        id: measureLine
        anchors.fill: parent
        visible: overlay.interaction.measurementMode === 'middle_line' && overlay.interaction.rangeSelectionActive
        onPaint: {
            const ctx = getContext('2d')
            ctx.clearRect(0, 0, width, height)
            if (!visible)
                return
            ctx.strokeStyle = overlay.accentBuyColor
            ctx.lineWidth = 1.5
            ctx.beginPath()
            ctx.moveTo(overlay.interaction.selectionStartX, overlay.interaction.selectionStartY)
            ctx.lineTo(overlay.interaction.selectionEndX, overlay.interaction.selectionEndY)
            ctx.stroke()
            ctx.fillStyle = overlay.accentBuyColor
            ctx.beginPath()
            ctx.arc(overlay.interaction.selectionStartX, overlay.interaction.selectionStartY, 3, 0, Math.PI * 2)
            ctx.arc(overlay.interaction.selectionEndX, overlay.interaction.selectionEndY, 3, 0, Math.PI * 2)
            ctx.fill()
        }
        Connections {
            target: overlay.interaction
            function onSelectionStartXChanged() { measureLine.requestPaint() }
            function onSelectionStartYChanged() { measureLine.requestPaint() }
            function onSelectionEndXChanged() { measureLine.requestPaint() }
            function onSelectionEndYChanged() { measureLine.requestPaint() }
            function onMeasurementModeChanged() { measureLine.requestPaint() }
            function onRangeSelectionActiveChanged() { measureLine.requestPaint() }
        }
    }

    Rectangle {
        id: selectionSummaryCard
        visible: overlay.interaction.rangeSelectionActive && overlay.chart.selectionActive && overlay.chart.selectionSummaryText !== ''
        radius: 8
        color: "#f014161a"
        border.color: overlay.accentBuyColor
        border.width: 1
        width: Math.min(Math.max(340, overlay.plotWidth * 0.34), 460)
        x: {
            const anchorX = overlay.interaction.measurementMode === 'middle_line'
                ? overlay.interaction.selectionEndX
                : overlay.interaction.selectionLeft() + overlay.interaction.selectionWidth()
            const desiredRight = anchorX + 12
            const fallbackLeft = anchorX - width - 12
            if (desiredRight + width <= overlay.plotWidth - 8)
                return desiredRight
            if (fallbackLeft >= 8)
                return fallbackLeft
            return Math.max(8, overlay.plotWidth - width - 8)
        }
        y: {
            const desiredTop = overlay.interaction.measurementMode === 'middle_line'
                ? overlay.interaction.selectionEndY
                : overlay.interaction.selectionTop()
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
