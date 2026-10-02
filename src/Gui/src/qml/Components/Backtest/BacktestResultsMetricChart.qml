import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HftRecorder 1.0

Rectangle {
    id: metricChart
    required property var viewRoot

    Layout.fillWidth: true
    Layout.fillHeight: true
    Layout.minimumHeight: 170
    color: viewRoot.windowColor
    border.color: viewRoot.borderColor
    radius: 5
    clip: true

    Canvas {
        id: metricCanvas
        anchors.fill: parent
        anchors.margins: 10
        property var points: viewRoot.backtestVm.selectedResultMetricSeries

        Connections {
            target: viewRoot.backtestVm
            function onSelectedResultMetricChanged() { metricCanvas.requestPaint() }
            function onSelectionChanged() { metricCanvas.requestPaint() }
        }
        onWidthChanged: requestPaint()
        onHeightChanged: requestPaint()
        onPointsChanged: requestPaint()

        function seriesValue(point) {
            if (viewRoot.backtestVm.selectedResultMetricRatioKey.length > 0) {
                if (!point.hasRatio) return NaN
                return Number(point.valueRaw) / Number(point.denominatorRaw)
            }
            return Number(point.valueRaw)
        }

        function paddedBounds(points) {
            var has = false
            var minValue = 0
            var maxValue = 0
            for (var i = 0; i < points.length; ++i) {
                var value = seriesValue(points[i])
                if (!isFinite(value)) continue
                if (!has) { minValue = value; maxValue = value; has = true }
                else { minValue = Math.min(minValue, value); maxValue = Math.max(maxValue, value) }
            }
            if (!has) return null
            minValue = Math.min(minValue, 0)
            maxValue = Math.max(maxValue, 0)
            var span = maxValue - minValue
            if (span <= 0) span = viewRoot.backtestVm.selectedResultMetricRatioKey.length > 0 ? 1.0 : 100000000
            var pad = Math.max(span * 0.08, viewRoot.backtestVm.selectedResultMetricRatioKey.length > 0 ? 0.01 : 1000000)
            return { "min": minValue - pad, "max": maxValue + pad }
        }

        function yFor(value, minValue, maxValue, plotY, plotH) {
            return plotY + plotH - ((value - minValue) / (maxValue - minValue)) * plotH
        }

        function drawScale(ctx, bounds, plotX, plotY, plotW, plotH) {
            ctx.font = "10px sans-serif"
            ctx.textAlign = "right"
            ctx.textBaseline = "middle"
            for (var i = 0; i <= 4; ++i) {
                var value = bounds.max - ((bounds.max - bounds.min) * i / 4)
                var y = plotY + (plotH * i / 4)
                ctx.strokeStyle = "#2f333d"
                ctx.lineWidth = 1
                ctx.beginPath()
                ctx.moveTo(plotX, y)
                ctx.lineTo(plotX + plotW, y)
                ctx.stroke()
                ctx.fillStyle = viewRoot.mutedTextColor
                ctx.fillText(viewRoot.metricPointText(value, viewRoot.backtestVm.selectedResultMetricKey), plotX - 8, y)
            }
        }

        onPaint: {
            var ctx = getContext("2d")
            ctx.clearRect(0, 0, width, height)
            var points = viewRoot.backtestVm.selectedResultMetricSeries
            if (points.length < 2) return
            var plotX = 78
            var plotY = 8
            var plotW = Math.max(20, width - plotX - 10)
            var plotH = Math.max(20, height - plotY - 24)
            var bounds = paddedBounds(points)
            if (bounds === null) return
            drawScale(ctx, bounds, plotX, plotY, plotW, plotH)
            if (bounds.min < 0 && bounds.max > 0) {
                var zeroY = yFor(0, bounds.min, bounds.max, plotY, plotH)
                ctx.strokeStyle = "#8a92a0"
                ctx.lineWidth = 1
                ctx.beginPath()
                ctx.moveTo(plotX, zeroY)
                ctx.lineTo(plotX + plotW, zeroY)
                ctx.stroke()
            }
            ctx.strokeStyle = viewRoot.accentColor
            ctx.lineWidth = 2
            ctx.beginPath()
            var started = false
            for (var p = 0; p < points.length; ++p) {
                var pointValue = seriesValue(points[p])
                if (!isFinite(pointValue)) continue
                var x = plotX + (p / (points.length - 1)) * plotW
                var y = yFor(pointValue, bounds.min, bounds.max, plotY, plotH)
                if (!started) { ctx.moveTo(x, y); started = true }
                else ctx.lineTo(x, y)
            }
            if (started) ctx.stroke()
            var lastValue = NaN
            for (var last = points.length - 1; last >= 0; --last) {
                lastValue = seriesValue(points[last])
                if (isFinite(lastValue)) break
            }
            ctx.font = "11px sans-serif"
            ctx.textAlign = "left"
            ctx.textBaseline = "bottom"
            ctx.fillStyle = viewRoot.textColor
            if (isFinite(lastValue)) ctx.fillText(viewRoot.metricPointText(lastValue, viewRoot.backtestVm.selectedResultMetricKey), plotX, height - 2)
        }
    }

    Label {
        anchors.centerIn: parent
        visible: viewRoot.backtestVm.selectedResultMetricSeries.length < 2
        text: "Для этой карточки есть только итоговое значение, без временного ряда."
        color: viewRoot.mutedTextColor
        font.pixelSize: 12
    }
}
