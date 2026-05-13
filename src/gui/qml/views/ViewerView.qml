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
    required property bool tabActive

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
    property string selectedCompareSourceA: ""
    property string selectedCompareSourceB: ""
    property int selectedCompareIndexA: -1
    property int selectedCompareIndexB: -1
    property var compareSourceRows: [{ id: "", label: "Select session" }]
    property bool userHasExplicitSelection: false
    property bool userHasExplicitCompareSelection: false
    property bool compareMode: selectedCompareSourceA !== "" && selectedCompareSourceB !== "" && selectedCompareSourceA !== selectedCompareSourceB
    property bool comparePickerActive: userHasExplicitCompareSelection || compareMode
    property bool showTradesLayer: true
    property bool showLiquidationsLayer: false
    property bool showOrderbookLayer: false
    property bool showBookTickerLayer: false
    property bool effectiveBookTickerLayer: showBookTickerLayer
    property bool userHasExplicitLayerSelection: false
    property bool useDedicatedGpuPath: false
    property bool useGpuRenderer: true

    function chartSurface() { return chartLoader.item }
    function syncRendererDiagnostics() { root.appVm.activeChartRenderer = root.useDedicatedGpuPath ? "gpu-orderbook" : "cpu-chart" }
    function syncLiveUpdateMode() {
        chart.setLiveUpdateIntervalMs(root.appVm.liveUpdateIntervalMs)
        compareChart.setLiveUpdateIntervalMs(root.appVm.liveUpdateIntervalMs)
    }
    function syncRenderWindow() { chart.setRenderWindowSeconds(root.appVm.renderWindowSeconds) }

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
            if (sourcesModel.groupAt(i) === "live")
                return i
        }
        return -1
    }

    function liveCompareSourceIds() {
        var ready = []
        var pending = []
        for (var i = 0; i < sourcesModel.rowCount(); ++i) {
            if (sourcesModel.groupAt(i) !== "live")
                continue
            var id = sourcesModel.sourceIdAt(i)
            if (id === "")
                continue
            if (sourcesModel.bookTickerCount(id) > 0)
                ready.push(id)
            else
                pending.push(id)
        }
        return ready.length >= 2 ? ready : []
    }

    function autoSelectLiveCompareSources(force) {
        if (!force && (root.selectedCompareSourceA !== "" || root.selectedCompareSourceB !== ""))
            return
        var ids = root.liveCompareSourceIds()
        if (ids.length < 2) {
            if (force && !root.userHasExplicitCompareSelection) {
                root.selectedCompareSourceA = ""
                root.selectedCompareSourceB = ""
                root.syncCompareIndexesFromIds()
                root.applyCompareSelection()
            }
            return
        }
        if (root.selectedCompareSourceA === ids[0] && root.selectedCompareSourceB === ids[1])
            return
        root.selectedCompareSourceA = ids[0]
        root.selectedCompareSourceB = ids[1]
        root.syncCompareIndexesFromIds()
        root.applyCompareSelection()
    }

    function ensureVisibleLayerSelection() {
        if (root.userHasExplicitLayerSelection)
            return

        if (chart.hasTrades || chart.hasLiquidations) {
            if (!root.showTradesLayer && !root.showLiquidationsLayer && !root.showOrderbookLayer && !root.showBookTickerLayer) {
                if (chart.hasTrades)
                    root.showTradesLayer = true
                else
                    root.showLiquidationsLayer = true
            }
            return
        }

        if (root.showTradesLayer || root.showLiquidationsLayer) {
            root.showTradesLayer = false
            root.showLiquidationsLayer = false
            if (chart.hasOrderbook)
                root.showOrderbookLayer = true
            else if (chart.hasBookTicker)
                root.showBookTickerLayer = true
        }

        if (!root.showTradesLayer && !root.showLiquidationsLayer && !root.showOrderbookLayer && !root.showBookTickerLayer) {
            if (chart.hasOrderbook)
                root.showOrderbookLayer = true
            else if (chart.hasBookTicker)
                root.showBookTickerLayer = true
            else if (chart.hasLiquidations)
                root.showLiquidationsLayer = true
            else
                root.showTradesLayer = true
        }
    }

    function applyCompareSelection() {
        compareChart.setPrimarySource(root.selectedCompareSourceA,
                                      sourcesModel.sourceKind(root.selectedCompareSourceA),
                                      sourcesModel.sessionPath(root.selectedCompareSourceA))
        compareChart.setSecondarySource(root.selectedCompareSourceB,
                                        sourcesModel.sourceKind(root.selectedCompareSourceB),
                                        sourcesModel.sessionPath(root.selectedCompareSourceB))
        if (root.selectedCompareSourceA !== "")
            root.selectedSourceId = root.selectedCompareSourceA
    }

    function syncCompareIndexesFromIds() {
        root.selectedCompareIndexA = root.indexInCompareRows(root.selectedCompareSourceA)
        root.selectedCompareIndexB = root.indexInCompareRows(root.selectedCompareSourceB)
    }

    function rebuildCompareSourceRows() {
        var rows = [{ id: "", label: "Select session" }]
        for (var i = 0; i < sourcesModel.rowCount(); ++i) {
            var id = sourcesModel.sourceIdAt(i)
            if (id !== "") {
                var label = sourcesModel.labelAt(i)
                if (sourcesModel.groupAt(i) === "live")
                    label += " | L1 " + sourcesModel.bookTickerCount(id)
                rows.push({ id: id, label: label })
            }
        }
        root.compareSourceRows = rows
        root.syncCompareIndexesFromIds()
    }

    function indexInCompareRows(sourceId) {
        if (sourceId === "") return 0
        for (var i = 1; i < root.compareSourceRows.length; ++i) {
            if (root.compareSourceRows[i].id === sourceId) return i
        }
        return 0
    }

    function ensureSourceSelection() {
        if (sessionToolbar.count() <= 0) {
            if (!root.userHasExplicitSelection) {
                root.selectedSourceId = ""
                chart.resetSession()
            }
            return
        }

        if (root.selectedSourceId !== "" && sourcesModel.hasSource(root.selectedSourceId)) {
            sessionToolbar.setCurrentIndex(sourcesModel.indexOfSource(root.selectedSourceId))
            if (!root.userHasExplicitSelection && chart.currentSourceId !== root.selectedSourceId)
                root.applySourceSelection(root.selectedSourceId)
            return
        }

        sessionToolbar.setCurrentIndex(-1)
    }

    function ensureCompareSelection() {
        if (root.selectedCompareSourceA !== "" && !sourcesModel.hasSource(root.selectedCompareSourceA))
            root.selectedCompareSourceA = ""
        if (root.selectedCompareSourceB !== "" && !sourcesModel.hasSource(root.selectedCompareSourceB))
            root.selectedCompareSourceB = ""
        if (root.selectedCompareSourceB === root.selectedCompareSourceA)
            root.selectedCompareSourceB = ""
        if (!root.userHasExplicitCompareSelection) {
            root.autoSelectLiveCompareSources(true)
            return
        }
        root.syncCompareIndexesFromIds()
        root.applyCompareSelection()
    }

    component DarkSourceCombo: ComboBox {
        id: combo
        Layout.preferredWidth: 420
        model: root.compareSourceRows
        textRole: "label"
        valueRole: "id"
        contentItem: Text {
            text: combo.currentIndex <= 0 ? "Select session" : combo.displayText
            color: combo.currentIndex <= 0 ? root.mutedTextColor : root.textColor
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            leftPadding: 10
            rightPadding: 28
        }
        background: Rectangle {
            radius: 7
            color: combo.down ? root.panelAltColor : root.panelColor
            border.color: combo.activeFocus ? root.accentBuyColor : root.borderColor
            border.width: 1
        }
        delegate: Component {
            ItemDelegate {
                width: combo.width
                text: modelData.label
                highlighted: combo.highlightedIndex === index
                contentItem: Text {
                    text: modelData.label
                    color: index === 0 ? root.mutedTextColor : root.textColor
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle {
                    color: highlighted ? root.panelAltColor : root.panelColor
                }
            }
        }
        popup: Popup {
            y: combo.height + 2
            width: combo.width
            implicitHeight: Math.min(contentItem.implicitHeight, 360)
            padding: 1
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: combo.popup.visible ? combo.delegateModel : null
                currentIndex: combo.highlightedIndex
            }
            background: Rectangle {
                color: root.panelColor
                border.color: root.borderColor
                radius: 7
            }
        }
        Component.onCompleted: currentIndex = 0
    }

    background: Rectangle { color: root.windowColor }

    ViewerSourceListModel {
        id: sourcesModel
        captureViewModel: root.captureVm
        Component.onCompleted: reload()
    }

    ChartController { id: chart; objectName: "chartController" }
    BookTickerCompareController { id: compareChart }
    ViewerInteractionState { id: interaction }
    Timer { id: interactiveModeTimer; interval: 120; repeat: false; onTriggered: interaction.interactiveMode = false }
    Timer {
        id: hoverUpdateTimer
        interval: 33
        repeat: false
        onTriggered: {
            if (root.chartSurface() && hoverMouseArea.hoverPending)
                root.chartSurface().setHoverPoint(hoverMouseArea.pendingHoverX, hoverMouseArea.pendingHoverY)
            hoverMouseArea.hoverPending = false
        }
    }

    Component.onCompleted: {
        chart.active = root.tabActive
        root.rebuildCompareSourceRows()
        Qt.callLater(root.autoSelectLiveCompareSources)
        Qt.callLater(root.syncRendererDiagnostics)
        Qt.callLater(root.syncLiveUpdateMode)
        Qt.callLater(root.syncRenderWindow)
    }
    onTabActiveChanged: chart.active = root.tabActive
    onUseDedicatedGpuPathChanged: root.syncRendererDiagnostics()

    onSelectedCompareIndexAChanged: if (compareComboA) compareComboA.currentIndex = root.selectedCompareIndexA
    onSelectedCompareIndexBChanged: if (compareComboB) compareComboB.currentIndex = root.selectedCompareIndexB

    Connections {
        target: root.appVm
        function onLiveUpdateModeChanged() { root.syncLiveUpdateMode() }
        function onRenderWindowSecondsChanged() { root.syncRenderWindow() }
    }

    Connections {
        target: sourcesModel
        function onModelReset() {
            root.rebuildCompareSourceRows()
            Qt.callLater(root.ensureCompareSelection)
            Qt.callLater(root.autoSelectLiveCompareSources)
        }
        function onRowsInserted() {
            root.rebuildCompareSourceRows()
            Qt.callLater(root.ensureCompareSelection)
            Qt.callLater(root.autoSelectLiveCompareSources)
        }
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
            visible: false
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

        Rectangle {
            Layout.fillWidth: true
            color: root.chromeColor
            implicitHeight: compareControls.implicitHeight + 12
            RowLayout {
                id: compareControls
                anchors.fill: parent
                anchors.margins: 6
                spacing: 8

                Label { text: "Source A"; color: root.mutedTextColor }
                DarkSourceCombo {
                    id: compareComboA
                    currentIndex: root.selectedCompareIndexA
                    onActivated: function(index) {
                        root.userHasExplicitCompareSelection = true
                        root.selectedCompareSourceA = index <= 0 ? "" : root.compareSourceRows[index].id
                        if (root.selectedCompareSourceB === root.selectedCompareSourceA)
                            root.selectedCompareSourceB = ""
                        root.syncCompareIndexesFromIds()
                        root.applyCompareSelection()
                    }
                }

                Label { text: "Source B"; color: root.mutedTextColor }
                DarkSourceCombo {
                    id: compareComboB
                    currentIndex: root.selectedCompareIndexB
                    onActivated: function(index) {
                        root.userHasExplicitCompareSelection = true
                        root.selectedCompareSourceB = index <= 0 ? "" : root.compareSourceRows[index].id
                        if (root.selectedCompareSourceB === root.selectedCompareSourceA)
                            root.selectedCompareSourceB = ""
                        root.syncCompareIndexesFromIds()
                        root.applyCompareSelection()
                    }
                }

                Label {
                    Layout.fillWidth: true
                    text: root.compareMode
                          ? compareChart.statusText + " | A: " + compareChart.primaryCount + " B: " + compareChart.secondaryCount + " spread: " + compareChart.spreadCount
                          : (root.comparePickerActive ? "Select source A and source B for compare" : "Pick two different sessions to show bookTicker overlay and arbitrage bps")
                    color: root.mutedTextColor
                    elide: Text.ElideRight
                }

                Button {
                    id: reloadButton
                    text: "Reload"
                    contentItem: Text {
                        text: reloadButton.text
                        color: root.textColor
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        radius: 7
                        color: reloadButton.down ? root.panelAltColor : root.panelColor
                        border.color: root.borderColor
                        border.width: 1
                    }
                    onClicked: {
                        sourcesModel.reload()
                        Qt.callLater(root.rebuildCompareSourceRows)
                        Qt.callLater(root.ensureCompareSelection)
                    }
                }
            }
        }

        ViewerLayerToolbar {
            visible: !root.comparePickerActive
            appVm: root.appVm
            chart: chart
            interaction: interaction
            showTradesLayer: root.showTradesLayer
            showLiquidationsLayer: root.showLiquidationsLayer
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
                root.userHasExplicitLayerSelection = true
                root.showTradesLayer = !root.showTradesLayer
                if (root.showTradesLayer && !chart.loaded && root.selectedSourceId !== "") {
                    root.userHasExplicitLayerSelection = false
                    root.applySourceSelection(root.selectedSourceId)
                    root.userHasExplicitLayerSelection = true
                }
            }
            onToggleLiquidations: {
                root.userHasExplicitLayerSelection = true
                root.showLiquidationsLayer = !root.showLiquidationsLayer
            }
            onToggleOrderbook: {
                root.userHasExplicitLayerSelection = true
                root.showOrderbookLayer = !root.showOrderbookLayer
            }
            onToggleBookTicker: {
                root.userHasExplicitLayerSelection = true
                root.showBookTickerLayer = !root.showBookTickerLayer
            }
        }

        Rectangle {
            Layout.fillWidth: true
            color: root.chromeColor
            implicitHeight: 28
            Label {
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 8
                text: root.comparePickerActive ? "Top: two bookTicker traces. Bottom: best buy-ask/sell-bid spread in bps, net of A/B internal spreads, no fees." : "Single-source preview until two different sessions are selected."
                color: root.mutedTextColor
                font.pixelSize: 12
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Rectangle { anchors.fill: parent; color: root.chartColor }
            BookTickerCompareItem {
                id: compareSurface
                anchors.fill: parent
                visible: root.comparePickerActive
                controller: compareChart
            }
            MouseArea {
                anchors.fill: parent
                visible: root.comparePickerActive
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                cursorShape: Qt.ArrowCursor
                property real lastX: 0
                property real pressX: 0
                property bool dragActive: false
                property bool measureActive: false
                property bool measureStarted: false
                property real pressY: 0
                onPressed: function(mouse) {
                    lastX = mouse.x
                    pressX = mouse.x
                    pressY = mouse.y
                    dragActive = false
                    measureStarted = false
                    measureActive = (mouse.button === Qt.MiddleButton) || ((mouse.button === Qt.LeftButton) && (mouse.modifiers & Qt.ShiftModifier))
                    if (mouse.button === Qt.RightButton) {
                        compareSurface.clearMeasure()
                        mouse.accepted = true
                        return
                    }
                }
                onPositionChanged: function(mouse) {
                    compareSurface.setHoverPoint(mouse.x, mouse.y)
                    if (measureActive) {
                        if (!measureStarted) {
                            if (Math.abs(mouse.x - pressX) < 3 && Math.abs(mouse.y - pressY) < 3)
                                return
                            compareSurface.beginMeasure(pressX, pressY)
                            measureStarted = true
                        }
                        compareSurface.updateMeasure(mouse.x, mouse.y)
                        return
                    }
                    if (!(mouse.buttons & Qt.LeftButton) && !(mouse.buttons & Qt.RightButton)) return
                    if (!dragActive && Math.abs(mouse.x - pressX) < 3) return
                    dragActive = true
                    var dx = mouse.x - lastX
                    lastX = mouse.x
                    compareChart.panTime(-dx / Math.max(1, width))
                }
                onReleased: function(mouse) {
                    if (measureActive && measureStarted)
                        compareSurface.endMeasure()
                    measureStarted = false
                    measureActive = false
                    dragActive = false
                }
                onCanceled: {
                    if (measureActive && measureStarted)
                        compareSurface.endMeasure()
                    measureStarted = false
                    measureActive = false
                    dragActive = false
                }
                onExited: compareSurface.clearHover()
                onWheel: function(wheel) {
                    var factor = wheel.angleDelta.y > 0 ? 1.22 : 0.82
                    compareChart.zoomTimeAt(factor, Math.max(0, Math.min(1, wheel.x / Math.max(1, width))))
                    compareSurface.setHoverPoint(wheel.x, wheel.y)
                    wheel.accepted = true
                }
                onDoubleClicked: compareChart.autoFit()
            }
            Item {
                id: plotFrame
                visible: !root.comparePickerActive
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
                        liquidationsVisible: root.showLiquidationsLayer
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
                        liquidationsVisible: root.showLiquidationsLayer
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
                    id: hoverMouseArea
                    anchors.fill: parent
                    property real lastX: 0
                    property real lastY: 0
                    property real pressX: 0
                    property real pressY: 0
                    property bool dragActive: false
                    property bool contextHoldActive: false
                    property real pendingHoverX: 0
                    property real pendingHoverY: 0
                    property bool hoverPending: false
                    acceptedButtons: Qt.LeftButton | Qt.RightButton | Qt.MiddleButton
                    hoverEnabled: true
                    preventStealing: true
                    onPressed: function(mouse) {
                        if (mouse.button === Qt.RightButton) {
                            hoverUpdateTimer.stop()
                            hoverPending = false
                            contextHoldActive = true
                            if (root.chartSurface()) root.chartSurface().activateContextPoint(mouse.x, mouse.y)
                            return
                        }
                        if (mouse.button === Qt.MiddleButton) {
                            hoverUpdateTimer.stop()
                            hoverPending = false
                            interaction.startInteractiveMode(interactiveModeTimer)
                            interaction.beginSelection(mouse.x, mouse.y, 'middle_line')
                            if (root.chartSurface()) root.chartSurface().clearHover()
                            return
                        }
                        if ((mouse.modifiers & Qt.ShiftModifier) && mouse.button === Qt.LeftButton) {
                            hoverUpdateTimer.stop()
                            hoverPending = false
                            interaction.startInteractiveMode(interactiveModeTimer)
                            interaction.beginSelection(mouse.x, mouse.y, 'shift_box')
                            if (root.chartSurface()) root.chartSurface().clearHover()
                            return
                        }
                        if ((mouse.modifiers & Qt.ControlModifier) && mouse.button === Qt.LeftButton) {
                            hoverUpdateTimer.stop()
                            hoverPending = false
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
                        if (root.chartSurface() && !interaction.anyHoverableLayerVisible(root.showTradesLayer || root.showLiquidationsLayer, root.effectiveBookTickerLayer, root.showOrderbookLayer)) root.chartSurface().clearHover()
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
                                hoverUpdateTimer.stop()
                                hoverPending = false
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
                        if (!interaction.anyHoverableLayerVisible(root.showTradesLayer || root.showLiquidationsLayer, root.effectiveBookTickerLayer, root.showOrderbookLayer)) return
                        pendingHoverX = mouse.x
                        pendingHoverY = mouse.y
                        hoverPending = true
                        hoverUpdateTimer.restart()
                    }
                    onReleased: {
                        if (interaction.rangeSelectionActive) {
                            interaction.finishMeasurement(chart)
                            interaction.stopInteractiveModeSoon(interactiveModeTimer)
                            return
                        }
                        if (contextHoldActive) {
                            contextHoldActive = false
                            hoverUpdateTimer.stop()
                            hoverPending = false
                            if (root.chartSurface()) root.chartSurface().clearHover()
                        }
                        interaction.plotDragging = false
                        if (dragActive) interaction.stopInteractiveModeSoon(interactiveModeTimer)
                        dragActive = false
                    }
                    onCanceled: {
                        if (interaction.rangeSelectionActive) interaction.finishMeasurement(chart)
                        contextHoldActive = false
                        hoverUpdateTimer.stop()
                        hoverPending = false
                        if (root.chartSurface()) root.chartSurface().clearHover()
                        interaction.plotDragging = false
                        if (dragActive) interaction.stopInteractiveModeSoon(interactiveModeTimer)
                        dragActive = false
                    }
                    onExited: {
                        contextHoldActive = false
                        hoverUpdateTimer.stop()
                        hoverPending = false
                        if (!interaction.rangeSelectionActive && root.chartSurface()) root.chartSurface().clearHover()
                    }
                    onWheel: function(wheel) {
                        interaction.startInteractiveMode(interactiveModeTimer)
                        hoverUpdateTimer.stop()
                        hoverPending = false
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
                visible: !root.comparePickerActive
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
                visible: !root.comparePickerActive
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
                visible: !root.comparePickerActive && (root.showOrderbookLayer || root.showBookTickerLayer || root.showLiquidationsLayer || !root.showTradesLayer)
                implicitWidth: layerStatusText.implicitWidth + 20
                implicitHeight: layerStatusText.implicitHeight + 12
                Label {
                    id: layerStatusText
                    anchors.centerIn: parent
                    text: root.showLiquidationsLayer ? "Liquidations" : !root.showTradesLayer ? "Trades hidden" : root.showOrderbookLayer ? "Orderbook" : "BookTicker mode"
                    color: root.mutedTextColor
                    font.pixelSize: 12
                }
            }
        }
    }
}













