import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HftRecorder 1.0

Pane {
    id: root
    padding: 0

    required property var backtestVm
    required property var captureVm
    required property bool tabActive

    property color windowColor: "#111216"
    property color chromeColor: "#1b1d23"
    property color panelColor: "#22252d"
    property color panelDeepColor: "#15171c"
    property color panelAltColor: "#2b303a"
    property color borderColor: "#343844"
    property color textColor: "#f1f4f8"
    property color mutedTextColor: "#a8afbd"
    property color accentColor: "#24c2cb"
    property color goodColor: "#82d46b"
    property color badColor: "#ef6f6c"
    property int selectedSweepPointId: -1
    property bool sweepPercentMode: false
    property bool showRawSummary: false
    property var secondarySessionRows: []
    property var sweepPalette: ["#24c2cb", "#82d46b", "#f0b35a", "#ef6f6c", "#a98cf5", "#5ca9ff", "#d77ad9", "#8bd3dd", "#c4d76b", "#ff8a5b"]

    function e8Text(value) {
        var negative = Number(value) < 0
        var absValue = Math.abs(Number(value)) / 100000000.0
        var text = absValue.toFixed(4)
        while (text.indexOf(".") >= 0 && text.endsWith("0")) text = text.slice(0, -1)
        if (text.endsWith(".")) text = text.slice(0, -1)
        return (negative ? "-" : "") + text
    }

    function sweepValue(valueE8, initialBalanceE8) {
        if (root.sweepPercentMode && Number(initialBalanceE8) > 0) return (Number(valueE8) * 100.0) / Number(initialBalanceE8)
        return Number(valueE8)
    }

    function sweepText(value, initialBalanceE8) {
        if (root.sweepPercentMode && Number(initialBalanceE8) > 0) return Number(value).toFixed(2) + "%"
        return root.e8Text(value)
    }

    function selectedRunMetric() {
        var metrics = root.backtestVm.selectedResultMetrics
        var key = root.backtestVm.selectedResultMetricKey
        for (var i = 0; i < metrics.length; ++i) {
            if (metrics[i].key === key) return metrics[i]
        }
        return metrics.length > 0 ? metrics[0] : ({})
    }

    function selectedMetricField(field) {
        var metric = selectedRunMetric()
        return metric && metric[field] ? metric[field] : ""
    }

    function hasSelectedVisualData() {
        if (!root.backtestVm.selectedDetailsLoaded) return false
        if (root.backtestVm.selectedIsSweep)
            return root.backtestVm.selectedSweepCurves.length > 0 || root.backtestVm.selectedSweepRows.length > 0
        return root.backtestVm.selectedEquityPoints.length >= 2
    }

    function selectedErrorDisplayText() {
        return root.backtestVm.selectedErrorText !== "" ? root.backtestVm.selectedErrorText : root.backtestVm.selectedDetailsErrorText
    }

    function loadOrReloadVisual() {
        if (root.backtestVm.selectedDetailsLoaded && !root.hasSelectedVisualData())
            root.backtestVm.unloadSelectedRunDetails()
        root.backtestVm.loadSelectedRunDetails()
    }

    function metricPointText(value, metricKey) {
        if (root.backtestVm.selectedResultMetricRatioKey.length > 0) return Number(value).toFixed(4) + "x"
        return metricKey.endsWith("_e8") ? root.e8Text(value) : String(Math.round(Number(value) * 1000) / 1000)
    }

    function executionPoints(entrySide) {
        var source = root.backtestVm.selectedExecutionQualityPoints || []
        var out = []
        for (var i = 0; i < source.length; ++i) {
            var phase = Number(source[i].phase)
            if (entrySide ? (phase === 1 || phase === 2) : (phase === 3 || phase === 4)) out.push(source[i])
        }
        return out
    }

    function incompleteExecutionPoints() {
        var source = root.backtestVm.selectedExecutionQualityPoints || []
        var out = []
        for (var i = 0; i < source.length && out.length < 8; ++i) {
            if (Number(source[i].status) !== 2) out.push(source[i])
        }
        return out
    }

    function firstExtraSessionId() {
        var text = String(root.backtestVm.extraSessionIds || "").trim()
        if (text.length === 0)
            return ""
        return text.split(/[,;\n]+/)[0].trim()
    }

    function sessionGroupId(sessionId) {
        var target = String(sessionId || "").trim()
        if (target.length === 0)
            return ""
        var sessions = root.backtestVm.sessions || []
        for (var i = 0; i < sessions.length; ++i) {
            var row = sessions[i]
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

    function sessionRowForId(sessionId) {
        var target = String(sessionId || "").trim()
        if (target.length === 0)
            return ({})
        var sessions = root.backtestVm.sessions || []
        for (var i = 0; i < sessions.length; ++i) {
            var row = sessions[i]
            if (row && String(row.id || "") === target)
                return row
        }
        return ({})
    }

    function sessionHasMultipleLegs(sessionId) {
        var row = root.sessionRowForId(sessionId)
        if (!row)
            return false
        if (row.isGroup === true)
            return true
        return row.sessionPaths !== undefined && row.sessionPaths !== null && row.sessionPaths.length > 1
    }

    function primarySessionIsGrouped() {
        return root.sessionHasMultipleLegs(root.backtestVm.selectedSessionId)
    }

    function pickPrimarySession(id) {
        var openLegSelector = root.sessionHasMultipleLegs(id)
        if (!openLegSelector && root.firstExtraSessionId() === id)
            root.backtestVm.setExtraSessionIds("")
        if (openLegSelector)
            root.backtestVm.setSelectedSessionIdForLegSelection(id)
        else
            root.backtestVm.setSelectedSessionId(id)
        if (openLegSelector)
            Qt.callLater(function() { resultsToolbar.openLegPopup() })
    }

    function rebuildSecondarySessionRows() {
        var rows = [{ "id": "", "label": "No extra legs", "rightText": "" }]
        var sessions = root.backtestVm.sessions || []
        var primaryId = String(root.backtestVm.selectedSessionId || "")
        var currentExtra = root.firstExtraSessionId()
        var currentFound = currentExtra.length === 0
        for (var i = 0; i < sessions.length; ++i) {
            var row = sessions[i]
            if (!row)
                continue
            var id = String(row.id || "")
            var selectable = row.selectable !== false
            if (row.isGroup === true) {
                rows.push(row)
                continue
            }
            if (!selectable || id.length === 0 || id === primaryId)
                continue
            if (id === currentExtra)
                currentFound = true
            rows.push(row)
        }
        if (!currentFound && currentExtra !== primaryId)
            rows.push({ "id": currentExtra, "label": currentExtra, "rightText": "custom" })
        root.secondarySessionRows = rows
    }

    function syncSelections() {
        resultsToolbar.syncSelections()
    }

    background: Rectangle { color: root.windowColor }


    Connections {
        target: root.backtestVm
        function onSessionsChanged() { root.syncSelections() }
        function onSelectedSessionChanged() { root.syncSelections() }
        function onMultiSessionChanged() { root.syncSelections() }
        function onSymbolChanged() { resultsToolbar.setSymbol() }
        function onSelectedStrategyChanged() { root.syncSelections() }
        function onConfigChanged() { root.syncSelections() }
        function onIndicatorProfileChanged() { root.syncSelections() }
        function onSelectionChanged() { root.selectedSweepPointId = -1; root.sweepPercentMode = false; root.showRawSummary = false }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        BacktestResultsToolbar { id: resultsToolbar; viewRoot: root }

        BacktestStrategyParametersPanel {
            backtestVm: root.backtestVm
        }

        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.bottomMargin: 10
            orientation: Qt.Horizontal

            BacktestResultsRunList { viewRoot: root }

            BacktestResultsSummary { viewRoot: root }
        }
    }
}
