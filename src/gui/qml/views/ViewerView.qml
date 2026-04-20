import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HftRecorder 1.0

Pane {
    id: root
    padding: 0
    focus: true
    required property AppViewModel appVm

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
    property string selectedSessionId: ""
    property bool showTradesLayer: true
    property bool showOrderbookLayer: false
    property bool showBookTickerLayer: false
    property bool effectiveBookTickerLayer: showBookTickerLayer || showOrderbookLayer
    property bool useDedicatedGpuPath: root.appVm.requestedRenderMode === "gpu"
    // Legacy compatibility flag. The active viewer item is still `ChartItem`
    // (`QQuickPaintedItem`), so current GPU mode means hardware-backed Qt
    // Quick compositing, not a dedicated GPU-native chart renderer.
    property bool useGpuRenderer: true

    function chartSurface() {
        return chartLoader.item
    }

    function syncChannelView() {
        interaction.clearSelectionVisual()
        chart.clearSelection()
        if (selectedSessionId === "") {
            chart.resetSession()
            return
        }
        chart.loadSession("./recordings/" + selectedSessionId)
    }

    function ensureSessionSelection() {
        if (sessionToolbar.count() <= 0) {
            selectedSessionId = ""
            chart.resetSession()
            return
        }

        var desiredIndex = sessionToolbar.findSessionIndex(selectedSessionId)
        if (desiredIndex < 0)
            desiredIndex = Math.max(0, sessionToolbar.currentIndex())
        if (desiredIndex < 0)
            desiredIndex = 0

        sessionToolbar.setCurrentIndex(desiredIndex)
        var nextSessionId = sessionToolbar.textAt(desiredIndex)
        if (nextSessionId === "")
            nextSessionId = sessionToolbar.currentText()

        if (selectedSessionId !== nextSessionId) {
            selectedSessionId = nextSessionId
            syncChannelView()
        } else if (chart.loaded !== true) {
            syncChannelView()
        }
    }

    background: Rectangle { color: root.windowColor }

    SessionListModel {
        id: sessionsModel
        Component.onCompleted: reload()
    }

    ChartController {
        id: chart
    }

    ViewerInteractionState {
        id: interaction
    }

    Timer {
        id: interactiveModeTimer
        interval: 120
        repeat: false
        onTriggered: interaction.interactiveMode = false
    }

    Component.onCompleted: Qt.callLater(root.ensureSessionSelection)

    Connections {
        target: sessionsModel
        function onModelReset() {
            Qt.callLater(root.ensureSessionSelection)
        }
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
            sessionsModel: sessionsModel
            selectedSessionId: root.selectedSessionId
            chromeColor: root.chromeColor
            panelColor: root.panelColor
            panelAltColor: root.panelAltColor
            borderColor: root.borderColor
            textColor: root.textColor
            mutedTextColor: root.mutedTextColor
            onSessionActivated: function(sessionId) {
                root.selectedSessionId = sessionId
                root.syncChannelView()
            }
            onReloadRequested: {
                sessionsModel.reload()
                Qt.callLater(root.ensureSessionSelection)
            }
            onSessionCountChanged: Qt.callLater(root.ensureSessionSelection)
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
                if (root.showTradesLayer && !chart.loaded && root.selectedSessionId !== "")
                    root.syncChannelView()
            }
            onToggleOrderbook: root.showOrderbookLayer = !root.showOrderbookLayer
            onToggleBookTicker: root.showBookTickerLayer = !root.showBookTickerLayer
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Rectangle {
                anchors.fill: parent
                color: root.chartColor
            }

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
                        interactiveMode: interaction.interactiveMode
                    }
                }

                Loader {
                    id: chartLoader
                    anchors.fill: parent
                    sourceComponent: root.useDedicatedGpuPath ? gpuChartComponent : cpuChartComponent
                }

                MouseArea {
                    anchors.fill: parent
                    property real lastX: 0
                    property real lastY: 0
                    property real pressX: 0
                    property real pressY: 0
                    property bool dragActive: false
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    hoverEnabled: true
                    preventStealing: true

                    onPressed: function(mouse) {
                        if (mouse.button === Qt.RightButton) {
                            if (root.chartSurface())
                                root.chartSurface().activateContextPoint(mouse.x, mouse.y)
                            return
                        }
                        if ((mouse.modifiers & Qt.ShiftModifier) && mouse.button === Qt.LeftButton) {
                            interaction.startInteractiveMode(interactiveModeTimer)
                            interaction.beginSelection(mouse.x, mouse.y)
                            if (root.chartSurface())
                                root.chartSurface().clearHover()
                            return
                        }
                        interaction.plotDragging = true
                        dragActive = false
                        pressX = mouse.x
                        pressY = mouse.y
                        lastX = mouse.x
                        lastY = mouse.y
                        if (root.chartSurface() && !interaction.anyHoverableLayerVisible(root.showTradesLayer, root.effectiveBookTickerLayer, root.showOrderbookLayer))
                            root.chartSurface().clearHover()
                    }

                    onPositionChanged: function(mouse) {
                        if (interaction.rangeSelectionActive) {
                            interaction.updateSelection(mouse.x, mouse.y, plotFrame.width, plotFrame.height)
                            return
                        }
                        if (mouse.buttons & Qt.LeftButton) {
                            if (!dragActive) {
                                var distance = Math.abs(mouse.x - pressX) + Math.abs(mouse.y - pressY)
                                if (distance < 4)
                                    return
                                dragActive = true
                                interaction.startInteractiveMode(interactiveModeTimer)
                                if (root.chartSurface())
                                    root.chartSurface().clearHover()
                            }
                            var dx = mouse.x - lastX
                            var dy = mouse.y - lastY
                            lastX = mouse.x
                            lastY = mouse.y
                            chart.panTime(-dx / Math.max(1, width))
                            chart.panPrice(dy / Math.max(1, height))
                            return
                        }
                        if (interaction.priceScaleDragging || interaction.timeScaleDragging)
                            return
                        if (!interaction.anyHoverableLayerVisible(root.showTradesLayer, root.effectiveBookTickerLayer, root.showOrderbookLayer))
                            return
                        if (root.chartSurface())
                            root.chartSurface().setHoverPoint(mouse.x, mouse.y)
                    }

                    onReleased: {
                        if (interaction.rangeSelectionActive) {
                            interaction.commitSelection(chart, plotFrame.width, plotFrame.height)
                            interaction.stopInteractiveModeSoon(interactiveModeTimer)
                            return
                        }
                        interaction.plotDragging = false
                        if (dragActive)
                            interaction.stopInteractiveModeSoon(interactiveModeTimer)
                        dragActive = false
                    }

                    onCanceled: {
                        if (interaction.rangeSelectionActive)
                            interaction.clearSelectionVisual()
                        interaction.plotDragging = false
                        if (dragActive)
                            interaction.stopInteractiveModeSoon(interactiveModeTimer)
                        dragActive = false
                    }

                    onExited: {
                        if (!interaction.rangeSelectionActive && root.chartSurface())
                            root.chartSurface().clearHover()
                    }

                    onWheel: function(wheel) {
                        interaction.startInteractiveMode(interactiveModeTimer)
                        if (root.chartSurface())
                            root.chartSurface().clearHover()
                        var factor = wheel.angleDelta.y > 0 ? 1.18 : 0.84
                        chart.zoomTime(factor)
                        chart.zoomPrice(factor)
                        interaction.stopInteractiveModeSoon(interactiveModeTimer)
                        wheel.accepted = true
                    }
                }

                ViewerSelectionOverlay {
                    interaction: interaction
                    chart: chart
                    plotWidth: plotFrame.width
                    plotHeight: plotFrame.height
                    accentBuyColor: root.accentBuyColor
                    borderColor: root.borderColor
                    textColor: root.textColor
                }
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

                    onPressed: function(mouse) {
                        interaction.priceScaleDragging = true
                        dragActive = false
                        pressY = mouse.y
                        lastY = mouse.y
                    }

                    onPositionChanged: function(mouse) {
                        if (!(mouse.buttons & Qt.LeftButton) && !(mouse.buttons & Qt.RightButton))
                            return
                        if (!dragActive) {
                            if (Math.abs(mouse.y - pressY) < 3)
                                return
                            dragActive = true
                            interaction.startInteractiveMode(interactiveModeTimer)
                            if (root.chartSurface())
                                root.chartSurface().clearHover()
                        }
                        var dy = mouse.y - lastY
                        lastY = mouse.y
                        chart.zoomPrice(Math.exp(-dy * 0.012))
                    }

                    onReleased: {
                        interaction.priceScaleDragging = false
                        if (dragActive)
                            interaction.stopInteractiveModeSoon(interactiveModeTimer)
                        dragActive = false
                    }

                    onCanceled: {
                        interaction.priceScaleDragging = false
                        if (dragActive)
                            interaction.stopInteractiveModeSoon(interactiveModeTimer)
                        dragActive = false
                    }
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

                    onPressed: function(mouse) {
                        interaction.timeScaleDragging = true
                        dragActive = false
                        pressX = mouse.x
                        lastX = mouse.x
                    }

                    onPositionChanged: function(mouse) {
                        if (!(mouse.buttons & Qt.LeftButton) && !(mouse.buttons & Qt.RightButton))
                            return
                        if (!dragActive) {
                            if (Math.abs(mouse.x - pressX) < 3)
                                return
                            dragActive = true
                            interaction.startInteractiveMode(interactiveModeTimer)
                            if (root.chartSurface())
                                root.chartSurface().clearHover()
                        }
                        var dx = mouse.x - lastX
                        lastX = mouse.x
                        chart.zoomTime(Math.exp(dx * 0.012))
                    }

                    onReleased: {
                        interaction.timeScaleDragging = false
                        if (dragActive)
                            interaction.stopInteractiveModeSoon(interactiveModeTimer)
                        dragActive = false
                    }

                    onCanceled: {
                        interaction.timeScaleDragging = false
                        if (dragActive)
                            interaction.stopInteractiveModeSoon(interactiveModeTimer)
                        dragActive = false
                    }
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
                    text: !root.showTradesLayer
                          ? "Trades hidden"
                          : root.showOrderbookLayer
                            ? "Orderbook + BookTicker"
                            : "BookTicker mode"
                    color: root.mutedTextColor
                    font.pixelSize: 12
                }
            }
        }
    }
}
