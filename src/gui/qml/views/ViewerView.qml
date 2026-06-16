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
    property bool showLiquidationsLayer: false
    property bool showCandlesLayer: false
    property bool showOrderbookLayer: false
    property bool showBookTickerLayer: true
    property bool showMarkPriceLayer: false
    property bool showIndexPriceLayer: false
    property bool showFundingLayer: false
    property bool showPriceLimitLayer: false
    property bool effectiveBookTickerLayer: showBookTickerLayer
    property bool userHasExplicitLayerSelection: false
    property bool userDisabledTradesLayer: false
    property bool userDisabledLiquidationsLayer: false
    property bool userDisabledCandlesLayer: false
    property bool userDisabledOrderbookLayer: false
    property bool userDisabledBookTickerLayer: false
    property bool userDisabledMarkPriceLayer: false
    property bool userDisabledIndexPriceLayer: false
    property bool userDisabledFundingLayer: false
    property bool userDisabledPriceLimitLayer: false
    property bool useDedicatedGpuPath: false
    property bool useGpuRenderer: true
    property string performanceDiagnosticsText: ""

    function chartSurface() { return chartLoader.item }
    function syncRendererDiagnostics() { root.appVm.activeChartRenderer = root.useDedicatedGpuPath ? "gpu-orderbook" : "cpu-chart" }
    function syncLiveUpdateMode() {
        chart.setLiveUpdateIntervalMs(root.appVm.liveUpdateIntervalMs)
        compareChart.setLiveUpdateIntervalMs(root.appVm.liveUpdateIntervalMs)
    }
    function syncRenderWindow() { chart.setRenderWindowSeconds(root.appVm.renderWindowSeconds) }
    function refreshPerformanceDiagnostics() {
        root.performanceDiagnosticsText = root.tabActive && !root.compareMode ? chart.performanceDiagnostics() : ""
    }

    function syncBacktestRows() {
        var rows = [{ path: "", sessionPath: "", label: "Backtest" }]
        for (var i = 0; i < chart.backtestResults.length; ++i)
            rows.push(chart.backtestResults[i])
        root.backtestRows = rows
        root.syncBacktestComboIndex()
    }

    function backtestPrimarySessionPath() {
        if (root.compareMode)
            return sourcesModel.sessionPath(root.selectedCompareSourceA)
        if (root.selectedSourceId !== "" && sourcesModel.sourceKind(root.selectedSourceId) === "recorded")
            return sourcesModel.sessionPath(root.selectedSourceId)
        if (root.selectedCompareSourceA !== "" && sourcesModel.sourceKind(root.selectedCompareSourceA) === "recorded")
            return sourcesModel.sessionPath(root.selectedCompareSourceA)
        return ""
    }

    function backtestSecondarySessionPath() {
        return root.compareMode ? sourcesModel.sessionPath(root.selectedCompareSourceB) : ""
    }

    function refreshBacktestChoices() {
        chart.refreshBacktestResults(root.backtestPrimarySessionPath(),
                                     root.backtestSecondarySessionPath())
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
        if (root.selectedSourceId === "" || sourcesModel.sourceKind(root.selectedSourceId) !== "recorded")
            return
        chart.loadRecordedOrderbook()
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
        return ""
    }

    function loadSingleSelectedSource() {
        var sourceId = root.singleSelectedSourceId()
        var sourceChanged = root.selectedSourceId !== sourceId
        if (sourceChanged) {
            root.userHasExplicitLayerSelection = false
            root.userDisabledTradesLayer = false
            root.userDisabledLiquidationsLayer = false
            root.userDisabledCandlesLayer = false
            root.userDisabledOrderbookLayer = false
            root.userDisabledBookTickerLayer = false
            root.userDisabledMarkPriceLayer = false
            root.userDisabledIndexPriceLayer = false
            root.userDisabledFundingLayer = false
            root.userDisabledPriceLimitLayer = false
            root.showTradesLayer = false
            root.showLiquidationsLayer = false
            root.showCandlesLayer = false
            root.showOrderbookLayer = false
            root.showBookTickerLayer = true
            root.showMarkPriceLayer = false
            root.showIndexPriceLayer = false
            root.showFundingLayer = false
            root.showPriceLimitLayer = false
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
                root.showLiquidationsLayer = false
                root.showCandlesLayer = false
                root.showOrderbookLayer = false
                root.showBookTickerLayer = true
                return
            }
            root.showTradesLayer = chart.hasTrades && !root.userDisabledTradesLayer
            root.showLiquidationsLayer = chart.hasLiquidations && !chart.hasTrades && !root.userDisabledLiquidationsLayer
            root.showCandlesLayer = chart.hasCandles && !root.userDisabledCandlesLayer
            root.showOrderbookLayer = chart.hasOrderbook && !root.userDisabledOrderbookLayer
            root.showMarkPriceLayer = chart.hasMarkPrice && !root.userDisabledMarkPriceLayer
            root.showIndexPriceLayer = chart.hasIndexPrice && !root.userDisabledIndexPriceLayer
            root.showFundingLayer = chart.hasFunding && !root.userDisabledFundingLayer
            root.showPriceLimitLayer = chart.hasPriceLimit && !root.userDisabledPriceLimitLayer
            root.showBookTickerLayer = false
            if (!root.showTradesLayer && !root.showLiquidationsLayer && !root.showCandlesLayer && !root.showOrderbookLayer && !root.showBookTickerLayer
                    && !root.showMarkPriceLayer && !root.showIndexPriceLayer && !root.showFundingLayer && !root.showPriceLimitLayer)
                root.showTradesLayer = !root.userDisabledTradesLayer
            return
        }

        if (root.showTradesLayer || root.showLiquidationsLayer || root.showCandlesLayer) {
            root.showTradesLayer = false
            root.showLiquidationsLayer = false
            if (chart.hasOrderbook && !root.userDisabledOrderbookLayer)
                root.showOrderbookLayer = true
            else if (chart.hasBookTicker && !root.userDisabledBookTickerLayer)
                root.showBookTickerLayer = true
            else if (chart.hasMarkPrice && !root.userDisabledMarkPriceLayer)
                root.showMarkPriceLayer = true
            else if (chart.hasIndexPrice && !root.userDisabledIndexPriceLayer)
                root.showIndexPriceLayer = true
            else if (chart.hasFunding && !root.userDisabledFundingLayer)
                root.showFundingLayer = true
            else if (chart.hasPriceLimit && !root.userDisabledPriceLimitLayer)
                root.showPriceLimitLayer = true
        }

        if (!root.showTradesLayer && !root.showLiquidationsLayer && !root.showCandlesLayer && !root.showOrderbookLayer && !root.showBookTickerLayer
                && !root.showMarkPriceLayer && !root.showIndexPriceLayer && !root.showFundingLayer && !root.showPriceLimitLayer) {
            if (chart.hasOrderbook && !root.userDisabledOrderbookLayer)
                root.showOrderbookLayer = true
            else if (chart.hasBookTicker && !root.userDisabledBookTickerLayer)
                root.showBookTickerLayer = true
            else if (chart.hasMarkPrice && !root.userDisabledMarkPriceLayer)
                root.showMarkPriceLayer = true
            else if (chart.hasIndexPrice && !root.userDisabledIndexPriceLayer)
                root.showIndexPriceLayer = true
            else if (chart.hasFunding && !root.userDisabledFundingLayer)
                root.showFundingLayer = true
            else if (chart.hasPriceLimit && !root.userDisabledPriceLimitLayer)
                root.showPriceLimitLayer = true
            else if (chart.hasLiquidations && !root.userDisabledLiquidationsLayer)
                root.showLiquidationsLayer = true
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
        for (var i = 0; i < sourcesModel.rowCount(); ++i) {
            var id = sourcesModel.sourceIdAt(i)
            if (id !== "") {
                var label = sourcesModel.labelAt(i)
                var summary = sourcesModel.sourceSummary(id)
                rows.push({ id: id, label: label, rightText: summary })
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
            root.selectedCompareSourceA = ""
            root.selectedCompareSourceB = ""
            root.syncCompareIndexesFromIds()
            compareChart.clear()
            return
        }
        root.syncCompareIndexesFromIds()
        root.applyCompareSelection()
    }

    component DarkSourceCombo: ComboBox {
        id: combo
        Layout.preferredWidth: 520
        model: root.compareSourceRows
        textRole: "label"
        valueRole: "id"
        property string searchText: ""
        property var filteredRows: []
        signal sourcePicked(string sourceId)
        function rebuildFilter() {
            var needle = combo.searchText.trim().toLowerCase()
            var rows = []
            for (var i = 0; i < root.compareSourceRows.length; ++i) {
                var row = root.compareSourceRows[i]
                var haystack = (row.label + " " + row.id + " " + (row.rightText || "")).toLowerCase()
                if (needle.length === 0 || haystack.indexOf(needle) !== -1)
                    rows.push({ "index": i, "label": row.label, "id": row.id, "rightText": row.rightText || "" })
            }
            combo.filteredRows = rows
        }
        function selectFilteredRow(row) {
            if (!row || row.index < 0)
                return
            combo.currentIndex = row.index
            combo.popup.close()
            combo.sourcePicked(row.id || "")
        }
        onSearchTextChanged: rebuildFilter()
        onModelChanged: rebuildFilter()
        contentItem: RowLayout {
            spacing: 8
            Text {
                Layout.fillWidth: true
                text: combo.currentIndex <= 0 ? "Select session" : combo.displayText
                color: combo.currentIndex <= 0 ? root.mutedTextColor : root.textColor
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                leftPadding: 10
            }
            Text {
                Layout.preferredWidth: visible ? Math.max(210, implicitWidth + 8) : 0
                rightPadding: 28
                text: combo.currentIndex > 0 && root.compareSourceRows[combo.currentIndex] ? (root.compareSourceRows[combo.currentIndex].rightText || "") : ""
                visible: text.length > 0
                color: root.mutedTextColor
                font.pixelSize: 12
                font.bold: true
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
            }
        }
        background: Rectangle {
            radius: 7
            color: combo.down ? root.panelAltColor : root.panelColor
            border.color: combo.activeFocus ? root.accentBuyColor : root.borderColor
            border.width: 1
        }
        delegate: Component {
            ItemDelegate {
                width: combo.popup.width
                text: modelData.label
                highlighted: combo.highlightedIndex === index
                contentItem: RowLayout {
                    spacing: 8
                    Text {
                        Layout.fillWidth: true
                        text: modelData.label
                        color: modelData.index === 0 ? root.mutedTextColor : root.textColor
                        elide: Text.ElideRight
                        verticalAlignment: Text.AlignVCenter
                    }
                    Text {
                        Layout.preferredWidth: visible ? Math.max(210, implicitWidth + 8) : 0
                        text: modelData.rightText || ""
                        visible: text.length > 0
                        color: root.mutedTextColor
                        font.pixelSize: 12
                        font.bold: true
                        horizontalAlignment: Text.AlignRight
                        verticalAlignment: Text.AlignVCenter
                    }
                }
                background: Rectangle {
                    color: highlighted ? root.panelAltColor : root.panelColor
                }
                onClicked: combo.selectFilteredRow(modelData)
            }
        }
        popup: Popup {
            y: combo.height + 2
            width: Math.max(combo.width, 760)
            implicitHeight: Math.min(contentItem.implicitHeight, 400)
            padding: 1
            onOpened: {
                combo.searchText = ""
                combo.rebuildFilter()
                sourceSearchField.forceActiveFocus()
            }
            contentItem: Column {
                width: combo.popup.width
                spacing: 4

                Rectangle {
                    width: parent.width - 8
                    x: 4
                    height: 30
                    radius: 5
                    color: root.panelDeepColor
                    border.color: sourceSearchField.activeFocus ? root.accentBuyColor : root.borderColor
                    border.width: 1

                    Text { anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; text: "Search"; visible: sourceSearchField.text.length === 0; color: root.mutedTextColor; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                    TextInput {
                        id: sourceSearchField
                        anchors.fill: parent
                        anchors.leftMargin: 8
                        anchors.rightMargin: 8
                        text: combo.searchText
                        color: root.textColor
                        selectionColor: root.accentBuyColor
                        selectedTextColor: root.panelDeepColor
                        font.pixelSize: 12
                        selectByMouse: true
                        clip: true
                        verticalAlignment: TextInput.AlignVCenter
                        onTextChanged: combo.searchText = text
                        Keys.onPressed: function(event) {
                            if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && combo.filteredRows.length > 0) {
                                combo.selectFilteredRow(combo.filteredRows[0])
                                event.accepted = true
                            } else if (event.key === Qt.Key_Escape) {
                                if (combo.searchText.length > 0) {
                                    combo.searchText = ""
                                    sourceSearchField.text = ""
                                } else {
                                    combo.popup.close()
                                }
                                event.accepted = true
                            }
                        }
                    }
                }

                ListView {
                    id: sourceResultList
                    width: parent.width
                    height: Math.min(contentHeight, 330)
                    clip: true
                    model: combo.popup.visible ? combo.filteredRows : []
                    currentIndex: 0
                    delegate: combo.delegate
                }

                Text {
                    id: sourceEmptyText
                    width: parent.width
                    height: 30
                    visible: combo.filteredRows.length === 0
                    text: "No matches"
                    color: root.mutedTextColor
                    font.pixelSize: 12
                    horizontalAlignment: Text.AlignHCenter
                    verticalAlignment: Text.AlignVCenter
                }
            }
            background: Rectangle {
                color: root.panelColor
                border.color: root.borderColor
                radius: 7
            }
        }
        Component.onCompleted: currentIndex = 0
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
        Component.onCompleted: reload()
    }

    ChartController { id: chart; objectName: "chartController" }
    BookTickerCompareController { id: compareChart }
    ViewerInteractionState { id: interaction }
    Timer { id: interactiveModeTimer; interval: 120; repeat: false; onTriggered: interaction.interactiveMode = false }
    Timer {
        id: performanceDiagnosticsTimer
        interval: 1000
        repeat: true
        running: root.tabActive && !root.compareMode
        triggeredOnStart: true
        onTriggered: root.refreshPerformanceDiagnostics()
    }
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
            Qt.callLater(root.refreshBacktestChoices)
            }
        function onRowsInserted() {
            root.rebuildCompareSourceRows()
            Qt.callLater(root.ensureCompareSelection)
            Qt.callLater(root.refreshBacktestChoices)
            }
    }

    Connections {
        target: root.backtestVm
        function onRunsChanged() { Qt.callLater(root.refreshBacktestChoices) }
    }

    Connections {
        target: chart
        function onSessionChanged() { Qt.callLater(root.ensureVisibleLayerSelection) }
        function onLiveDataChanged() { Qt.callLater(root.ensureVisibleLayerSelection) }
        function onBacktestResultsChanged() { Qt.callLater(root.syncBacktestRows) }
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
                DarkSourceCombo {
                    id: compareComboA
                    currentIndex: root.selectedCompareIndexA
                    onSourcePicked: function(sourceId) {
                        root.userHasExplicitCompareSelection = true
                        root.selectedCompareSourceA = sourceId
                        if (root.selectedCompareSourceB === root.selectedCompareSourceA)
                            root.selectedCompareSourceB = ""
                        root.syncCompareIndexesFromIds()
                        root.applyCompareSelection()
                    }
                    onActivated: function(index) {
                        var sourceId = index <= 0 || index >= root.compareSourceRows.length ? "" : root.compareSourceRows[index].id
                        compareComboA.sourcePicked(sourceId)
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
                DarkSourceCombo {
                    id: compareComboB
                    currentIndex: root.selectedCompareIndexB
                    onSourcePicked: function(sourceId) {
                        root.userHasExplicitCompareSelection = true
                        root.selectedCompareSourceB = sourceId
                        if (root.selectedCompareSourceB === root.selectedCompareSourceA)
                            root.selectedCompareSourceB = ""
                        root.syncCompareIndexesFromIds()
                        root.applyCompareSelection()
                    }
                    onActivated: function(index) {
                        var sourceId = index <= 0 || index >= root.compareSourceRows.length ? "" : root.compareSourceRows[index].id
                        compareComboB.sourcePicked(sourceId)
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

                ComboBox {
                    id: backtestCombo
                    Layout.preferredWidth: 220
                    enabled: root.backtestRows.length > 1
                    model: root.backtestRows
                    textRole: "label"
                    valueRole: "path"
                    property string searchText: ""
                    property var filteredRows: []
                    function rebuildFilter() {
                        var needle = backtestCombo.searchText.trim().toLowerCase()
                        var rows = []
                        for (var i = 0; i < root.backtestRows.length; ++i) {
                            var row = root.backtestRows[i]
                            var haystack = (row.label + " " + row.path).toLowerCase()
                            if (needle.length === 0 || haystack.indexOf(needle) !== -1)
                                rows.push({ "index": i, "label": row.label, "path": row.path, "pnlText": row.pnlText || (row.selectable === false ? "sweep" : ""), "selectable": row.selectable !== false })
                        }
                        backtestCombo.filteredRows = rows
                    }
                    function selectFilteredRow(row) {
                        if (!row || row.index < 0)
                            return
                        backtestCombo.currentIndex = row.index
                        backtestCombo.popup.close()
                        root.chooseBacktestRow(row.index)
                    }
                    onSearchTextChanged: rebuildFilter()
                    onModelChanged: rebuildFilter()
                    onActivated: function(index) { root.chooseBacktestRow(index) }
                    contentItem: Text {
                        text: backtestCombo.displayText === "" ? "Backtest" : backtestCombo.displayText
                        color: backtestCombo.enabled ? root.textColor : root.mutedTextColor
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        leftPadding: 10
                        rightPadding: 28
                    }
                    background: Rectangle {
                        radius: 7
                        color: backtestCombo.down ? root.panelAltColor : root.panelColor
                        border.color: backtestCombo.activeFocus ? root.accentBuyColor : root.borderColor
                        border.width: 1
                    }
                    delegate: Component {
                        ItemDelegate {
                            width: backtestCombo.popup.width
                            text: modelData.label
                            highlighted: backtestCombo.highlightedIndex === index
                            contentItem: RowLayout {
                                spacing: 8
                                Text {
                                    Layout.fillWidth: true
                                    text: modelData.label
                                    color: modelData.index === 0 || modelData.selectable === false ? root.mutedTextColor : root.textColor
                                    elide: Text.ElideRight
                                    verticalAlignment: Text.AlignVCenter
                                }
                                Text {
                                    Layout.preferredWidth: visible ? 64 : 0
                                    text: modelData.pnlText || ""
                                    visible: text.length > 0
                                    color: text.charAt(0) === "-" ? "#ef6f6c" : root.accentBuyColor
                                    font.pixelSize: 12
                                    font.bold: true
                                    horizontalAlignment: Text.AlignRight
                                    verticalAlignment: Text.AlignVCenter
                                }
                            }
                            background: Rectangle { color: highlighted ? root.panelAltColor : root.panelColor }
                            onClicked: backtestCombo.selectFilteredRow(modelData)
                        }
                    }
                    popup: Popup {
                        y: backtestCombo.height + 2
                        x: Math.min(0, backtestCombo.width - width)
                        width: Math.min(root.width - 32, Math.max(backtestCombo.width, 420))
                        implicitHeight: Math.min(contentItem.implicitHeight, 360)
                        padding: 1
                        onOpened: {
                            root.refreshBacktestChoices()
                            backtestCombo.searchText = ""
                            backtestCombo.rebuildFilter()
                            backtestSearchField.forceActiveFocus()
                        }
                        contentItem: Column {
                            width: backtestCombo.popup.width
                            spacing: 4

                            Rectangle {
                                width: parent.width - 8
                                x: 4
                                height: 30
                                radius: 5
                                color: root.panelDeepColor
                                border.color: backtestSearchField.activeFocus ? root.accentBuyColor : root.borderColor
                                border.width: 1

                                Text { anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; text: "Search"; visible: backtestSearchField.text.length === 0; color: root.mutedTextColor; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                                TextInput {
                                    id: backtestSearchField
                                    anchors.fill: parent
                                    anchors.leftMargin: 8
                                    anchors.rightMargin: 8
                                    text: backtestCombo.searchText
                                    color: root.textColor
                                    selectionColor: root.accentBuyColor
                                    selectedTextColor: root.panelDeepColor
                                    font.pixelSize: 12
                                    selectByMouse: true
                                    clip: true
                                    verticalAlignment: TextInput.AlignVCenter
                                    onTextChanged: backtestCombo.searchText = text
                                    Keys.onPressed: function(event) {
                                        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && backtestCombo.filteredRows.length > 0) {
                                            backtestCombo.selectFilteredRow(backtestCombo.filteredRows[0])
                                            event.accepted = true
                                        } else if (event.key === Qt.Key_Escape) {
                                            if (backtestCombo.searchText.length > 0) {
                                                backtestCombo.searchText = ""
                                                backtestSearchField.text = ""
                                            } else {
                                                backtestCombo.popup.close()
                                            }
                                            event.accepted = true
                                        }
                                    }
                                }
                            }

                            ListView {
                                id: backtestResultList
                                width: parent.width
                                height: Math.min(contentHeight, 290)
                                clip: true
                                model: backtestCombo.popup.visible ? backtestCombo.filteredRows : []
                                currentIndex: 0
                                delegate: backtestCombo.delegate
                            }

                            Text {
                                id: backtestEmptyText
                                width: parent.width
                                height: 30
                                visible: backtestCombo.filteredRows.length === 0
                                text: "No matches"
                                color: root.mutedTextColor
                                font.pixelSize: 12
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                        background: Rectangle {
                            color: root.panelColor
                            border.color: root.borderColor
                            radius: 7
                        }
                    }
                }
                Label {
                    Layout.fillWidth: true
                    text: root.compareMode
                          ? compareChart.statusText + " | A: " + compareChart.primaryCount + " B: " + compareChart.secondaryCount + " spread: " + compareChart.spreadCount
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
            showCandlesLayer: root.showCandlesLayer
            showOrderbookLayer: root.showOrderbookLayer
            showBookTickerLayer: root.showBookTickerLayer
            showMarkPriceLayer: root.showMarkPriceLayer
            showIndexPriceLayer: root.showIndexPriceLayer
            showFundingLayer: root.showFundingLayer
            showPriceLimitLayer: root.showPriceLimitLayer
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
                root.showTradesLayer = nextVisible
                root.userDisabledTradesLayer = !nextVisible
            }
            onToggleCandles: {
                root.userHasExplicitLayerSelection = true
                var nextVisible = !root.showCandlesLayer
                root.showCandlesLayer = nextVisible
                root.userDisabledCandlesLayer = !nextVisible
            }
            onToggleLiquidations: {
                root.userHasExplicitLayerSelection = true
                var nextVisible = !root.showLiquidationsLayer
                root.showLiquidationsLayer = nextVisible
                root.userDisabledLiquidationsLayer = !nextVisible
            }
            onToggleOrderbook: {
                root.userHasExplicitLayerSelection = true
                var nextVisible = !root.showOrderbookLayer
                root.showOrderbookLayer = nextVisible
                root.userDisabledOrderbookLayer = !nextVisible
                if (nextVisible) root.loadSelectedRecordedOrderbook()
            }
            onToggleBookTicker: {
                root.userHasExplicitLayerSelection = true
                var nextVisible = !root.showBookTickerLayer
                root.showBookTickerLayer = nextVisible
                root.userDisabledBookTickerLayer = !nextVisible
            }
            onToggleMarkPrice: {
                root.userHasExplicitLayerSelection = true
                var nextVisible = !root.showMarkPriceLayer
                root.showMarkPriceLayer = nextVisible
                root.userDisabledMarkPriceLayer = !nextVisible
            }
            onToggleIndexPrice: {
                root.userHasExplicitLayerSelection = true
                var nextVisible = !root.showIndexPriceLayer
                root.showIndexPriceLayer = nextVisible
                root.userDisabledIndexPriceLayer = !nextVisible
            }
            onToggleFunding: {
                root.userHasExplicitLayerSelection = true
                var nextVisible = !root.showFundingLayer
                root.showFundingLayer = nextVisible
                root.userDisabledFundingLayer = !nextVisible
            }
            onTogglePriceLimit: {
                root.userHasExplicitLayerSelection = true
                var nextVisible = !root.showPriceLimitLayer
                root.showPriceLimitLayer = nextVisible
                root.userDisabledPriceLimitLayer = !nextVisible
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
                    ? "Top: combined bookTicker traces and routed backtest markers. Bottom: best-side spread in bps with rolling mean and cost band."
                    : (chart.selectedBacktestResult !== "" || root.preferChartStatusText())
                        ? chart.statusText
                        : root.tabActive && root.performanceDiagnosticsText !== ""
                            ? root.performanceDiagnosticsText
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
                anchors.bottomMargin: chart.hasStrategyIndicator ? strategyIndicatorFrame.height : 0
                clip: true
                Component {
                    id: cpuChartComponent
                    ChartItem {
                        anchors.fill: parent
                        controller: chart
                        tradesVisible: root.showTradesLayer
                        liquidationsVisible: root.showLiquidationsLayer
                        candlesVisible: root.showCandlesLayer
                        orderbookVisible: root.showOrderbookLayer
                        bookTickerVisible: root.effectiveBookTickerLayer
                        markPriceVisible: root.showMarkPriceLayer
                        indexPriceVisible: root.showIndexPriceLayer
                        fundingVisible: root.showFundingLayer
                        priceLimitVisible: root.showPriceLimitLayer
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
                        liquidationsVisible: root.showLiquidationsLayer
                        candlesVisible: root.showCandlesLayer
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

















