import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HftRecorder 1.0

Pane {
    id: root
    padding: 0

    required property var backtestVm
    required property bool tabActive

    property color windowColor: "#111216"
    property color chromeColor: "#1b1d23"
    property color panelColor: "#22252d"
    property color panelDeepColor: "#15171c"
    property color borderColor: "#343844"
    property color textColor: "#f1f4f8"
    property color mutedTextColor: "#a8afbd"
    property color accentColor: "#24c2cb"
    property color badColor: "#ef6f6c"
    property color grossColor: "#ef6f6c"
    property color netColor: "#9b78d6"
    property color feesColor: "#ff8f5c"
    property bool pnlPercentMode: false

    function e8Text(value) {
        var negative = value < 0
        var n = Math.abs(value)
        var whole = Math.floor(n / 100000000)
        var frac = Math.floor(n % 100000000).toString().padStart(8, "0")
        while (frac.length > 0 && frac.endsWith("0")) frac = frac.slice(0, -1)
        return (negative ? "-" : "") + whole + (frac.length > 0 ? "." + frac : "")
    }

    function effectivePercentMode() {
        return root.pnlPercentMode && root.backtestVm.selectedInitialBalanceE8 > 0
    }

    function chartValue(valueE8) {
        if (!root.effectivePercentMode()) return valueE8
        return (valueE8 * 100.0) / root.backtestVm.selectedInitialBalanceE8
    }

    function chartText(value) {
        if (!root.effectivePercentMode()) return (value / 100000000.0).toFixed(3)
        var n = Math.abs(value)
        var digits = n < 1.0 ? 4 : 2
        return value.toFixed(digits) + "%"
    }

    function pnlTextE8(valueE8) {
        return root.chartText(root.chartValue(valueE8))
    }
    function realizedRangeText() {
        var points = root.backtestVm.selectedEquityPoints
        if (points.length <= 0) return "no equity points"
        var minPnl = root.chartValue(points[0].realizedPnlE8)
        var maxPnl = minPnl
        for (var i = 0; i < points.length; ++i) {
            var realized = root.chartValue(points[i].realizedPnlE8)
            var gross = root.chartValue(points[i].grossRealizedPnlE8)
            var fees = root.chartValue(points[i].feesPaidE8)
            minPnl = Math.min(minPnl, realized, gross, fees)
            maxPnl = Math.max(maxPnl, realized, gross, fees)
        }
        return "range " + root.chartText(minPnl) + " .. " + root.chartText(maxPnl)
    }

    function syncSelections() {
        sessionBox.currentIndex = sessionBox.indexOfValue(root.backtestVm.selectedSessionId)
        runBox.currentIndex = runBox.indexOfValue(root.backtestVm.selectedRunId)
    }
    background: Rectangle { color: root.windowColor }

    Connections {
        target: root.backtestVm
        function onSessionsChanged() { root.syncSelections() }
        function onSelectedSessionChanged() { root.syncSelections() }
        function onRunsChanged() { root.syncSelections() }
        function onSelectionChanged() { root.syncSelections() }
    }
    component ActionButton: Rectangle {
        property string text: ""
        property bool enabledValue: true
        signal clicked()
        radius: 6
        implicitWidth: Math.max(78, label.implicitWidth + 18)
        implicitHeight: 30
        Layout.preferredWidth: implicitWidth
        Layout.preferredHeight: implicitHeight
        color: enabledValue ? (mouse.containsMouse ? "#2b303a" : root.panelColor) : root.panelDeepColor
        border.color: enabledValue ? root.accentColor : root.borderColor
        border.width: 1
        opacity: enabledValue ? 1.0 : 0.5
        Text { id: label; anchors.centerIn: parent; width: parent.width - 12; text: parent.text; color: enabledValue ? root.textColor : root.mutedTextColor; font.pixelSize: 11; font.bold: true; elide: Text.ElideRight; horizontalAlignment: Text.AlignHCenter }
        MouseArea { id: mouse; anchors.fill: parent; hoverEnabled: true; enabled: parent.enabledValue; onClicked: parent.clicked() }
    }

    component LegendChip: Rectangle {
        property string text: ""
        property color lineColor: root.accentColor
        property bool checked: true
        signal toggled()
        radius: 5
        implicitWidth: Math.max(92, row.implicitWidth + 14)
        implicitHeight: 26
        Layout.preferredWidth: implicitWidth
        Layout.preferredHeight: implicitHeight
        color: mouse.containsMouse ? "#2b303a" : root.panelDeepColor
        border.color: checked ? lineColor : root.borderColor
        opacity: checked ? 1.0 : 0.45
        RowLayout {
            id: row
            anchors.centerIn: parent
            spacing: 6
            Rectangle { Layout.preferredWidth: 18; Layout.preferredHeight: 3; radius: 2; color: lineColor }
            Label { text: parent.parent.text; color: root.textColor; font.pixelSize: 11; font.bold: true }
        }
        MouseArea { id: mouse; anchors.fill: parent; hoverEnabled: true; onClicked: parent.toggled() }
    }

    component KpiCard: Rectangle {
        property var metric: ({})
        visible: metric.primary === true
        width: visible ? 136 : 0
        height: visible ? 52 : 0
        radius: 7
        color: root.panelDeepColor
        border.color: root.borderColor
        border.width: 1
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 2
            Label { text: metric.value || ""; color: root.textColor; font.pixelSize: 14; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
            Label { text: metric.label || ""; color: root.mutedTextColor; font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 10

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 66
            color: root.chromeColor
            border.color: root.borderColor
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 12
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Label { text: "Backtest Results"; color: root.textColor; font.pixelSize: 17; font.bold: true }
                    Label { text: root.backtestVm.selectedRunId === "" ? "Select or run a backtest" : root.backtestVm.selectedRunId + " / " + root.backtestVm.selectedStrategy; color: root.mutedTextColor; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true }
                }
                RecorderComboBox {
                    id: sessionBox
                    Layout.fillWidth: false
                    Layout.preferredWidth: 360
                    caption: "Session"
                    textRole: "label"
                    valueRole: "id"
                    model: root.backtestVm.sessions
                    popupWidth: 640
                    onActivated: root.backtestVm.setSelectedSessionId(currentValue)
                    Component.onCompleted: root.syncSelections()
                }
                RecorderComboBox {
                    id: runBox
                    Layout.fillWidth: false
                    Layout.preferredWidth: 260
                    caption: "Backtest"
                    textRole: "label"
                    valueRole: "runId"
                    model: root.backtestVm.runs
                    popupWidth: 420
                    popupMaxWidth: root.width - 32
                    popupAlignRight: true
                    colorizeRightText: true
                    onActivated: root.backtestVm.selectRun(currentValue)
                    Component.onCompleted: root.syncSelections()
                }
                ActionButton { text: "Refresh"; onClicked: root.backtestVm.refreshResults() }
                ActionButton {
                    text: root.backtestVm.selectedDetailsLoading ? "Loading" : (root.backtestVm.selectedDetailsLoaded ? "Visual loaded" : "Load visual")
                    visible: root.backtestVm.hasSelection
                    enabledValue: !root.backtestVm.selectedDetailsLoading && !root.backtestVm.selectedDetailsLoaded
                    onClicked: root.backtestVm.loadSelectedRunDetails()
                }
                ActionButton { visible: false; text: "Raw"; enabledValue: false }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.bottomMargin: 10
            spacing: 10

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                color: root.panelColor
                border.color: root.borderColor
                radius: 6

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10

                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Realized PnL"; color: root.textColor; font.pixelSize: 15; font.bold: true; Layout.fillWidth: true }
                        Label { text: root.backtestVm.hasEquityPoints ? root.realizedRangeText() : "no equity points"; color: root.mutedTextColor; font.pixelSize: 12 }
                    }

                    Flow {
                        visible: root.backtestVm.selectedResultMetrics.length > 0
                        Layout.fillWidth: true
                        Layout.preferredHeight: childrenRect.height
                        spacing: 8
                        Repeater {
                            model: root.backtestVm.selectedResultMetrics
                            delegate: KpiCard { metric: modelData }
                        }
                    }
                    Label {
                        visible: root.backtestVm.selectedResultMetrics.length <= 0
                        text: root.backtestVm.selectedDetailsLoading ? "Loading visual data..." : (!root.backtestVm.selectedDetailsLoaded ? "Load visual to show risk/execution KPIs" : "Selected run has no risk/execution KPIs")
                        color: root.mutedTextColor
                        font.pixelSize: 12
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        LegendChip { text: "Gross realized"; lineColor: root.grossColor; checked: true; onToggled: {} }
                        LegendChip { text: "Realized PnL"; lineColor: root.netColor; checked: true; onToggled: {} }
                        LegendChip { text: "Fees paid"; lineColor: root.feesColor; checked: true; onToggled: {} }
                        ActionButton { text: root.effectivePercentMode() ? "PnL %" : "PnL $"; enabledValue: root.backtestVm.selectedDetailsLoaded && root.backtestVm.selectedInitialBalanceE8 > 0; onClicked: { root.pnlPercentMode = !root.pnlPercentMode; pnlCanvas.requestPaint(); hoverCanvas.requestPaint() } }
                        Item { Layout.fillWidth: true }
                    }

                    Rectangle {
                        id: chartFrame
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        Layout.minimumHeight: 280
                        color: root.panelDeepColor
                        border.color: root.borderColor
                        radius: 6
                        clip: true

                        Canvas {
                            id: pnlCanvas
                            anchors.fill: parent
                            anchors.margins: 12
                            property bool hoverActive: false
                            property int hoverIndex: -1
                            property real hoverX: 0
                            property real hoverY: 0

                            Connections {
                                target: root.backtestVm
                                function onSelectionChanged() { pnlCanvas.clearHover(); pnlCanvas.requestPaint(); hoverCanvas.requestPaint() }
                            }
                            onWidthChanged: requestPaint()
                            onHeightChanged: requestPaint()

                            function paddedBounds(minPnl, maxPnl) {
                                minPnl = Math.min(minPnl, 0)
                                maxPnl = Math.max(maxPnl, 0)
                                var span = maxPnl - minPnl
                                if (span <= 0) span = root.effectivePercentMode() ? 2.0 : 200000000
                                var minPad = root.effectivePercentMode() ? 0.01 : 1000000
                                var pad = Math.max(minPad, span * 0.08)
                                return { min: minPnl - pad, max: maxPnl + pad }
                            }

                            function seriesValue(point, field) {
                                return root.chartValue(point[field])
                            }

                            function yFor(value, minPnl, maxPnl, plotY, plotH) {
                                return plotY + plotH - ((value - minPnl) / (maxPnl - minPnl)) * plotH
                            }

                            function drawDashedHLine(ctx, x0, x1, y, dash, gap) {
                                var x = x0
                                while (x < x1) {
                                    var end = Math.min(x + dash, x1)
                                    ctx.beginPath()
                                    ctx.moveTo(x, y)
                                    ctx.lineTo(end, y)
                                    ctx.stroke()
                                    x = end + gap
                                }
                            }

                            function drawScale(ctx, minPnl, maxPnl, plotX, plotY, plotW, plotH) {
                                ctx.font = "11px sans-serif"
                                ctx.textAlign = "right"
                                ctx.textBaseline = "middle"
                                for (var i = 0; i <= 4; ++i) {
                                    var value = maxPnl - ((maxPnl - minPnl) * i / 4)
                                    var y = plotY + (plotH * i / 4)
                                    ctx.strokeStyle = "#2f333d"
                                    ctx.lineWidth = 1
                                    ctx.beginPath()
                                    ctx.moveTo(plotX, y)
                                    ctx.lineTo(plotX + plotW, y)
                                    ctx.stroke()
                                    ctx.fillStyle = root.mutedTextColor
                                    ctx.fillText(root.chartText(value), plotX - 8, y)
                                }
                                if (minPnl < 0 && maxPnl > 0) {
                                    var zeroY = yFor(0, minPnl, maxPnl, plotY, plotH)
                                    ctx.fillStyle = "rgba(255, 255, 255, 0.035)"
                                    ctx.fillRect(plotX, zeroY - 4, plotW, 8)
                                    ctx.strokeStyle = "#8a92a0"
                                    ctx.lineWidth = 1
                                    drawDashedHLine(ctx, plotX, plotX + plotW, zeroY, 7, 5)
                                    ctx.fillStyle = root.textColor
                                    ctx.fillText(root.effectivePercentMode() ? "0%" : "0", plotX - 8, zeroY)
                                }
                            }

                            function drawLine(ctx, points, field, color, minPnl, maxPnl, plotX, plotY, plotW, plotH) {
                                ctx.strokeStyle = color
                                ctx.lineWidth = 2
                                ctx.beginPath()
                                for (var i = 0; i < points.length; ++i) {
                                    var point = points[i]
                                    var x = plotX + (i / (points.length - 1)) * plotW
                                    var y = yFor(seriesValue(point, field), minPnl, maxPnl, plotY, plotH)
                                    if (i === 0) ctx.moveTo(x, y)
                                    else ctx.lineTo(x, y)
                                }
                                ctx.stroke()
                            }

                            function primaryField() {
                                return "realizedPnlE8"
                            }

                            function visibleBounds(points) {
                                var minPnl = seriesValue(points[0], "realizedPnlE8")
                                var maxPnl = minPnl
                                for (var p = 0; p < points.length; ++p) {
                                    var realized = seriesValue(points[p], "realizedPnlE8")
                                    var gross = seriesValue(points[p], "grossRealizedPnlE8")
                                    var fees = seriesValue(points[p], "feesPaidE8")
                                    minPnl = Math.min(minPnl, realized, gross, fees)
                                    maxPnl = Math.max(maxPnl, realized, gross, fees)
                                }
                                return paddedBounds(minPnl, maxPnl)
                            }

                            function clearHover() {
                                if (!hoverActive && hoverIndex < 0) return
                                hoverActive = false
                                hoverIndex = -1
                                hoverCanvas.requestPaint()
                            }

                            function updateHover(mx, my) {
                                var points = root.backtestVm.selectedEquityPoints
                                if (points.length < 2) { clearHover(); return }
                                var plotX = 62
                                var plotY = 8
                                var plotW = Math.max(20, width - plotX - 8)
                                var plotH = Math.max(20, height - plotY - 12)
                                if (mx < plotX || mx > plotX + plotW || my < plotY || my > plotY + plotH) { clearHover(); return }
                                var idx = Math.round(((mx - plotX) / plotW) * (points.length - 1))
                                idx = Math.max(0, Math.min(points.length - 1, idx))
                                if (hoverActive && hoverIndex === idx) return
                                var bounds = visibleBounds(points)
                                if (bounds === null) { clearHover(); return }
                                hoverActive = true
                                hoverIndex = idx
                                hoverX = plotX + (idx / (points.length - 1)) * plotW
                                hoverY = yFor(seriesValue(points[idx], primaryField()), bounds.min, bounds.max, plotY, plotH)
                                hoverCanvas.requestPaint()
                            }

                            function drawHover(ctx, points, minPnl, maxPnl, plotX, plotY, plotW, plotH) {
                                if (!hoverActive || hoverIndex < 0 || hoverIndex >= points.length) return
                                var point = points[hoverIndex]
                                var x = plotX + (hoverIndex / (points.length - 1)) * plotW
                                var y = yFor(seriesValue(point, primaryField()), minPnl, maxPnl, plotY, plotH)
                                ctx.strokeStyle = "rgba(245, 245, 245, 0.42)"
                                ctx.lineWidth = 1
                                ctx.beginPath()
                                ctx.moveTo(x, plotY)
                                ctx.lineTo(x, plotY + plotH)
                                ctx.moveTo(plotX, y)
                                ctx.lineTo(plotX + plotW, y)
                                ctx.stroke()

                                var cardX = plotX + 8
                                var cardH = 82
                                var cardY = plotY + plotH - cardH - 8
                                var cardW = 188
                                ctx.fillStyle = "rgba(16, 17, 21, 0.92)"
                                ctx.fillRect(cardX, cardY, cardW, cardH)
                                ctx.strokeStyle = "rgba(138, 146, 160, 0.85)"
                                ctx.strokeRect(cardX, cardY, cardW, cardH)
                                ctx.font = "11px sans-serif"
                                ctx.textAlign = "left"
                                ctx.textBaseline = "top"
                                ctx.fillStyle = root.textColor
                                ctx.fillText("Fills " + point.fillCount, cardX + 10, cardY + 8)
                                ctx.fillStyle = root.grossColor
                                ctx.fillText("Gross " + root.pnlTextE8(point.grossRealizedPnlE8), cardX + 10, cardY + 26)
                                ctx.fillStyle = root.netColor
                                ctx.fillText("Realized " + root.pnlTextE8(point.realizedPnlE8), cardX + 10, cardY + 44)
                                ctx.fillStyle = root.feesColor
                                ctx.fillText("Fees " + root.pnlTextE8(point.feesPaidE8), cardX + 10, cardY + 62)
                            }

                            onPaint: {
                                var ctx = getContext("2d")
                                ctx.clearRect(0, 0, width, height)
                                var plotX = 62
                                var plotY = 8
                                var plotW = Math.max(20, width - plotX - 8)
                                var plotH = Math.max(20, height - plotY - 12)

                                var points = root.backtestVm.selectedEquityPoints
                                if (points.length < 2) {
                                    drawScale(ctx, -100000000, 100000000, plotX, plotY, plotW, plotH)
                                    return
                                }
                                var bounds = visibleBounds(points)
                                if (bounds === null) return
                                var minPnl = bounds.min
                                var maxPnl = bounds.max
                                drawScale(ctx, minPnl, maxPnl, plotX, plotY, plotW, plotH)
                                drawLine(ctx, points, "grossRealizedPnlE8", root.grossColor, minPnl, maxPnl, plotX, plotY, plotW, plotH)
                                drawLine(ctx, points, "realizedPnlE8", root.netColor, minPnl, maxPnl, plotX, plotY, plotW, plotH)
                                drawLine(ctx, points, "feesPaidE8", root.feesColor, minPnl, maxPnl, plotX, plotY, plotW, plotH)
                            }
                        }

                        Canvas {
                            id: hoverCanvas
                            anchors.fill: pnlCanvas
                            onWidthChanged: requestPaint()
                            onHeightChanged: requestPaint()
                            onPaint: {
                                var ctx = getContext("2d")
                                ctx.clearRect(0, 0, width, height)
                                var points = root.backtestVm.selectedEquityPoints
                                if (points.length < 2) return
                                var plotX = 62
                                var plotY = 8
                                var plotW = Math.max(20, width - plotX - 8)
                                var plotH = Math.max(20, height - plotY - 12)
                                var bounds = pnlCanvas.visibleBounds(points)
                                if (bounds === null) return
                                pnlCanvas.drawHover(ctx, points, bounds.min, bounds.max, plotX, plotY, plotW, plotH)
                            }
                        }

                        Timer {
                            id: pnlHoverUpdateTimer
                            interval: 16
                            repeat: false
                            onTriggered: {
                                if (hoverMouseArea.hoverPending) {
                                    hoverMouseArea.hoverPending = false
                                    pnlCanvas.updateHover(hoverMouseArea.pendingHoverX, hoverMouseArea.pendingHoverY)
                                }
                            }
                        }

                        MouseArea {
                            id: hoverMouseArea
                            anchors.fill: hoverCanvas
                            hoverEnabled: true
                            acceptedButtons: Qt.NoButton
                            property bool hoverPending: false
                            property real pendingHoverX: 0
                            property real pendingHoverY: 0
                            onPositionChanged: function(mouse) {
                                pendingHoverX = mouse.x
                                pendingHoverY = mouse.y
                                hoverPending = true
                                if (!pnlHoverUpdateTimer.running) pnlHoverUpdateTimer.start()
                            }
                            onExited: {
                                hoverPending = false
                                pnlHoverUpdateTimer.stop()
                                pnlCanvas.clearHover()
                            }
                        }

                        Label {
                            anchors.centerIn: parent
                            visible: !root.backtestVm.hasEquityPoints
                            text: root.backtestVm.hasSelection
                                  ? (root.backtestVm.selectedDetailsLoaded ? "Selected run has no equity data" : "Load visual to show equity chart")
                                  : "No run selected"
                            color: root.mutedTextColor
                            font.pixelSize: 14
                        }
                    }

                    GridView {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 116
                        clip: true
                        cellWidth: 220
                        cellHeight: 52
                        model: root.backtestVm.selectedResultMetrics
                        delegate: Rectangle {
                            required property var modelData
                            width: GridView.view.cellWidth - 8
                            height: GridView.view.cellHeight - 8
                            radius: 6
                            color: root.panelDeepColor
                            border.color: root.borderColor
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 8
                                spacing: 2
                                Label { text: modelData.label; color: root.mutedTextColor; font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true }
                                Label { text: modelData.value; color: root.textColor; font.pixelSize: 15; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                            }
                        }
                    }
                }
            }

            Rectangle {
                id: rawDrawer
                visible: false
                Layout.preferredWidth: 420
                Layout.fillHeight: true
                color: root.panelColor
                border.color: root.borderColor
                radius: 6
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8
                    Label { text: "Raw result"; color: root.textColor; font.pixelSize: 15; font.bold: true }
                    TextArea {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        text: root.backtestVm.selectedJson
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.NoWrap
                        color: root.textColor
                        font.family: "monospace"
                        font.pixelSize: 11
                        background: Rectangle { color: root.panelDeepColor; border.color: root.borderColor; radius: 5 }
                    }
                }
            }
        }
    }
}
