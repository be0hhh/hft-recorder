import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HftRecorder 1.0

ColumnLayout {
    id: sweeps
    required property var viewRoot

    function requestPaint() {
        sweepCanvas.requestPaint()
        sweepHoverCanvas.requestPaint()
        distributionCanvas.requestPaint()
        distributionHoverCanvas.requestPaint()
    }

    visible: viewRoot.backtestVm.selectedIsSweep
    Layout.fillWidth: true
    Layout.fillHeight: true
    spacing: 8

    Label {
        visible: !viewRoot.backtestVm.selectedDetailsLoaded
        Layout.fillWidth: true
        Layout.fillHeight: true
        text: viewRoot.backtestVm.selectedDetailsLoading ? "Loading visual data..." : "Load visual to open sweep charts."
        color: viewRoot.mutedTextColor
        font.pixelSize: 12
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
    }

    Rectangle {
        visible: viewRoot.backtestVm.selectedDetailsLoaded
        Layout.fillWidth: true
        Layout.fillHeight: true
        color: viewRoot.panelDeepColor
        border.color: viewRoot.borderColor
        radius: 6
        clip: true

        Canvas {
            id: sweepCanvas
            visible: viewRoot.backtestVm.selectedSweepView === "curves"
            anchors.fill: parent
            anchors.margins: 12
            property int hoverPointId: -1
            property int hoverCurveIndex: -1
            property int hoverStep: -1
            property real hoverX: 0
            property real hoverY: 0
            property var sweepCurves: viewRoot.backtestVm.selectedSweepCurves
            property string sweepStateKey: ""
            property int sweepSteps: 1
            property var sweepBounds: ({ "min": 0, "max": 1 })
            property var sweepCurveValues: []

            Connections {
                target: viewRoot.backtestVm
                function onSelectionChanged() { sweepCanvas.clearSweepState(); sweepCanvas.clearHover(); sweepCanvas.requestPaint(); sweepHoverCanvas.requestPaint() }
            }
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()

            function maxSteps(curves) {
                var max = 1
                for (var i = 0; i < curves.length; ++i) max = Math.max(max, curves[i].curve.length)
                return max
            }

            function curveValues(curve) {
                var values = []
                for (var i = 0; i < curve.curve.length; ++i) values.push(viewRoot.sweepValue(curve.curve[i], curve.initialBalanceE8))
                return values
            }

            function curveValue(values, step) {
                if (values.length === 0) return 0
                var idx = Math.min(step, values.length - 1)
                return values[idx]
            }

            function paddedBounds(curveValues, steps) {
                var minPnl = 0
                var maxPnl = 0
                var has = false
                for (var c = 0; c < curveValues.length; ++c) {
                    for (var s = 0; s < steps; ++s) {
                        var value = curveValue(curveValues[c], s)
                        if (!has) { minPnl = value; maxPnl = value; has = true }
                        minPnl = Math.min(minPnl, value)
                        maxPnl = Math.max(maxPnl, value)
                    }
                }
                minPnl = Math.min(minPnl, 0)
                maxPnl = Math.max(maxPnl, 0)
                var span = maxPnl - minPnl
                if (span <= 0) span = viewRoot.sweepPercentMode ? 2.0 : 200000000
                var pad = Math.max(viewRoot.sweepPercentMode ? 0.01 : 1000000, span * 0.08)
                return { min: minPnl - pad, max: maxPnl + pad }
            }

            function sweepStateCacheKey(curves) {
                var key = viewRoot.backtestVm.selectedRunId + ":" + viewRoot.backtestVm.selectedSweepMetric + ":" + viewRoot.backtestVm.selectedSweepCurveLimit + ":" + viewRoot.sweepPercentMode + ":" + curves.length
                if (curves.length > 0) {
                    var first = curves[0]
                    var last = curves[curves.length - 1]
                    key += ":" + first.pointId + ":" + first.curve.length + ":" + last.pointId + ":" + last.curve.length
                }
                return key
            }

            function ensureSweepState(curves) {
                var key = sweepStateCacheKey(curves)
                if (sweepStateKey === key) return
                sweepCurveValues = []
                for (var i = 0; i < curves.length; ++i) sweepCurveValues.push(curveValues(curves[i]))
                sweepSteps = maxSteps(curves)
                sweepBounds = paddedBounds(sweepCurveValues, sweepSteps)
                sweepStateKey = key
            }

            function clearSweepState() {
                sweepStateKey = ""
            }

            function yFor(value, minPnl, maxPnl, plotY, plotH) {
                return plotY + plotH - ((value - minPnl) / (maxPnl - minPnl)) * plotH
            }

            function xFor(step, steps, plotX, plotW) {
                if (steps <= 1) return plotX
                return plotX + (step / (steps - 1)) * plotW
            }

            function colorFor(index) {
                return viewRoot.sweepPalette[index % viewRoot.sweepPalette.length]
            }

            function drawScale(ctx, bounds, plotX, plotY, plotW, plotH) {
                ctx.font = "11px sans-serif"
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
                    ctx.fillText(viewRoot.sweepPercentMode ? value.toFixed(2) + "%" : viewRoot.e8Text(value), plotX - 8, y)
                }
                if (bounds.min < 0 && bounds.max > 0) {
                    var zeroY = yFor(0, bounds.min, bounds.max, plotY, plotH)
                    ctx.strokeStyle = "#8a92a0"
                    ctx.beginPath()
                    ctx.moveTo(plotX, zeroY)
                    ctx.lineTo(plotX + plotW, zeroY)
                    ctx.stroke()
                }
            }

            function drawCurve(ctx, curve, index, steps, bounds, plotX, plotY, plotW, plotH) {
                var values = sweepCurveValues[index] || []
                ctx.strokeStyle = colorFor(index)
                ctx.globalAlpha = viewRoot.selectedSweepPointId < 0 || viewRoot.selectedSweepPointId === curve.pointId ? 0.95 : 0.35
                ctx.lineWidth = viewRoot.selectedSweepPointId === curve.pointId ? 3 : 1.7
                ctx.beginPath()
                for (var step = 0; step < steps; ++step) {
                    var x = xFor(step, steps, plotX, plotW)
                    var y = yFor(curveValue(values, step), bounds.min, bounds.max, plotY, plotH)
                    if (step === 0) ctx.moveTo(x, y)
                    else ctx.lineTo(x, y)
                }
                ctx.stroke()
                ctx.globalAlpha = 1.0
            }

            function updateHover(mx, my) {
                var curves = sweepCurves
                if (curves.length === 0) { clearHover(); return }
                var plotX = 66
                var plotY = 8
                var plotW = Math.max(20, width - plotX - 10)
                var plotH = Math.max(20, height - plotY - 22)
                if (mx < plotX || mx > plotX + plotW || my < plotY || my > plotY + plotH) { clearHover(); return }
                ensureSweepState(curves)
                var steps = sweepSteps
                var bounds = sweepBounds
                var bestCurve = -1
                var bestStep = -1
                var bestDistance = 999999
                var centerStep = steps <= 1 ? 0 : Math.round(((mx - plotX) / plotW) * (steps - 1))
                var firstStep = Math.max(0, centerStep - 1)
                var lastStep = Math.min(steps - 1, centerStep + 1)
                for (var c = 0; c < curves.length; ++c) {
                    var values = sweepCurveValues[c] || []
                    for (var s = firstStep; s <= lastStep; ++s) {
                        var x = xFor(s, steps, plotX, plotW)
                        var y = yFor(curveValue(values, s), bounds.min, bounds.max, plotY, plotH)
                        var dx = x - mx
                        var dy = y - my
                        var d = dx * dx + dy * dy
                        if (d < bestDistance) { bestDistance = d; bestCurve = c; bestStep = s }
                    }
                }
                if (bestCurve < 0 || bestDistance > 900) { clearHover(); return }
                var pointId = curves[bestCurve].pointId
                if (hoverPointId === pointId && hoverStep === bestStep) return
                hoverPointId = pointId
                hoverCurveIndex = bestCurve
                hoverStep = bestStep
                hoverX = xFor(bestStep, steps, plotX, plotW)
                hoverY = yFor(curveValue(sweepCurveValues[bestCurve] || [], bestStep), bounds.min, bounds.max, plotY, plotH)
                sweepHoverCanvas.requestPaint()
            }

            function clearHover() {
                if (hoverPointId < 0) return
                hoverPointId = -1
                hoverCurveIndex = -1
                hoverStep = -1
                sweepHoverCanvas.requestPaint()
            }

            function selectHover() {
                if (hoverPointId < 0) return
                viewRoot.selectedSweepPointId = hoverPointId
                viewRoot.backtestVm.applySweepPointById(hoverPointId)
                requestPaint()
                sweepHoverCanvas.requestPaint()
            }

            function drawHover(ctx, curves, steps, bounds, plotX, plotY, plotW, plotH) {
                if (hoverCurveIndex < 0 || hoverCurveIndex >= curves.length) return
                var curve = curves[hoverCurveIndex]
                var value = curveValue(sweepCurveValues[hoverCurveIndex] || [], hoverStep)
                ctx.strokeStyle = "rgba(245,245,245,0.42)"
                ctx.lineWidth = 1
                ctx.beginPath()
                ctx.moveTo(hoverX, plotY)
                ctx.lineTo(hoverX, plotY + plotH)
                ctx.moveTo(plotX, hoverY)
                ctx.lineTo(plotX + plotW, hoverY)
                ctx.stroke()
                ctx.fillStyle = colorFor(hoverCurveIndex)
                ctx.beginPath()
                ctx.arc(hoverX, hoverY, 4, 0, Math.PI * 2)
                ctx.fill()

                var cardW = 260
                var cardH = 82
                var cardX = Math.min(plotX + plotW - cardW - 8, hoverX + 12)
                if (cardX < plotX + 8) cardX = plotX + 8
                var cardY = Math.max(plotY + 8, hoverY - cardH - 12)
                ctx.fillStyle = "rgba(16, 17, 21, 0.94)"
                ctx.fillRect(cardX, cardY, cardW, cardH)
                ctx.strokeStyle = "rgba(138, 146, 160, 0.85)"
                ctx.strokeRect(cardX, cardY, cardW, cardH)
                ctx.font = "11px sans-serif"
                ctx.textAlign = "left"
                ctx.textBaseline = "top"
                ctx.fillStyle = viewRoot.textColor
                ctx.fillText("#" + curve.pointId + " step " + (hoverStep + 1), cardX + 10, cardY + 8)
                ctx.fillStyle = Number(curve.totalPnlE8) < 0 ? viewRoot.badColor : viewRoot.goodColor
                ctx.fillText((curve.metricLabel || "PnL") + " " + viewRoot.sweepText(value, curve.initialBalanceE8), cardX + 10, cardY + 28)
                ctx.fillStyle = viewRoot.mutedTextColor
                ctx.fillText(curve.label || "", cardX + 10, cardY + 50)
            }

            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                var curves = sweepCurves
                var plotX = 66
                var plotY = 8
                var plotW = Math.max(20, width - plotX - 10)
                var plotH = Math.max(20, height - plotY - 22)
                ensureSweepState(curves)
                var steps = sweepSteps
                var bounds = sweepBounds
                drawScale(ctx, bounds, plotX, plotY, plotW, plotH)
                for (var i = curves.length - 1; i >= 0; --i) drawCurve(ctx, curves[i], i, steps, bounds, plotX, plotY, plotW, plotH)
                ctx.fillStyle = viewRoot.mutedTextColor
                ctx.font = "11px sans-serif"
                ctx.textAlign = "center"
                ctx.fillText("fills / equity steps", plotX + plotW / 2, plotY + plotH + 16)
            }
        }

        Canvas {
            id: sweepHoverCanvas
            visible: viewRoot.backtestVm.selectedSweepView === "curves"
            anchors.fill: sweepCanvas
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                var curves = sweepCanvas.sweepCurves
                if (curves.length === 0) return
                var plotX = 66
                var plotY = 8
                var plotW = Math.max(20, width - plotX - 10)
                var plotH = Math.max(20, height - plotY - 22)
                sweepCanvas.ensureSweepState(curves)
                var steps = sweepCanvas.sweepSteps
                var bounds = sweepCanvas.sweepBounds
                sweepCanvas.drawHover(ctx, curves, steps, bounds, plotX, plotY, plotW, plotH)
            }
        }

        Timer {
            id: sweepHoverTimer
            interval: 40
            repeat: false
            onTriggered: {
                if (sweepMouse.hoverPending) {
                    sweepMouse.hoverPending = false
                    sweepCanvas.updateHover(sweepMouse.pendingX, sweepMouse.pendingY)
                }
            }
        }

        MouseArea {
            id: sweepMouse
            visible: viewRoot.backtestVm.selectedSweepView === "curves"
            anchors.fill: sweepHoverCanvas
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton
            property bool hoverPending: false
            property real pendingX: 0
            property real pendingY: 0
            property real lastHoverX: -1000000
            property real lastHoverY: -1000000
            onPositionChanged: function(mouse) {
                var dx = mouse.x - lastHoverX
                var dy = mouse.y - lastHoverY
                if (dx * dx + dy * dy < 0.25) return
                lastHoverX = mouse.x
                lastHoverY = mouse.y
                pendingX = mouse.x
                pendingY = mouse.y
                hoverPending = true
                if (!sweepHoverTimer.running) sweepHoverTimer.start()
            }
            onClicked: sweepCanvas.selectHover()
            onExited: {
                hoverPending = false
                lastHoverX = -1000000
                lastHoverY = -1000000
                sweepHoverTimer.stop()
                sweepCanvas.clearHover()
            }
        }

        Canvas {
            id: distributionCanvas
            visible: viewRoot.backtestVm.selectedSweepView === "distribution"
            anchors.fill: parent
            anchors.margins: 12
            property int hoverPointId: -1
            property int hoverIndex: -1
            property real hoverX: 0
            property real hoverY: 0
            property var bars: viewRoot.backtestVm.selectedSweepDistributionBars
            property string stateKey: ""
            property var bounds: ({ "min": 0, "max": 1 })
            property var layoutRows: []
            property var barValues: []

            Connections {
                target: viewRoot.backtestVm
                function onSelectionChanged() { distributionCanvas.clearState(); distributionCanvas.clearHover(); distributionCanvas.requestPaint(); distributionHoverCanvas.requestPaint() }
            }
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()

            function barValue(bar) { return viewRoot.sweepValue(bar.metricRaw, bar.initialBalanceE8 || viewRoot.backtestVm.selectedInitialBalanceE8) }

            function stateCacheKey(rows) {
                var key = viewRoot.backtestVm.selectedRunId + ":" + viewRoot.backtestVm.selectedSweepMetric + ":" + viewRoot.backtestVm.selectedSweepDistributionParam + ":" + viewRoot.sweepPercentMode + ":" + rows.length
                if (rows.length > 0) key += ":" + rows[0].pointId + ":" + rows[rows.length - 1].pointId
                return key
            }

            function ensureState(rows, plotX, plotY, plotW, plotH) {
                var key = stateCacheKey(rows) + ":" + Math.round(plotW) + ":" + Math.round(plotH)
                if (stateKey === key) return
                barValues = []
                for (var i = 0; i < rows.length; ++i) barValues.push(barValue(rows[i]))
                var minValue = 0
                var maxValue = 0
                var has = false
                for (var i = 0; i < rows.length; ++i) {
                    var value = barValues[i]
                    if (!has) { minValue = value; maxValue = value; has = true }
                    minValue = Math.min(minValue, value)
                    maxValue = Math.max(maxValue, value)
                }
                minValue = Math.min(minValue, 0)
                maxValue = Math.max(maxValue, 0)
                var span = maxValue - minValue
                if (span <= 0) span = viewRoot.sweepPercentMode ? 2.0 : 200000000
                var pad = Math.max(viewRoot.sweepPercentMode ? 0.01 : 1000000, span * 0.08)
                bounds = { min: minValue - pad, max: maxValue + pad }
                layoutRows = []
                var groupCount = 0
                var lastParam = null
                for (var g = 0; g < rows.length; ++g) {
                    if (lastParam === null || rows[g].paramRaw !== lastParam) { ++groupCount; lastParam = rows[g].paramRaw }
                }
                var groupGap = Math.min(14, Math.max(4, plotW * 0.01))
                var totalGap = Math.max(0, groupCount - 1) * groupGap
                var slotW = rows.length > 0 ? Math.max(2, (plotW - totalGap) / rows.length) : 2
                var barW = Math.max(4, Math.min(30, slotW * 0.92))
                var x = plotX
                lastParam = null
                for (var b = 0; b < rows.length; ++b) {
                    if (lastParam !== null && rows[b].paramRaw !== lastParam) x += groupGap
                    var centerX = x + slotW / 2
                    layoutRows.push({ x: centerX - barW / 2, centerX: centerX, width: barW, bar: rows[b] })
                    x += slotW
                    lastParam = rows[b].paramRaw
                }
                stateKey = key
            }

            function clearState() { stateKey = "" }

            function yFor(value, minValue, maxValue, plotY, plotH) {
                return plotY + plotH - ((value - minValue) / (maxValue - minValue)) * plotH
            }

            function drawScale(ctx, currentBounds, plotX, plotY, plotW, plotH) {
                ctx.font = "11px sans-serif"
                ctx.textAlign = "right"
                ctx.textBaseline = "middle"
                for (var i = 0; i <= 4; ++i) {
                    var value = currentBounds.max - ((currentBounds.max - currentBounds.min) * i / 4)
                    var y = plotY + (plotH * i / 4)
                    ctx.strokeStyle = "#2f333d"
                    ctx.lineWidth = 1
                    ctx.beginPath()
                    ctx.moveTo(plotX, y)
                    ctx.lineTo(plotX + plotW, y)
                    ctx.stroke()
                    ctx.fillStyle = viewRoot.mutedTextColor
                    ctx.fillText(viewRoot.sweepPercentMode ? value.toFixed(2) + "%" : viewRoot.e8Text(value), plotX - 8, y)
                }
                var zeroY = yFor(0, currentBounds.min, currentBounds.max, plotY, plotH)
                ctx.strokeStyle = "#8a92a0"
                ctx.beginPath()
                ctx.moveTo(plotX, zeroY)
                ctx.lineTo(plotX + plotW, zeroY)
                ctx.stroke()
            }

            function drawBars(ctx, rows, currentBounds, plotX, plotY, plotW, plotH) {
                var zeroY = yFor(0, currentBounds.min, currentBounds.max, plotY, plotH)
                var lastLabel = ""
                var lastLabelX = -100000
                for (var i = 0; i < layoutRows.length; ++i) {
                    var item = layoutRows[i]
                    var bar = item.bar
                    var value = barValues[i]
                    var y = yFor(value, currentBounds.min, currentBounds.max, plotY, plotH)
                    var top = Math.min(y, zeroY)
                    var h = Math.max(1, Math.abs(zeroY - y))
                    ctx.globalAlpha = viewRoot.selectedSweepPointId < 0 || viewRoot.selectedSweepPointId === bar.pointId ? 0.95 : 0.45
                    ctx.fillStyle = value < 0 ? viewRoot.badColor : viewRoot.goodColor
                    ctx.fillRect(item.x, top, item.width, h)
                    ctx.globalAlpha = 1.0
                    if (bar.paramText !== lastLabel) {
                        if (item.centerX - lastLabelX >= 46) {
                            ctx.fillStyle = viewRoot.mutedTextColor
                            ctx.font = "11px sans-serif"
                            ctx.textAlign = "center"
                            ctx.textBaseline = "top"
                            ctx.fillText(bar.paramText, item.centerX, plotY + plotH + 8)
                            lastLabelX = item.centerX
                        }
                        lastLabel = bar.paramText
                    }
                }
            }

            function updateHover(mx, my) {
                var rows = bars
                if (rows.length === 0) { clearHover(); return }
                var plotX = 66
                var plotY = 8
                var plotW = Math.max(20, width - plotX - 10)
                var plotH = Math.max(20, height - plotY - 50)
                ensureState(rows, plotX, plotY, plotW, plotH)
                var zeroY = yFor(0, bounds.min, bounds.max, plotY, plotH)
                var best = -1
                for (var i = 0; i < layoutRows.length; ++i) {
                    var item = layoutRows[i]
                    if (mx < item.x - 2 || mx > item.x + item.width + 2) continue
                    var y = yFor(barValues[i], bounds.min, bounds.max, plotY, plotH)
                    var top = Math.min(y, zeroY)
                    var bottom = Math.max(y, zeroY)
                    if (my >= top - 3 && my <= bottom + 3) { best = i; break }
                }
                if (best < 0) { clearHover(); return }
                hoverIndex = best
                hoverPointId = layoutRows[best].bar.pointId
                hoverX = layoutRows[best].centerX
                hoverY = yFor(barValues[best], bounds.min, bounds.max, plotY, plotH)
                distributionHoverCanvas.requestPaint()
            }

            function clearHover() {
                if (hoverPointId < 0) return
                hoverPointId = -1
                hoverIndex = -1
                distributionHoverCanvas.requestPaint()
            }

            function selectHover() {
                if (hoverPointId < 0) return
                viewRoot.selectedSweepPointId = hoverPointId
                requestPaint()
                distributionHoverCanvas.requestPaint()
            }

            function drawHover(ctx, plotX, plotY, plotW, plotH) {
                if (hoverIndex < 0 || hoverIndex >= layoutRows.length) return
                var bar = layoutRows[hoverIndex].bar
                var value = barValues[hoverIndex]
                ctx.strokeStyle = "rgba(245,245,245,0.42)"
                ctx.lineWidth = 1
                ctx.beginPath()
                ctx.moveTo(hoverX, plotY)
                ctx.lineTo(hoverX, plotY + plotH)
                ctx.moveTo(plotX, hoverY)
                ctx.lineTo(plotX + plotW, hoverY)
                ctx.stroke()
                var cardW = 280
                var cardH = 88
                var cardX = Math.min(plotX + plotW - cardW - 8, hoverX + 12)
                if (cardX < plotX + 8) cardX = plotX + 8
                var cardY = Math.max(plotY + 8, hoverY - cardH - 12)
                ctx.fillStyle = "rgba(16, 17, 21, 0.94)"
                ctx.fillRect(cardX, cardY, cardW, cardH)
                ctx.strokeStyle = "rgba(138, 146, 160, 0.85)"
                ctx.strokeRect(cardX, cardY, cardW, cardH)
                ctx.font = "11px sans-serif"
                ctx.textAlign = "left"
                ctx.textBaseline = "top"
                ctx.fillStyle = viewRoot.textColor
                ctx.fillText("#" + bar.pointId + "  " + bar.paramKey + "=" + bar.paramText, cardX + 10, cardY + 8)
                ctx.fillStyle = Number(bar.metricRaw) < 0 ? viewRoot.badColor : viewRoot.goodColor
                ctx.fillText("PnL " + viewRoot.sweepText(value, viewRoot.backtestVm.selectedInitialBalanceE8), cardX + 10, cardY + 28)
                ctx.fillStyle = viewRoot.mutedTextColor
                ctx.fillText(bar.label || "", cardX + 10, cardY + 50)
            }

            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                var rows = bars
                var plotX = 66
                var plotY = 8
                var plotW = Math.max(20, width - plotX - 10)
                var plotH = Math.max(20, height - plotY - 50)
                ensureState(rows, plotX, plotY, plotW, plotH)
                drawScale(ctx, bounds, plotX, plotY, plotW, plotH)
                drawBars(ctx, rows, bounds, plotX, plotY, plotW, plotH)
                ctx.fillStyle = viewRoot.mutedTextColor
                ctx.font = "11px sans-serif"
                ctx.textAlign = "center"
                ctx.fillText(viewRoot.backtestVm.selectedSweepDistributionParam, plotX + plotW / 2, plotY + plotH + 42)
            }
        }

        Canvas {
            id: distributionHoverCanvas
            visible: viewRoot.backtestVm.selectedSweepView === "distribution"
            anchors.fill: distributionCanvas
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                if (distributionCanvas.bars.length === 0) return
                var plotX = 66
                var plotY = 8
                var plotW = Math.max(20, width - plotX - 10)
                var plotH = Math.max(20, height - plotY - 50)
                distributionCanvas.ensureState(distributionCanvas.bars, plotX, plotY, plotW, plotH)
                distributionCanvas.drawHover(ctx, plotX, plotY, plotW, plotH)
            }
        }

        MouseArea {
            id: distributionMouse
            visible: viewRoot.backtestVm.selectedSweepView === "distribution"
            anchors.fill: distributionHoverCanvas
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton
            onPositionChanged: function(mouse) { distributionCanvas.updateHover(mouse.x, mouse.y) }
            onClicked: distributionCanvas.selectHover()
            onExited: distributionCanvas.clearHover()
        }

        Label {
            anchors.centerIn: parent
            visible: (viewRoot.backtestVm.selectedSweepView === "curves" && sweepCanvas.sweepCurves.length === 0) ||
                     (viewRoot.backtestVm.selectedSweepView === "distribution" && distributionCanvas.bars.length === 0)
            text: viewRoot.backtestVm.selectedSweepView === "distribution" ? "No sweep distribution" : "No sweep curves"
            color: viewRoot.mutedTextColor
            font.pixelSize: 14
        }
    }
}
