import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HftRecorder 1.0

Pane {
    id: root
    padding: 0
    focus: true
    required property AppViewModel appVm
    required property CaptureViewModel captureVm

    property color windowColor: "#161616"
    property color chromeColor: "#202024"
    property color panelColor: "#2c2c2f"
    property color panelAltColor: "#35353a"
    property color borderColor: "#49494f"
    property color textColor: "#f5f5f5"
    property color mutedTextColor: "#aaaaaf"
    property color accentBuyColor: "#24c2cb"
    property color chartColor: "#202022"
    property color scaleColor: "#26262b"
    property int priceTickCount: 6
    property int timeTickCount: 5
    property string selectedSourceId: ""
    property bool userHasExplicitSelection: false
    property bool showTradesLayer: true
    property bool showOrderbookLayer: false
    property bool showBookTickerLayer: false
    property bool effectiveBookTickerLayer: showBookTickerLayer || showOrderbookLayer
    property bool useDedicatedGpuPath: false
    property bool useGpuRenderer: true

    function chartSurface() { return chartLoader.item }
    function syncRendererDiagnostics() { root.appVm.activeChartRenderer = root.useDedicatedGpuPath ? "gpu-orderbook" : "cpu-chart" }
    function syncLiveUpdateMode() { chart.setLiveUpdateIntervalMs(root.appVm.liveUpdateIntervalMs) }

    function applySourceSelection(sourceId) {
        interaction.clearSelectionVisual()
        chart.clearSelection()
        var sourceKind = sourcesModel.sourceKind(sourceId)
        if (sourceKind === "live") {
            chart.activateLiveSource(sourceId, sourcesModel.sessionPath(sourceId))
            return
        }
        if (sourceKind === "recorded") {
            chart.loadSession(sourcesModel.sessionPath(sourceId))
            return
        }
        chart.resetSession()
    }

    function liveSourceIndex() {
        for (var i = 0; i < sourcesModel.rowCount(); ++i) {
            var row = sourcesModel.index(i, 0)
            if (sourcesModel.groupAt(i) === "live")
                return i
        }
        return -1
    }

    function ensureVisibleLayerSelection() {
        if (chart.hasTrades) {
            if (!root.showTradesLayer && !root.showOrderbookLayer && !root.showBookTickerLayer)
                root.showTradesLayer = true
            return
        }

        if (root.showTradesLayer) {
            root.showTradesLayer = false
            if (chart.hasOrderbook)
                root.showOrderbookLayer = true
            else if (chart.hasBookTicker)
                root.showBookTickerLayer = true
        }

        if (!root.showTradesLayer && !root.showOrderbookLayer && !root.showBookTickerLayer) {
            if (chart.hasOrderbook)
                root.showOrderbookLayer = true
            else if (chart.hasBookTicker)
                root.showBookTickerLayer = true
            else
                root.showTradesLayer = true
        }
    }

    function ensureSourceSelection() {
        if (sessionToolbar.count() <= 0) {
            if (!root.userHasExplicitSelection) {
                root.selectedSourceId = ""
                chart.resetSession()
            }
            return
        }

        if (sourcesModel.hasSource(root.selectedSourceId)) {
            sessionToolbar.setCurrentIndex(sourcesModel.indexOfSource(root.selectedSourceId))
            if (!root.userHasExplicitSelection && chart.currentSourceId !== root.selectedSourceId)
                root.applySourceSelection(root.selectedSourceId)
            return
        }

        if (root.userHasExplicitSelection) {
            sessionToolbar.setCurrentIndex(-1)
            return
        }

        var desiredIndex = root.liveSourceIndex()
        if (desiredIndex < 0)
            desiredIndex = 0
        if (desiredIndex < 0)
            return

        sessionToolbar.setCurrentIndex(desiredIndex)
        var nextSourceId = sessionToolbar.currentSourceId()
        if (nextSourceId === "")
            return
        root.selectedSourceId = nextSourceId
        root.applySourceSelection(nextSourceId)
    }

    background: Rectangle { color: root.windowColor }

    ViewerSourceListModel {
        id: sourcesModel
        captureViewModel: root.captureVm
        Component.onCompleted: reload()
    }

    ChartController { id: chart }
    ViewerInteractionState { id: interaction }
    Timer { id: interactiveModeTimer; interval: 120; repeat: false; onTriggered: interaction.interactiveMode = false }

    Component.onCompleted: {
        Qt.callLater(root.ensureSourceSelection)
        Qt.callLater(root.syncRendererDiagnostics)
        Qt.callLater(root.syncLiveUpdateMode)
    }
    onUseDedicatedGpuPathChanged: root.syncRendererDiagnostics()

    Connections {
        target: root.appVm
        function onLiveUpdateModeChanged() { root.syncLiveUpdateMode() }
    }

    Connections {
        target: sourcesModel
        function onModelReset() { Qt.callLater(root.ensureSourceSelection) }
    }

    Connections {
        target: chart
        function onSessionChanged() { Qt.callLater(root.ensureVisibleLayerSelection) }
        function onLiveDataChanged() { Qt.callLater(root.ensureVisibleLayerSelection) }
    }

    Keys.onEscapePressed: {
        interaction.clearSelectionVisual()
        chart.clearSelection()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        ViewerSessionToolbar {
            id: sessionToolbar
            sourcesModel: sourcesModel
            selectedSourceId: root.selectedSourceId
            chromeColor: root.chromeColor
            panelColor: root.panelColor
            panelAltColor: root.panelAltColor
            borderColor: root.borderColor
            textColor: root.textColor
            mutedTextColor: root.mutedTextColor
            onSourceActivated: function(sourceId) {
                root.userHasExplicitSelection = true
                root.selectedSourceId = sourceId
                root.applySourceSelection(sourceId)
            }
            onReloadRequested: {
                sourcesModel.reload()
                Qt.callLater(root.ensureSourceSelection)
            }
            onSourceCountChanged: Qt.callLater(root.ensureSourceSelection)
        }

        ViewerLayerToolbar {
            appVm: root.appVm
            chart: chart
            interaction: interaction
            showTradesLayer: root.showTradesLayer
            showOrderbookLayer: root.showOrderbookLayer
            showBookTickerLayer: root.showBookTickerLayer
            effectiveBookTickerLayer: root.effectiveBookTickerLayer
            chromeColor: root.chromeColor
            panelColor: root.panelColor
            panelAltColor: root.panelAltColor
            borderColor: root.borderColor
            textColor: root.textColor
            mutedTextColor: root.mutedTextColor
            accentBuyColor: root.accentBuyColor
            onToggleTrades: {
                root.showTradesLayer = !root.showTradesLayer
                if (root.showTradesLayer && !chart.loaded && root.selectedSourceId !== "")
                    root.applySourceSelection(root.selectedSourceId)
            }
            onToggleOrderbook: root.showOrderbookLayer = !root.showOrderbookLayer
            onToggleBookTicker: root.showBookTickerLayer = !root.showBookTickerLayer
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Rectangle { anchors.fill: parent; color: root.chartColor }
            Item {
                id: plotFrame
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.right: priceScale.left
                anchors.bottom: timeScale.top
                clip: true
                Component {
                    id: cpuChartComponent
                    ChartItem {
                        anchors.fill: parent
                        controller: chart
                        tradesVisible: root.showTradesLayer
                        orderbookVisible: root.showOrderbookLayer
                        bookTickerVisible: root.effectiveBookTickerLayer
                        tradeAmountScale: root.appVm.tradeAmountScale
                        bookOpacityGain: root.appVm.bookBrightnessUsdRef
                        bookRenderDetail: root.appVm.bookMinVisibleUsd
                        bookDepthWindowPct: root.appVm.bookDepthWindowPct
                        interactiveMode: interaction.interactiveMode
                    }
                }
                Component {
                    id: gpuChartComponent
                    GpuChartItem {
                        anchors.fill: parent
                        controller: chart
                        tradesVisible: root.showTradesLayer
                        orderbookVisible: root.showOrderbookLayer
                        bookTickerVisible: root.effectiveBookTickerLayer
                        tradeAmountScale: root.appVm.tradeAmountScale
                        bookOpacityGain: root.appVm.bookBrightnessUsdRef
                        bookRenderDetail: root.appVm.bookMinVisibleUsd
                        bookDepthWindowPct: root.appVm.bookDepthWindowPct
                        interactiveMode: interaction.interactiveMode
                    }
                }
                Loader { id: chartLoader; anchors.fill: parent; sourceComponent: root.useDedicatedGpuPath ? gpuChartComponent : cpuChartComponent; onLoaded: root.syncRendererDiagnostics() }
                MouseArea {
                    anchors.fill: parent
                    property real lastX: 0
                    property real lastY: 0
                    property real pressX: 0
                    property real pressY: 0
                    property bool dragActive: false
                    property bool contextHoldActive: false
                    acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                    hoverEnabled: true
                    preventStealing: true
                    onPressed: function(mouse) {
                        if (mouse.button === Qt.RightButton) {
                            contextHoldActive = true
                            if (root.chartSurface()) root.chartSurface().activateContextPoint(mouse.x, mouse.y)
                            return
                        }
                        if (mouse.button === Qt.MiddleButton) {
                            interaction.startInteractiveMode(interactiveModeTimer)
                            interaction.beginSelection(mouse.x, mouse.y, 'middle_line')
                            if (root.chartSurface()) root.chartSurface().clearHover()
                            return
                        }
                        if ((mouse.modifiers & Qt.ShiftModifier) && mouse.button === Qt.LeftButton) {
                            interaction.startInteractiveMode(interactiveModeTimer)
                            interaction.beginSelection(mouse.x, mouse.y, 'shift_box')
                            if (root.chartSurface()) root.chartSurface().clearHover()
                            return
                        }
                        if ((mouse.modifiers & Qt.ControlModifier) && mouse.button === Qt.LeftButton) {
                            interaction.startInteractiveMode(interactiveModeTimer)
                            interaction.beginSelection(mouse.x, mouse.y, 'ctrl_hilo')
                            if (root.chartSurface()) root.chartSurface().clearHover()
                            return
                        }
                        interaction.plotDragging = true
                        dragActive = false
                        pressX = mouse.x
                        pressY = mouse.y
                        lastX = mouse.x
                        lastY = mouse.y
                        if (root.chartSurface() && !interaction.anyHoverableLayerVisible(root.showTradesLayer, root.effectiveBookTickerLayer, root.showOrderbookLayer)) root.chartSurface().clearHover()
                    }
                    onPositionChanged: function(mouse) {
                        if (interaction.rangeSelectionActive) {
                            interaction.updateSelection(mouse.x, mouse.y, plotFrame.width, plotFrame.height)
                            interaction.updateMeasurement(chart, plotFrame.width, plotFrame.height)
                            return
                        }
                        if (contextHoldActive || (mouse.buttons & Qt.RightButton)) {
                            contextHoldActive = true
                            if (root.chartSurface()) root.chartSurface().activateContextPoint(mouse.x, mouse.y)
                            return
                        }
                        if (mouse.buttons & Qt.LeftButton) {
                            if (!dragActive) {
                                var distance = Math.abs(mouse.x - pressX) + Math.abs(mouse.y - pressY)
                                if (distance < 4) return
                                dragActive = true
                                interaction.startInteractiveMode(interactiveModeTimer)
                                if (root.chartSurface()) root.chartSurface().clearHover()
                            }
                            var dx = mouse.x - lastX
                            var dy = mouse.y - lastY
                            lastX = mouse.x
                            lastY = mouse.y
                            chart.panTime(-dx / Math.max(1, width))
                            chart.panPrice(dy / Math.max(1, height))
                            return
                        }
                        if (interaction.priceScaleDragging || interaction.timeScaleDragging) return
                        if (!interaction.anyHoverableLayerVisible(root.showTradesLayer, root.effectiveBookTickerLayer, root.showOrderbookLayer)) return
                        if (root.chartSurface()) root.chartSurface().setHoverPoint(mouse.x, mouse.y)
                    }
                    onReleased: {
                        if (interaction.rangeSelectionActive) {
                            interaction.finishMeasurement(chart)
                            interaction.stopInteractiveModeSoon(interactiveModeTimer)
                            return
                        }
                        if (contextHoldActive) {
                            contextHoldActive = false
                            if (root.chartSurface()) root.chartSurface().clearHover()
                        }
                        interaction.plotDragging = false
                        if (dragActive) interaction.stopInteractiveModeSoon(interactiveModeTimer)
                        dragActive = false
                    }
                    onCanceled: {
                        if (interaction.rangeSelectionActive) interaction.finishMeasurement(chart)
                        contextHoldActive = false
                        if (root.chartSurface()) root.chartSurface().clearHover()
                        interaction.plotDragging = false
                        if (dragActive) interaction.stopInteractiveModeSoon(interactiveModeTimer)
                        dragActive = false
                    }
                    onExited: {
                        contextHoldActive = false
                        if (!interaction.rangeSelectionActive && root.chartSurface()) root.chartSurface().clearHover()
                    }
                    onWheel: function(wheel) {
                        interaction.startInteractiveMode(interactiveModeTimer)
                        if (root.chartSurface()) root.chartSurface().clearHover()
                        var factor = wheel.angleDelta.y > 0 ? 1.18 : 0.84
                        chart.zoomTime(factor)
                        chart.zoomPrice(factor)
                        interaction.stopInteractiveModeSoon(interactiveModeTimer)
                        wheel.accepted = true
                    }
                }
                ViewerSelectionOverlay { interaction: interaction; chart: chart; plotWidth: plotFrame.width; plotHeight: plotFrame.height; accentBuyColor: root.accentBuyColor; borderColor: root.borderColor; textColor: root.textColor }
            }
            ViewerPriceScale {
                id: priceScale
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.bottom: timeScale.top
                chart: chart
                tickCount: root.priceTickCount
                scaleColor: root.scaleColor
                borderColor: root.borderColor
                mutedTextColor: root.mutedTextColor
                MouseArea {
                    anchors.fill: parent
                    property real lastY: 0
                    property real pressY: 0
                    property bool dragActive: false
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    cursorShape: Qt.SizeVerCursor
                    preventStealing: true
                    onPressed: function(mouse) { interaction.priceScaleDragging = true; dragActive = false; pressY = mouse.y; lastY = mouse.y }
                    onPositionChanged: function(mouse) {
                        if (!(mouse.buttons & Qt.LeftButton) && !(mouse.buttons & Qt.RightButton)) return
                        if (!dragActive) {
                            if (Math.abs(mouse.y - pressY) < 3) return
                            dragActive = true
                            interaction.startInteractiveMode(interactiveModeTimer)
                            if (root.chartSurface()) root.chartSurface().clearHover()
                        }
                        var dy = mouse.y - lastY
                        lastY = mouse.y
                        chart.zoomPrice(Math.exp(-dy * 0.012))
                    }
                    onReleased: { interaction.priceScaleDragging = false; if (dragActive) interaction.stopInteractiveModeSoon(interactiveModeTimer); dragActive = false }
                    onCanceled: { interaction.priceScaleDragging = false; if (dragActive) interaction.stopInteractiveModeSoon(interactiveModeTimer); dragActive = false }
                }
            }
            ViewerTimeScale {
                id: timeScale
                anchors.left: parent.left
                anchors.right: priceScale.left
                anchors.bottom: parent.bottom
                chart: chart
                tickCount: root.timeTickCount
                scaleColor: root.scaleColor
                borderColor: root.borderColor
                mutedTextColor: root.mutedTextColor
                MouseArea {
                    anchors.fill: parent
                    property real lastX: 0
                    property real pressX: 0
                    property bool dragActive: false
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    cursorShape: Qt.SizeHorCursor
                    preventStealing: true
                    onPressed: function(mouse) { interaction.timeScaleDragging = true; dragActive = false; pressX = mouse.x; lastX = mouse.x }
                    onPositionChanged: function(mouse) {
                        if (!(mouse.buttons & Qt.LeftButton) && !(mouse.buttons & Qt.RightButton)) return
                        if (!dragActive) {
                            if (Math.abs(mouse.x - pressX) < 3) return
                            dragActive = true
                            interaction.startInteractiveMode(interactiveModeTimer)
                            if (root.chartSurface()) root.chartSurface().clearHover()
                        }
                        var dx = mouse.x - lastX
                        lastX = mouse.x
                        chart.zoomTime(Math.exp(dx * 0.012))
                    }
                    onReleased: { interaction.timeScaleDragging = false; if (dragActive) interaction.stopInteractiveModeSoon(interactiveModeTimer); dragActive = false }
                    onCanceled: { interaction.timeScaleDragging = false; if (dragActive) interaction.stopInteractiveModeSoon(interactiveModeTimer); dragActive = false }
                }
            }
            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.leftMargin: 16
                anchors.topMargin: 14
                radius: 8
                color: "#2b2b31"
                border.color: root.borderColor
                border.width: 1
                visible: root.showOrderbookLayer || root.showBookTickerLayer || !root.showTradesLayer
                implicitWidth: layerStatusText.implicitWidth + 20
                implicitHeight: layerStatusText.implicitHeight + 12
                Label {
                    id: layerStatusText
                    anchors.centerIn: parent
                    text: !root.showTradesLayer ? "Trades hidden" : root.showOrderbookLayer ? "Orderbook + BookTicker" : "BookTicker mode"
                    color: root.mutedTextColor
                    font.pixelSize: 12
                }
            }
        }
    }
}


