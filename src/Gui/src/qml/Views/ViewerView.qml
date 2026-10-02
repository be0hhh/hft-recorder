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
    required property var backtestVm
    required property var recordingCatalog
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
    property color panelDeepColor: "#161616"
    property color scaleColor: "#26262b"
    property int priceTickCount: 6
    property int timeTickCount: 5
    property string selectedSourceId: ""
    property string selectedCompareSourceA: ""
    property string selectedCompareSourceB: ""
    property int selectedCompareIndexA: -1
    property int selectedCompareIndexB: -1
    property var compareSourceRows: [{ id: "", label: "Select session" }]
    property var backtestRows: [{ path: "", sessionPath: "", label: "Backtest" }]
    property bool userHasExplicitSelection: false
    property bool userHasExplicitCompareSelection: false
    property bool compareMode: selectedCompareSourceA !== "" && selectedCompareSourceB !== "" && selectedCompareSourceA !== selectedCompareSourceB
    property bool comparePickerActive: compareMode
    property bool showTradesLayer: false
    property bool showCandlesLayer: false
    property bool showCandles2Layer: false
    property bool showOrderbookLayer: false
    property bool showBookTickerLayer: true
    property bool showRateLimitLayer: false
    property bool effectiveBookTickerLayer: showBookTickerLayer
    property bool userHasExplicitLayerSelection: false
    property bool userDisabledTradesLayer: false
    property bool userDisabledCandlesLayer: false
    property bool userDisabledCandles2Layer: false
    property bool userDisabledOrderbookLayer: false
    property bool userDisabledBookTickerLayer: false
    property bool useDedicatedGpuPath: false
    property bool useGpuRenderer: true

    function chartSurface() { return chartLoader.item }
    function syncRendererDiagnostics() { root.appVm.activeChartRenderer = root.useDedicatedGpuPath ? "gpu-orderbook" : "cpu-chart" }
    function syncLiveUpdateMode() {
        chart.setLiveUpdateIntervalMs(root.appVm.liveUpdateIntervalMs)
        compareChart.setLiveUpdateIntervalMs(root.appVm.liveUpdateIntervalMs)
    }
    function syncRenderWindow() { chart.setRenderWindowSeconds(root.appVm.renderWindowSeconds) }

    function syncBacktestRows() {
        var rows = [{ path: "", sessionPath: "", label: "Backtest" }]
        var seen = {}
        var primarySourceId = root.compareMode ? root.effectiveCompareSourceA() : root.singleSelectedSourceId()
        if (primarySourceId === "")
            primarySourceId = root.effectiveCompareSourceA()
        var secondarySourceId = root.compareMode ? root.effectiveCompareSourceB() : ""
        var resultRows = sourcesModel.backtestResultRows(primarySourceId, secondarySourceId)
        for (var i = 0; i < resultRows.length; ++i)
            root.appendBacktestRow(rows, seen, resultRows[i])
        root.backtestRows = rows
        root.syncBacktestComboIndex()
        if (backtestCombo)
            backtestCombo.rebuildFilter()
    }

    function appendBacktestRow(rows, seen, row) {
        if (!row || !row.path || row.path === "" || seen[row.path])
            return
        seen[row.path] = true
        rows.push(row)
    }

    function compareComboSourceId(combo) {
        if (!combo)
            return ""
        var index = combo.currentIndex
        if (index <= 0 || index >= root.compareSourceRows.length)
            return ""
        var row = root.compareSourceRows[index]
        if (!row || row.selectable === false || row.isGroup === true)
            return ""
        return row && row.id ? row.id : ""
    }

    function effectiveCompareSourceA() {
        return root.selectedCompareSourceA !== "" ? root.selectedCompareSourceA : root.compareComboSourceId(compareComboA)
    }

    function effectiveCompareSourceB() {
        return root.selectedCompareSourceB !== "" ? root.selectedCompareSourceB : root.compareComboSourceId(compareComboB)
    }

    function compareSourceGroupId(sourceId) {
        var target = String(sourceId || "").trim()
        if (target.length === 0)
            return ""
        for (var i = 0; i < root.compareSourceRows.length; ++i) {
            var row = root.compareSourceRows[i]
            if (!row || String(row.id || "") !== target)
                continue
            if (row.parentGroupId)
                return String(row.parentGroupId)
            if (row.groupId)
                return String(row.groupId)
            return ""
        }
        return ""
    }

    function backtestPrimarySessionPath() {
        if (root.compareMode)
            return sourcesModel.sessionPath(root.effectiveCompareSourceA())
        var sourceId = root.singleSelectedSourceId()
        if (sourceId === "")
            sourceId = root.effectiveCompareSourceA()
        if (sourceId !== "" && sourcesModel.sourceKind(sourceId) === "recorded")
            return sourcesModel.sessionPath(sourceId)
        if (chart.currentSourceKind === "recorded" && chart.sessionDir !== "")
            return chart.sessionDir
        return ""
    }

    function backtestSecondarySessionPath() {
        return root.compareMode ? sourcesModel.sessionPath(root.effectiveCompareSourceB()) : ""
    }

    function selectedBacktestCountHint() {
        var sourceId = root.compareMode ? root.effectiveCompareSourceA() : root.singleSelectedSourceId()
        if (sourceId === "")
            sourceId = root.effectiveCompareSourceA()
        return sourceId === "" ? 0 : sourcesModel.backtestCount(sourceId)
    }

    function refreshBacktestChoices() {
        var primarySessionPath = root.backtestPrimarySessionPath()
        var secondarySessionPath = root.backtestSecondarySessionPath()
        if (primarySessionPath === "") {
            chart.refreshBacktestResults("", "")
            root.syncBacktestRows()
            compareChart.setBacktestResult("")
            return
        }
        if (!root.compareMode && primarySessionPath !== "" && root.backtestVm.sessionPath !== primarySessionPath)
            root.backtestVm.setSessionPath(primarySessionPath)
        chart.refreshBacktestResults(primarySessionPath, secondarySessionPath)
        root.syncBacktestRows()
        compareChart.setBacktestResult(chart.selectedBacktestResult)
    }

    function syncBacktestComboIndex() {
        if (!backtestCombo)
            return
        var selected = chart.selectedBacktestResult
        var nextIndex = 0
        for (var i = 1; i < root.backtestRows.length; ++i) {
            if (root.backtestRows[i].path === selected) {
                nextIndex = i
                break
            }
        }
        backtestCombo.currentIndex = nextIndex
    }

    function chooseBacktestRow(index) {
        if (index <= 0 || index >= root.backtestRows.length) {
            chart.clearBacktestResult()
            compareChart.setBacktestResult("")
            root.syncBacktestComboIndex()
            return
        }
        var row = root.backtestRows[index]
        if (row.sessionPath !== "" && chart.sessionDir !== row.sessionPath)
            chart.loadRecordedSession(row.sessionPath)
        if (row.selectable === false) {
            root.syncBacktestComboIndex()
            return
        }
        backtestCombo.currentIndex = index
        if (chart.selectBacktestResult(row.path)) {
            compareChart.setBacktestResult(row.path)
        } else {
            compareChart.setBacktestResult("")
            root.syncBacktestComboIndex()
        }
    }

    function loadRecordedSource(sourceId) {
        chart.loadRecordedSession(sourcesModel.sessionPath(sourceId))
    }

    function loadSelectedRecordedOrderbook() {
        if (chart.currentSourceKind !== "recorded" || chart.sessionDir === "")
            return false
        return chart.loadRecordedOrderbook()
    }

    function preferChartStatusText() {
        var text = chart.statusText
        if (text === "")
            return false
        if (root.showOrderbookLayer && !chart.hasOrderbook)
            return true
        return text.indexOf("failed") !== -1
            || text.indexOf("Failed") !== -1
            || text.indexOf("No orderbook") !== -1
            || text.indexOf("Orderbook load") !== -1
    }

    function applySourceSelection(sourceId) {
        if (sourceId !== "" && chart.currentSourceId === sourceId && chart.loaded)
            return
        interaction.clearSelectionVisual()
        chart.clearSelection()
        var sourceKind = sourcesModel.sourceKind(sourceId)
        if (sourceKind === "live") {
            chart.activateLiveSource(sourceId, sourcesModel.sessionPath(sourceId))
            return
        }
        if (sourceKind === "recorded") {
            root.loadRecordedSource(sourceId)
            Qt.callLater(root.refreshBacktestChoices)
            return
        }
        chart.resetSession()
        Qt.callLater(root.refreshBacktestChoices)
    }

    function singleSelectedSourceId() {
        if (root.compareMode)
            return ""
        if (root.selectedCompareSourceA !== "")
            return root.selectedCompareSourceA
        if (root.selectedCompareSourceB !== "")
            return root.selectedCompareSourceB
        if (root.selectedSourceId !== "")
            return root.selectedSourceId
        return ""
    }

    function loadSingleSelectedSource() {
        var sourceId = root.singleSelectedSourceId()
        var sourceChanged = root.selectedSourceId !== sourceId
        if (sourceChanged) {
            root.userHasExplicitLayerSelection = false
            root.userDisabledTradesLayer = false
            root.userDisabledCandlesLayer = false
            root.userDisabledCandles2Layer = false
            root.userDisabledOrderbookLayer = false
            root.userDisabledBookTickerLayer = false
            root.showTradesLayer = false
            root.showCandlesLayer = false
            root.showCandles2Layer = false
            root.showOrderbookLayer = false
            root.showBookTickerLayer = true
        }
        root.userHasExplicitSelection = sourceId !== ""
        root.selectedSourceId = sourceId
        if (sourceId === "") {
            chart.resetSession()
            return
        }
        root.applySourceSelection(sourceId)
    }

    function liveSourceIndex() {
        for (var i = 0; i < sourcesModel.rowCount(); ++i) {
            if (sourcesModel.groupAt(i) === "live")
                return i
        }
        return -1
    }


    function ensureVisibleLayerSelection() {
        if (root.userHasExplicitLayerSelection)
            return

        if (!root.compareMode && root.selectedSourceId !== "") {
            if (chart.hasBookTicker && !root.userDisabledBookTickerLayer) {
                root.showTradesLayer = false
                root.showCandlesLayer = false
                root.showCandles2Layer = false
                root.showOrderbookLayer = false
                root.showBookTickerLayer = true
                return
            }
            root.showTradesLayer = chart.hasTrades && !root.userDisabledTradesLayer
            root.showCandles2Layer = chart.hasCandles2 && !root.userDisabledCandles2Layer
            root.showCandlesLayer = !root.showCandles2Layer && chart.hasCandles && !root.userDisabledCandlesLayer
            root.showOrderbookLayer = chart.hasOrderbook && !root.userDisabledOrderbookLayer
            root.showBookTickerLayer = false
            if (!root.showTradesLayer && !root.showCandlesLayer && !root.showCandles2Layer && !root.showOrderbookLayer && !root.showBookTickerLayer)
                root.showTradesLayer = !root.userDisabledTradesLayer
            return
        }

        if (root.showTradesLayer || root.showCandlesLayer || root.showCandles2Layer) {
            root.showTradesLayer = false
            root.showCandlesLayer = false
            root.showCandles2Layer = false
            if (chart.hasOrderbook && !root.userDisabledOrderbookLayer)
                root.showOrderbookLayer = true
            else if (chart.hasBookTicker && !root.userDisabledBookTickerLayer)
                root.showBookTickerLayer = true
        }

        if (!root.showTradesLayer && !root.showCandlesLayer && !root.showCandles2Layer && !root.showOrderbookLayer && !root.showBookTickerLayer) {
            if (chart.hasOrderbook && !root.userDisabledOrderbookLayer)
                root.showOrderbookLayer = true
            else if (chart.hasBookTicker && !root.userDisabledBookTickerLayer)
                root.showBookTickerLayer = true
            else if (!root.userDisabledTradesLayer)
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
        root.loadCompareFees()
        if (root.compareMode && root.selectedCompareSourceA !== "")
            root.selectedSourceId = root.selectedCompareSourceA
        else
            root.loadSingleSelectedSource()
        root.refreshBacktestChoices()
    }

    function loadCompareFees() {
        var exchangeA = sourcesModel.exchange(root.selectedCompareSourceA)
        var marketA = sourcesModel.market(root.selectedCompareSourceA)
        var exchangeB = sourcesModel.exchange(root.selectedCompareSourceB)
        var marketB = sourcesModel.market(root.selectedCompareSourceB)
        compareChart.setPrimaryFeeActionBps(compareChart.savedFeeActionBps(exchangeA, marketA))
        compareChart.setSecondaryFeeActionBps(compareChart.savedFeeActionBps(exchangeB, marketB))
    }

    function savePrimaryFee(value) {
        var bps = Number(value)
        if (!isFinite(bps) || bps < 0) bps = 0
        compareChart.setPrimaryFeeActionBps(bps)
        compareChart.saveFeeActionBps(sourcesModel.exchange(root.selectedCompareSourceA),
                                      sourcesModel.market(root.selectedCompareSourceA),
                                      bps)
    }

    function saveSecondaryFee(value) {
        var bps = Number(value)
        if (!isFinite(bps) || bps < 0) bps = 0
        compareChart.setSecondaryFeeActionBps(bps)
        compareChart.saveFeeActionBps(sourcesModel.exchange(root.selectedCompareSourceB),
                                      sourcesModel.market(root.selectedCompareSourceB),
                                      bps)
    }

    function syncCompareIndexesFromIds() {
        root.selectedCompareIndexA = root.indexInCompareRows(root.selectedCompareSourceA)
        root.selectedCompareIndexB = root.indexInCompareRows(root.selectedCompareSourceB)
    }

    function rebuildCompareSourceRows() {
        var rows = [{ id: "", label: "Select session" }]
        var sourceRows = sourcesModel.sourceRows()
        for (var i = 0; i < sourceRows.length; ++i) {
            rows.push(sourceRows[i])
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
            root.selectedCompareSourceA = ""
            root.selectedCompareSourceB = ""
            root.syncCompareIndexesFromIds()
            compareChart.clear()
            return
        }
        root.syncCompareIndexesFromIds()
        root.applyCompareSelection()
    }

    component MeanSecondsField: TextField {
        id: meanField
        property real secondsValue: 5
        Layout.preferredWidth: 58
        text: Number(secondsValue).toFixed(2)
        color: root.textColor
        selectedTextColor: root.windowColor
        selectionColor: root.accentBuyColor
        horizontalAlignment: TextInput.AlignRight
        verticalAlignment: TextInput.AlignVCenter
        font.pixelSize: 12
        validator: DoubleValidator { bottom: 0.1; top: 3600; decimals: 3; notation: DoubleValidator.StandardNotation }
        background: Rectangle {
            radius: 7
            color: meanField.activeFocus ? root.panelAltColor : root.panelColor
            border.color: meanField.activeFocus ? root.accentBuyColor : root.borderColor
            border.width: 1
        }
    }
    component FeeBpsField: TextField {
        id: feeField
        property real feeValue: 0
        Layout.preferredWidth: 64
        text: Number(feeValue).toFixed(2)
        color: root.textColor
        selectedTextColor: root.windowColor
        selectionColor: root.accentBuyColor
        horizontalAlignment: TextInput.AlignRight
        verticalAlignment: TextInput.AlignVCenter
        font.pixelSize: 12
        validator: DoubleValidator { bottom: 0; top: 1000; decimals: 4; notation: DoubleValidator.StandardNotation }
        background: Rectangle {
            radius: 7
            color: feeField.activeFocus ? root.panelAltColor : root.panelColor
            border.color: feeField.activeFocus ? root.accentBuyColor : root.borderColor
            border.width: 1
        }
    }

    background: Rectangle { color: root.windowColor }

    ViewerSourceListModel {
        id: sourcesModel
        captureViewModel: root.captureVm
        recordingCatalog: root.recordingCatalog
    }

    ChartController { id: chart; objectName: "chartController" }
    BookTickerCompareController {
        id: compareChart
        rateLimitVisible: root.showRateLimitLayer
    }
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
        Qt.callLater(root.syncRendererDiagnostics)
        Qt.callLater(root.syncLiveUpdateMode)
        Qt.callLater(root.syncRenderWindow)
    }
    onTabActiveChanged: {
        chart.active = root.tabActive
        if (root.tabActive)
            Qt.callLater(root.refreshBacktestChoices)
    }
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
            if (root.tabActive)
                Qt.callLater(root.refreshBacktestChoices)
            }
        function onRowsInserted() {
            root.rebuildCompareSourceRows()
            Qt.callLater(root.ensureCompareSelection)
            if (root.tabActive)
                Qt.callLater(root.refreshBacktestChoices)
            }
    }

    Connections {
        target: root.backtestVm
        function onRunsChanged() { if (root.tabActive) Qt.callLater(root.refreshBacktestChoices) }
    }

    Connections {
        target: chart
        function onSessionChanged() { Qt.callLater(root.ensureVisibleLayerSelection) }
        function onLiveDataChanged() { Qt.callLater(root.ensureVisibleLayerSelection) }
        function onBacktestResultsChanged() { root.syncBacktestRows() }
        function onBacktestResultChanged() { Qt.callLater(root.syncBacktestComboIndex) }
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
                SessionPickerCombo {
                    id: compareComboA
                    Layout.preferredWidth: 520
                    rows: root.compareSourceRows
                    caption: "Source A"
                    emptyLabel: "Select session"
                    popupWidth: 760
                    currentIndex: root.selectedCompareIndexA
                    onPicked: function(sourceId) {
                        root.userHasExplicitCompareSelection = true
                        root.selectedCompareSourceA = sourceId
                        if (root.selectedCompareSourceB === root.selectedCompareSourceA)
                            root.selectedCompareSourceB = ""
                        root.syncCompareIndexesFromIds()
                        root.applyCompareSelection()
                    }
                }

                Label { text: "fee"; color: root.mutedTextColor }
                FeeBpsField {
                    id: feeAField
                    enabled: root.selectedCompareSourceA !== ""
                    feeValue: compareChart.primaryFeeActionBps
                    onEditingFinished: root.savePrimaryFee(text)
                    onAccepted: root.savePrimaryFee(text)
                }

                Label { text: "Source B"; color: root.mutedTextColor }
                SessionPickerCombo {
                    id: compareComboB
                    Layout.preferredWidth: 520
                    rows: root.compareSourceRows
                    caption: "Source B"
                    emptyLabel: "Select session"
                    popupWidth: 760
                    preferredOpenGroupId: root.compareSourceGroupId(root.effectiveCompareSourceA())
                    scrollToPreferredGroupOnOpen: true
                    currentIndex: root.selectedCompareIndexB
                    onPicked: function(sourceId) {
                        root.userHasExplicitCompareSelection = true
                        root.selectedCompareSourceB = sourceId
                        if (root.selectedCompareSourceB === root.selectedCompareSourceA)
                            root.selectedCompareSourceB = ""
                        root.syncCompareIndexesFromIds()
                        root.applyCompareSelection()
                    }
                }

                Label { text: "fee"; color: root.mutedTextColor }
                FeeBpsField {
                    id: feeBField
                    enabled: root.selectedCompareSourceB !== ""
                    feeValue: compareChart.secondaryFeeActionBps
                    onEditingFinished: root.saveSecondaryFee(text)
                    onAccepted: root.saveSecondaryFee(text)
                }


                Label { text: "mean s"; color: root.mutedTextColor }
                MeanSecondsField {
                    id: meanWindowField
                    secondsValue: compareChart.meanWindowSeconds
                    onEditingFinished: compareChart.setMeanWindowSeconds(Number(text))
                    onAccepted: compareChart.setMeanWindowSeconds(Number(text))
                }

                ViewerBacktestPicker {
                    id: backtestCombo
                    Layout.preferredWidth: 220
                    enabled: root.backtestRows.length > 1 || root.selectedBacktestCountHint() > 0
                    rows: root.backtestRows
                    layoutRoot: root
                    textColor: root.textColor
                    mutedTextColor: root.mutedTextColor
                    panelColor: root.panelColor
                    panelAltColor: root.panelAltColor
                    panelDeepColor: root.panelDeepColor
                    borderColor: root.borderColor
                    accentBuyColor: root.accentBuyColor
                    onRowChosen: function(index) { root.chooseBacktestRow(index) }
                    onRefreshRequested: root.refreshBacktestChoices()
                }
                Label {
                    Layout.fillWidth: true
                    text: root.compareMode
                          ? compareChart.statusText + " | bt A: " + compareChart.primaryCount + " B: " + compareChart.secondaryCount + " spread: " + compareChart.spreadCount
                          : (root.singleSelectedSourceId() !== "" ? chart.statusText : "Pick one session for full viewer or two sessions for spread compare")
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
                        root.rebuildCompareSourceRows()
                        root.ensureCompareSelection()
                        root.refreshBacktestChoices()
                    }
                }
            }
        }

        ViewerLayerToolbar {
            appVm: root.appVm
            chart: chart
            interaction: interaction
            compareMode: root.comparePickerActive
            showTradesLayer: root.showTradesLayer
            showCandlesLayer: root.showCandlesLayer
            showCandles2Layer: root.showCandles2Layer
            showOrderbookLayer: root.showOrderbookLayer
            showBookTickerLayer: root.showBookTickerLayer
            showRateLimitLayer: root.showRateLimitLayer
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
                var nextVisible = !root.showTradesLayer
                if (nextVisible && chart.currentSourceKind === "recorded" && !chart.loadRecordedTrades())
                    nextVisible = false
                root.showTradesLayer = nextVisible
                root.userDisabledTradesLayer = !nextVisible
            }
            onToggleCandles: {
                root.userHasExplicitLayerSelection = true
                var nextVisible = !root.showCandlesLayer
                if (nextVisible && chart.currentSourceKind === "recorded" && !chart.loadRecordedCandles())
                    nextVisible = false
                root.showCandlesLayer = nextVisible
                root.userDisabledCandlesLayer = !nextVisible
            }
            onToggleCandles2: {
                root.userHasExplicitLayerSelection = true
                var nextVisible = !root.showCandles2Layer
                if (nextVisible && chart.currentSourceKind === "recorded" && !chart.loadRecordedCandles2())
                    nextVisible = false
                root.showCandles2Layer = nextVisible
                root.userDisabledCandles2Layer = !nextVisible
            }
            
            onToggleOrderbook: {
                root.userHasExplicitLayerSelection = true
                var nextVisible = !root.showOrderbookLayer
                if (nextVisible && chart.currentSourceKind === "recorded" && !root.loadSelectedRecordedOrderbook())
                    nextVisible = false
                root.showOrderbookLayer = nextVisible
                root.userDisabledOrderbookLayer = !nextVisible
            }
            onToggleBookTicker: {
                root.userHasExplicitLayerSelection = true
                var nextVisible = !root.showBookTickerLayer
                if (nextVisible && chart.currentSourceKind === "recorded" && !chart.loadRecordedBookTicker())
                    nextVisible = false
                root.showBookTickerLayer = nextVisible
                root.userDisabledBookTickerLayer = !nextVisible
            }
            
            
            
            
            onToggleRateLimit: {
                root.showRateLimitLayer = !root.showRateLimitLayer
            }
        }

        Rectangle {
            Layout.fillWidth: true
            color: root.chromeColor
            implicitHeight: 28
            Label {
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                text: root.compareMode
                    ? "Top: combined bookTicker traces and routed backtest markers. Bottom: " + compareChart.lowerPaneTitle + "."
                    : (chart.selectedBacktestResult !== "" || root.preferChartStatusText())
                        ? chart.statusText
                        : "Single source: trades, candles, bookTicker, and orderbook layers are drawn together when present."
                color: root.mutedTextColor
                font.pixelSize: 12
                elide: Text.ElideRight
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
                property real lastY: 0
                property real pressX: 0
                property bool dragActive: false
                property string dragValuePanel: "none"
                property bool measureActive: false
                property bool measureStarted: false
                property real pressY: 0
                onPressed: function(mouse) {
                    lastX = mouse.x
                    lastY = mouse.y
                    pressX = mouse.x
                    pressY = mouse.y
                    dragActive = false
                    dragValuePanel = compareSurface.isPricePanelPoint(mouse.x, mouse.y) ? "price" : (compareSurface.isSpreadPanelPoint(mouse.x, mouse.y) ? "spread" : "none")
                    measureStarted = false
                    measureActive = (mouse.button === Qt.MiddleButton) || ((mouse.button === Qt.LeftButton) && (mouse.modifiers & Qt.ShiftModifier))
                    if (mouse.button === Qt.RightButton) {
                        compareSurface.clearMeasure()
                        mouse.accepted = true
                        return
                    }
                    if (!measureActive) {
                        interaction.plotDragging = true
                        compareSurface.clearHover()
                    }
                }
                onPositionChanged: function(mouse) {
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
                    if (mouse.buttons & Qt.RightButton) {
                        if (!dragActive && Math.abs(mouse.y - pressY) < 3) return
                        dragActive = true
                        var dy = mouse.y - lastY
                        lastY = mouse.y
                        if (dragValuePanel === "price")
                            compareChart.panPrice(dy / Math.max(1, height))
                        else if (dragValuePanel === "spread")
                            compareChart.panSpread(dy / Math.max(1, height))
                        return
                    }
                    if (!(mouse.buttons & Qt.LeftButton)) {
                        compareSurface.setHoverPoint(mouse.x, mouse.y)
                        return
                    }
                    if (!dragActive && Math.abs(mouse.x - pressX) + Math.abs(mouse.y - pressY) < 4) return
                    if (!dragActive) {
                        dragActive = true
                        interaction.startInteractiveMode(interactiveModeTimer)
                        compareSurface.clearHover()
                    }
                    var dx = mouse.x - lastX
                    var ldy = mouse.y - lastY
                    lastX = mouse.x
                    lastY = mouse.y
                    compareChart.panTime(-dx / Math.max(1, width))
                    if (dragValuePanel === "price")
                        compareChart.panPrice(ldy / Math.max(1, height))
                    else if (dragValuePanel === "spread")
                        compareChart.panSpread(ldy / Math.max(1, height))
                }
                onReleased: function(mouse) {
                    if (measureActive && measureStarted)
                        compareSurface.endMeasure()
                    measureStarted = false
                    measureActive = false
                    interaction.plotDragging = false
                    if (dragActive) interaction.stopInteractiveModeSoon(interactiveModeTimer)
                    dragActive = false
                    dragValuePanel = "none"
                }
                onCanceled: {
                    if (measureActive && measureStarted)
                        compareSurface.endMeasure()
                    measureStarted = false
                    measureActive = false
                    interaction.plotDragging = false
                    if (dragActive) interaction.stopInteractiveModeSoon(interactiveModeTimer)
                    dragActive = false
                    dragValuePanel = "none"
                }
                onExited: compareSurface.clearHover()
                onWheel: function(wheel) {
                    var factor = wheel.angleDelta.y > 0 ? 1.3 : (1.0 / 1.3)
                    if (wheel.modifiers & Qt.ControlModifier) {
                        var overPricePanel = compareSurface.isPricePanelPoint(wheel.x, wheel.y)
                        var overSpreadPanel = compareSurface.isSpreadPanelPoint(wheel.x, wheel.y)
                        if (overPricePanel || (!overSpreadPanel && wheel.y < height * 0.67))
                            compareChart.zoomPriceAt(factor, compareSurface.priceAnchorFraction(wheel.y))
                        else
                            compareChart.zoomSpreadAt(factor, compareSurface.spreadAnchorFraction(wheel.y))
                    } else {
                        compareChart.zoomTimeAt(factor, Math.max(0, Math.min(1, wheel.x / Math.max(1, width))))
                    }
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
                anchors.bottomMargin: (rateLimitStripFrame.visible ? rateLimitStripFrame.height : 0) +
                                      (strategyIndicatorFrame.visible ? strategyIndicatorFrame.height : 0)
                clip: true
                Component {
                    id: cpuChartComponent
                    ChartItem {
                        anchors.fill: parent
                        controller: chart
                        tradesVisible: root.showTradesLayer
                        candlesVisible: root.showCandlesLayer
                        candles2Visible: root.showCandles2Layer
                        orderbookVisible: root.showOrderbookLayer
                        bookTickerVisible: root.effectiveBookTickerLayer
                        tradeAmountScale: root.appVm.tradeAmountScale
                        candleWidthPx: root.appVm.candleWidthPx
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
                        candlesVisible: root.showCandlesLayer
                        candles2Visible: root.showCandles2Layer
                        orderbookVisible: root.showOrderbookLayer
                        bookTickerVisible: root.effectiveBookTickerLayer
                        tradeAmountScale: root.appVm.tradeAmountScale
                        candleWidthPx: root.appVm.candleWidthPx
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
                        if (!interaction.anyHoverableLayerVisible(root.showTradesLayer, root.effectiveBookTickerLayer, root.showOrderbookLayer)) return
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
            Item {
                id: rateLimitStripFrame
                visible: !root.comparePickerActive && root.showRateLimitLayer && chart.hasRateLimitUsage
                anchors.left: parent.left
                anchors.right: priceScale.left
                anchors.bottom: strategyIndicatorFrame.visible ? strategyIndicatorFrame.top : timeScale.top
                height: visible ? 44 : 0
                clip: true
                RateLimitStripItem {
                    anchors.fill: parent
                    controller: chart
                }
                Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 1; color: root.borderColor }
            }
            Item {
                id: strategyIndicatorFrame
                visible: !root.comparePickerActive && chart.hasStrategyIndicator
                anchors.left: parent.left
                anchors.right: priceScale.left
                anchors.bottom: timeScale.top
                height: visible ? Math.min(170, Math.max(120, parent.height * 0.24)) : 0
                clip: true
                StrategyIndicatorItem {
                    anchors.fill: parent
                    controller: chart
                }
                Rectangle { anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top; height: 1; color: root.borderColor }
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
        }
    }
}




