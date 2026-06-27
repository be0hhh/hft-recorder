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
    property color panelAltColor: "#2b303a"
    property color borderColor: "#343844"
    property color textColor: "#f1f4f8"
    property color mutedTextColor: "#a8afbd"
    property color accentColor: "#24c2cb"
    property color goodColor: "#82d46b"
    property color badColor: "#ef6f6c"
    property string selectedMode: "overview"
    property var modeChoices: [
        { "id": "overview", "label": "Overview" },
        { "id": "matrix", "label": "Pair Matrix" },
        { "id": "time", "label": "Time Split" },
        { "id": "plateau", "label": "Plateau" },
        { "id": "raw", "label": "Raw Tables" }
    ]
    property var rawModeChoices: [
        { "id": "stable", "label": "Stable" },
        { "id": "profit", "label": "Max profit" },
        { "id": "symbols", "label": "Symbols" },
        { "id": "pairs", "label": "Pairs" },
        { "id": "params", "label": "Params" },
        { "id": "skipped", "label": "Skipped" }
    ]

    function e8Text(value) {
        var raw = Number(value || 0)
        var negative = raw < 0
        var absValue = Math.abs(raw) / 100000000.0
        var text = absValue.toFixed(4)
        while (text.indexOf(".") >= 0 && text.endsWith("0")) text = text.slice(0, -1)
        if (text.endsWith(".")) text = text.slice(0, -1)
        return (negative ? "-" : "") + text
    }

    function modeIndex() {
        for (var i = 0; i < modeChoices.length; ++i) {
            if (modeChoices[i].id === selectedMode)
                return i
        }
        return 0
    }

    function modeTitle() {
        if (selectedMode === "matrix") return "Exchange pair matrix"
        if (selectedMode === "time") return "Time split stability"
        if (selectedMode === "plateau") return "Parameter plateau"
        if (selectedMode === "raw") return "Raw leaderboards"
        return "Quant overview"
    }

    function modeDescription() {
        if (selectedMode === "matrix") return "Биржа x биржа. Ячейка показывает насколько связка стабильно дает fills, плюс и низкий drawdown по всем монетам и параметрам."
        if (selectedMode === "time") return "Делит equity curve каждого sweep point на 4 progress-куска. Хорошие строки зарабатывают не одним всплеском, а в нескольких частях прогона."
        if (selectedMode === "plateau") return "Ищет параметры, рядом с которыми соседние значения тоже хорошие. Это отсекает одиночные иголки, которые обычно не переживают live."
        if (selectedMode === "raw") return "Сырые таблицы после batch sweep: stable, max profit, symbols, pairs, params и skipped. Это быстрый drill-down без дополнительной агрегации."
        return "Сводка по последнему batch sweep: сколько пар и points прошло, сколько строк положительные, чистые, risk-stopped и какой средний PnL."
    }

    function rawMode() {
        return backtestVm.batchRawTableMode && backtestVm.batchRawTableMode.length > 0 ? backtestVm.batchRawTableMode : "stable"
    }

    function setRawMode(mode) {
        backtestVm.setBatchRawTableMode(mode)
        pnlChart.requestPaint()
    }

    function rawRows() {
        var mode = rawMode()
        if (mode === "stable") return backtestVm.batchStableRows
        if (mode === "profit") return backtestVm.batchProfitRows
        if (mode === "symbols") return backtestVm.batchSymbolRows
        if (mode === "pairs") return backtestVm.batchPairRows
        if (mode === "params") return backtestVm.batchParamRows
        return backtestVm.batchSkippedRows
    }

    function rowLabel(row) {
        var mode = rawMode()
        if (mode === "symbols" || mode === "pairs" || mode === "params") return row.label || row.key || ""
        if (mode === "skipped") return row.reason || row.status || "skipped"
        return row.symbol || row.label || ""
    }

    function rowPair(row) {
        var mode = rawMode()
        if (mode === "symbols" || mode === "pairs" || mode === "params") return row.rows ? String(row.rows) + " rows" : ""
        if (mode === "skipped") return row.exchangePair || row.exchange || ""
        return row.exchangePair || ""
    }

    function rowParams(row) {
        var mode = rawMode()
        if (mode === "symbols" || mode === "pairs" || mode === "params")
            return "ok " + (row.okRows || 0) + "/" + (row.rows || 0) + " pos " + (row.positivePct || 0) + "%"
        if (mode === "skipped") return row.market || row.path || ""
        return row.paramsLabel || ""
    }

    function rowPnl(row) {
        var mode = rawMode()
        if (mode === "symbols" || mode === "pairs" || mode === "params") return e8Text(row.bestPnlE8 || 0)
        if (mode === "skipped") return ""
        return e8Text(row.totalPnlE8 || row.total_pnl_e8 || 0)
    }

    function rowPnlRaw(row) {
        var mode = rawMode()
        if (mode === "skipped") return 0
        if (mode === "symbols" || mode === "pairs" || mode === "params") return Number(row.bestPnlE8 || 0)
        return Number(row.totalPnlE8 || row.total_pnl_e8 || 0)
    }

    function rowRisk(row) {
        var mode = rawMode()
        if (mode === "symbols" || mode === "pairs" || mode === "params")
            return "avg " + e8Text(row.avgPnlE8 || 0) + " dd " + e8Text(row.worstDrawdownE8 || 0)
        if (mode === "skipped") return row.sessionId || ""
        var flags = []
        if (row.riskStopped || row.risk_stopped) flags.push("risk")
        if (row.liquidated) flags.push("liq")
        return "dd " + e8Text(row.maxDrawdownE8 || row.max_drawdown_e8 || 0) + (flags.length > 0 ? " / " + flags.join(",") : "")
    }

    function matrixCell(rowExchange, columnExchange) {
        var cells = backtestVm.batchPairMatrixCells
        var key = rowExchange + "|" + columnExchange
        for (var i = 0; i < cells.length; ++i) {
            if (cells[i].cellKey === key)
                return cells[i]
        }
        return ({})
    }

    function syncControls() {
        universeBox.currentIndex = universeBox.indexOfValue(root.backtestVm.batchUniverseId)
    }

    background: Rectangle { color: root.windowColor }

    Connections {
        target: root.backtestVm
        function onSessionsChanged() { root.syncControls() }
        function onBatchConfigChanged() { root.syncControls(); pnlChart.requestPaint() }
        function onBatchResultsChanged() { pnlChart.requestPaint() }
    }

    component ActionButton: Rectangle {
        property string text: ""
        property bool enabledValue: true
        property bool selected: false
        property color accent: root.accentColor
        signal clicked()
        radius: 6
        implicitWidth: Math.max(78, label.implicitWidth + 18)
        implicitHeight: 30
        Layout.minimumWidth: implicitWidth
        Layout.preferredWidth: implicitWidth
        Layout.preferredHeight: implicitHeight
        color: enabledValue ? (selected ? root.panelAltColor : (mouse.containsMouse ? root.panelAltColor : root.panelColor)) : root.panelDeepColor
        border.color: enabledValue ? (selected ? accent : root.borderColor) : root.borderColor
        border.width: selected ? 2 : 1
        opacity: enabledValue ? 1.0 : 0.5
        Text { id: label; anchors.centerIn: parent; width: parent.width - 12; text: parent.text; color: enabledValue ? root.textColor : root.mutedTextColor; font.pixelSize: 11; font.bold: true; elide: Text.ElideRight; horizontalAlignment: Text.AlignHCenter }
        MouseArea { id: mouse; anchors.fill: parent; hoverEnabled: true; enabled: parent.enabledValue; onClicked: parent.clicked() }
    }

    component CompactField: Item {
        property string caption: ""
        property alias text: input.text
        property int fieldWidth: 92
        signal edited(string value)
        Layout.preferredWidth: fieldWidth
        Layout.minimumWidth: fieldWidth
        Layout.preferredHeight: 42
        ColumnLayout {
            anchors.fill: parent
            spacing: 2
            Label { text: caption; color: root.mutedTextColor; font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true }
            TextField {
                id: input
                Layout.fillWidth: true
                Layout.preferredHeight: 24
                color: root.textColor
                font.pixelSize: 12
                selectByMouse: true
                background: Rectangle { radius: 5; color: root.panelDeepColor; border.color: root.borderColor; border.width: 1 }
                onEditingFinished: edited(text)
            }
        }
    }

    component StatCard: Rectangle {
        required property var row
        Layout.fillWidth: true
        Layout.preferredHeight: 74
        radius: 6
        color: root.panelDeepColor
        border.color: row.tone === "good" ? root.goodColor : (row.tone === "bad" ? root.badColor : root.borderColor)
        border.width: 1
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 3
            Label { text: row.label || ""; color: root.mutedTextColor; font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true }
            Label { text: row.value || ""; color: root.textColor; font.pixelSize: 19; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
            Label { text: row.detail || ""; color: root.mutedTextColor; font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 112
            color: root.chromeColor
            border.color: root.borderColor
            border.width: 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8

                    RecorderComboBox {
                        id: universeBox
                        Layout.fillWidth: true
                        Layout.preferredWidth: 520
                        caption: "Universe"
                        textRole: "label"
                        valueRole: "id"
                        model: root.backtestVm.batchUniverseChoices
                        popupWidth: 720
                        onActivated: root.backtestVm.setBatchUniverseId(currentValue)
                        Component.onCompleted: root.syncControls()
                    }

                    RecorderComboBox {
                        id: strategyBox
                        Layout.preferredWidth: 280
                        caption: "Strategy"
                        textRole: "label"
                        valueRole: "id"
                        model: root.backtestVm.strategyChoices
                        popupWidth: 420
                        Component.onCompleted: currentIndex = indexOfValue(root.backtestVm.selectedStrategy)
                        onActivated: root.backtestVm.setSelectedStrategy(currentValue)
                    }

                    CompactField { caption: "Pair budget"; fieldWidth: 100; text: root.backtestVm.batchPairBudget; onEdited: function(value) { root.backtestVm.setBatchPairBudget(value) } }
                    CompactField { caption: "Sweep budget"; fieldWidth: 106; text: root.backtestVm.sweepBudget; onEdited: function(value) { root.backtestVm.setSweepBudget(value) } }
                    CompactField { caption: "Seed"; fieldWidth: 82; text: root.backtestVm.sweepSeed; onEdited: function(value) { root.backtestVm.setSweepSeed(value) } }

                    ActionButton { text: "Refresh"; onClicked: root.backtestVm.reloadSessions() }
                    ActionButton { text: root.backtestVm.running ? "Running" : "Start"; enabledValue: !root.backtestVm.running; accent: root.goodColor; onClicked: root.backtestVm.startBatchSweep() }
                    ActionButton { visible: root.backtestVm.running; text: "Cancel"; enabledValue: root.backtestVm.running; accent: root.badColor; onClicked: root.backtestVm.cancelBacktest() }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    ProgressBar { Layout.preferredWidth: 260; from: 0; to: 100; value: root.backtestVm.progressPercent }
                    Label { text: root.backtestVm.progressPercent + "%"; color: root.textColor; font.bold: true; font.pixelSize: 12; Layout.preferredWidth: 42 }
                    Label { text: root.backtestVm.progressText; color: root.mutedTextColor; font.pixelSize: 12; elide: Text.ElideRight; Layout.preferredWidth: 360 }
                    Label { text: root.backtestVm.batchSummaryText || root.backtestVm.statusText; color: root.mutedTextColor; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 46
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            color: root.panelColor
            border.color: root.borderColor
            radius: 6
            RowLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 8
                Repeater {
                    model: root.modeChoices
                    delegate: ActionButton {
                        required property var modelData
                        text: modelData.label
                        selected: root.selectedMode === modelData.id
                        accent: root.goodColor
                        onClicked: root.selectedMode = modelData.id
                    }
                }
                Item { Layout.fillWidth: true }
                Label { text: root.backtestVm.batchRunId; color: root.mutedTextColor; font.pixelSize: 11; elide: Text.ElideLeft; Layout.maximumWidth: 360 }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 54
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            color: root.panelDeepColor
            border.color: root.borderColor
            radius: 6
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 2
                Label { text: root.modeTitle(); color: root.textColor; font.pixelSize: 13; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                Label { text: root.modeDescription(); color: root.mutedTextColor; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.bottomMargin: 10
            currentIndex: root.modeIndex()

            Rectangle {
                color: root.panelColor
                border.color: root.borderColor
                radius: 6
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 10
                    GridLayout {
                        Layout.fillWidth: true
                        columns: 6
                        rowSpacing: 8
                        columnSpacing: 8
                        Repeater {
                            model: root.backtestVm.batchSummaryCards
                            delegate: StatCard { required property var modelData; row: modelData }
                        }
                    }
                    Label { text: "Stable leaders"; color: root.textColor; font.pixelSize: 12; font.bold: true }
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: root.backtestVm.batchStableRows
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            width: ListView.view.width
                            height: 38
                            color: index % 2 === 0 ? root.panelDeepColor : "#181b21"
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                spacing: 10
                                Label { text: String(index + 1); color: root.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 32 }
                                Label { text: modelData.symbol || ""; color: root.textColor; font.bold: true; font.pixelSize: 12; elide: Text.ElideRight; Layout.preferredWidth: 120 }
                                Label { text: modelData.exchangePair || ""; color: root.mutedTextColor; font.pixelSize: 11; elide: Text.ElideRight; Layout.preferredWidth: 160 }
                                Label { text: modelData.paramsLabel || ""; color: root.mutedTextColor; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
                                Label { text: root.e8Text(modelData.totalPnlE8 || modelData.total_pnl_e8 || 0); color: Number(modelData.totalPnlE8 || modelData.total_pnl_e8 || 0) < 0 ? root.badColor : root.goodColor; font.pixelSize: 12; font.bold: true; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 90 }
                            }
                        }
                    }
                }
            }

            Rectangle {
                color: root.panelColor
                border.color: root.borderColor
                radius: 6
                Flickable {
                    anchors.fill: parent
                    anchors.margins: 10
                    contentWidth: matrixColumn.implicitWidth
                    contentHeight: matrixColumn.implicitHeight
                    clip: true
                    ColumnLayout {
                        id: matrixColumn
                        spacing: 6
                        RowLayout {
                            spacing: 6
                            Rectangle { Layout.preferredWidth: 116; Layout.preferredHeight: 34; color: root.panelDeepColor; border.color: root.borderColor; Label { anchors.centerIn: parent; text: "exchange"; color: root.mutedTextColor; font.pixelSize: 11 } }
                            Repeater {
                                model: root.backtestVm.batchPairMatrixColumns
                                delegate: Rectangle {
                                    required property var modelData
                                    Layout.preferredWidth: 124
                                    Layout.preferredHeight: 34
                                    color: root.panelDeepColor
                                    border.color: root.borderColor
                                    Label { anchors.centerIn: parent; text: modelData.label || ""; color: root.textColor; font.pixelSize: 11; font.bold: true; elide: Text.ElideRight; width: parent.width - 8; horizontalAlignment: Text.AlignHCenter }
                                }
                            }
                        }
                        Repeater {
                            model: root.backtestVm.batchPairMatrixColumns
                            delegate: RowLayout {
                                required property var modelData
                                property string rowExchange: modelData.id
                                spacing: 6
                                Rectangle { Layout.preferredWidth: 116; Layout.preferredHeight: 62; color: root.panelDeepColor; border.color: root.borderColor; Label { anchors.centerIn: parent; width: parent.width - 8; text: modelData.label || ""; color: root.textColor; font.pixelSize: 11; font.bold: true; elide: Text.ElideRight; horizontalAlignment: Text.AlignHCenter } }
                                Repeater {
                                    model: root.backtestVm.batchPairMatrixColumns
                                    delegate: Rectangle {
                                        required property var modelData
                                        property var cell: root.matrixCell(rowExchange, modelData.id)
                                        Layout.preferredWidth: 124
                                        Layout.preferredHeight: 62
                                        color: rowExchange === modelData.id ? root.panelDeepColor : root.panelAltColor
                                        border.color: cell.rows > 0 ? (cell.positivePct >= 50 ? root.goodColor : root.badColor) : root.borderColor
                                        radius: 4
                                        Column {
                                            anchors.fill: parent
                                            anchors.margins: 6
                                            spacing: 2
                                            Label { text: cell.rows > 0 ? root.e8Text(cell.avgPnlE8 || 0) : "-"; color: cell.avgPnlE8 < 0 ? root.badColor : root.goodColor; font.pixelSize: 12; font.bold: true; elide: Text.ElideRight; width: parent.width }
                                            Label { text: cell.rows > 0 ? "pos " + (cell.positivePct || 0) + "% rows " + (cell.rows || 0) : ""; color: root.mutedTextColor; font.pixelSize: 10; elide: Text.ElideRight; width: parent.width }
                                            Label { text: cell.rows > 0 ? "fills " + (cell.fills || 0) : ""; color: root.mutedTextColor; font.pixelSize: 10; elide: Text.ElideRight; width: parent.width }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }

            Rectangle {
                color: root.panelColor
                border.color: root.borderColor
                radius: 6
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "#"; color: root.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 34 }
                        Label { text: "Symbol"; color: root.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 110 }
                        Label { text: "Pair"; color: root.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 150 }
                        Label { text: "Chunks"; color: root.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 260 }
                        Label { text: "Params"; color: root.mutedTextColor; font.pixelSize: 11; Layout.fillWidth: true }
                        Label { text: "Total"; color: root.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 90; horizontalAlignment: Text.AlignRight }
                    }
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: root.backtestVm.batchTimeRows
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            width: ListView.view.width
                            height: 46
                            color: index % 2 === 0 ? root.panelDeepColor : "#181b21"
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                spacing: 10
                                Label { text: String(index + 1); color: root.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 34 }
                                Label { text: modelData.symbol || ""; color: root.textColor; font.bold: true; font.pixelSize: 12; elide: Text.ElideRight; Layout.preferredWidth: 110 }
                                Label { text: modelData.exchangePair || ""; color: root.mutedTextColor; font.pixelSize: 11; elide: Text.ElideRight; Layout.preferredWidth: 150 }
                                RowLayout {
                                    Layout.preferredWidth: 260
                                    spacing: 4
                                    Repeater {
                                        model: [modelData.chunk0E8 || 0, modelData.chunk1E8 || 0, modelData.chunk2E8 || 0, modelData.chunk3E8 || 0]
                                        delegate: Rectangle {
                                            required property var modelData
                                            Layout.preferredWidth: 60
                                            Layout.preferredHeight: 24
                                            radius: 4
                                            color: Number(modelData) < 0 ? Qt.rgba(root.badColor.r, root.badColor.g, root.badColor.b, 0.2) : Qt.rgba(root.goodColor.r, root.goodColor.g, root.goodColor.b, 0.2)
                                            border.color: Number(modelData) < 0 ? root.badColor : root.goodColor
                                            Label { anchors.centerIn: parent; text: root.e8Text(modelData); color: Number(modelData) < 0 ? root.badColor : root.goodColor; font.pixelSize: 10; elide: Text.ElideRight; width: parent.width - 4; horizontalAlignment: Text.AlignHCenter }
                                        }
                                    }
                                }
                                Label { text: (modelData.stabilityText || "") + "  " + (modelData.paramsLabel || ""); color: root.mutedTextColor; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
                                Label { text: root.e8Text(modelData.totalPnlE8 || modelData.total_pnl_e8 || 0); color: Number(modelData.totalPnlE8 || modelData.total_pnl_e8 || 0) < 0 ? root.badColor : root.goodColor; font.bold: true; font.pixelSize: 12; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 90 }
                            }
                        }
                    }
                }
            }

            Rectangle {
                color: root.panelColor
                border.color: root.borderColor
                radius: 6
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "#"; color: root.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 34 }
                        Label { text: "Symbol"; color: root.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 110 }
                        Label { text: "Pair"; color: root.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 150 }
                        Label { text: "Params"; color: root.mutedTextColor; font.pixelSize: 11; Layout.fillWidth: true }
                        Label { text: "Neighbors"; color: root.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 130 }
                        Label { text: "PnL"; color: root.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 90; horizontalAlignment: Text.AlignRight }
                    }
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: root.backtestVm.batchPlateauRows
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            width: ListView.view.width
                            height: 42
                            color: index % 2 === 0 ? root.panelDeepColor : "#181b21"
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                spacing: 10
                                Label { text: String(index + 1); color: root.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 34 }
                                Label { text: modelData.symbol || ""; color: root.textColor; font.bold: true; font.pixelSize: 12; elide: Text.ElideRight; Layout.preferredWidth: 110 }
                                Label { text: modelData.exchangePair || ""; color: root.mutedTextColor; font.pixelSize: 11; elide: Text.ElideRight; Layout.preferredWidth: 150 }
                                Label { text: modelData.paramsLabel || ""; color: root.mutedTextColor; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
                                Label { text: (modelData.positiveNeighbors || 0) + "/" + (modelData.neighbors || 0) + " conf " + (modelData.confidence || 0) + "%"; color: (modelData.positiveNeighbors || 0) > 0 ? root.goodColor : root.mutedTextColor; font.pixelSize: 11; elide: Text.ElideRight; Layout.preferredWidth: 130 }
                                Label { text: root.e8Text(modelData.totalPnlE8 || modelData.total_pnl_e8 || 0); color: Number(modelData.totalPnlE8 || modelData.total_pnl_e8 || 0) < 0 ? root.badColor : root.goodColor; font.bold: true; font.pixelSize: 12; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 90 }
                            }
                        }
                    }
                }
            }

            Rectangle {
                color: root.panelColor
                border.color: root.borderColor
                radius: 6
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 6
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        Repeater {
                            model: root.rawModeChoices
                            delegate: ActionButton {
                                required property var modelData
                                text: modelData.label
                                selected: root.rawMode() === modelData.id
                                accent: root.goodColor
                                onClicked: root.setRawMode(modelData.id)
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }
                    Canvas {
                        id: pnlChart
                        Layout.fillWidth: true
                        Layout.preferredHeight: 112
                        onWidthChanged: requestPaint()
                        onHeightChanged: requestPaint()
                        onPaint: {
                            var ctx = getContext("2d")
                            ctx.clearRect(0, 0, width, height)
                            ctx.fillStyle = root.panelDeepColor
                            ctx.fillRect(0, 0, width, height)
                            var rows = root.rawRows()
                            var count = Math.min(rows.length, 48)
                            var maxAbs = 1
                            for (var i = 0; i < count; ++i)
                                maxAbs = Math.max(maxAbs, Math.abs(root.rowPnlRaw(rows[i])))
                            var zeroY = Math.round(height / 2)
                            ctx.strokeStyle = root.borderColor
                            ctx.beginPath()
                            ctx.moveTo(0, zeroY)
                            ctx.lineTo(width, zeroY)
                            ctx.stroke()
                            if (count === 0)
                                return
                            var gap = 2
                            var barWidth = Math.max(3, (width - gap * (count - 1)) / count)
                            for (var b = 0; b < count; ++b) {
                                var value = root.rowPnlRaw(rows[b])
                                var barHeight = Math.max(1, Math.abs(value) / maxAbs * (height / 2 - 8))
                                var x = b * (barWidth + gap)
                                var y = value >= 0 ? zeroY - barHeight : zeroY
                                ctx.fillStyle = value >= 0 ? root.goodColor : root.badColor
                                ctx.fillRect(x, y, barWidth, barHeight)
                            }
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        Label { text: "#"; color: root.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 34 }
                        Label { text: "Name"; color: root.mutedTextColor; font.pixelSize: 11; Layout.fillWidth: true }
                        Label { text: "Pair/Rows"; color: root.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 160 }
                        Label { text: "Params/Rate"; color: root.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 270 }
                        Label { text: "PnL"; color: root.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 94; horizontalAlignment: Text.AlignRight }
                        Label { text: "Risk"; color: root.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 180 }
                    }
                    ListView {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        model: root.rawRows()
                        delegate: Rectangle {
                            required property var modelData
                            required property int index
                            width: ListView.view.width
                            height: 44
                            radius: 5
                            color: index % 2 === 0 ? root.panelDeepColor : "#181b21"
                            border.color: root.borderColor
                            border.width: 1
                            RowLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 8
                                anchors.rightMargin: 8
                                spacing: 10
                                Label { text: String(index + 1); color: root.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 34 }
                                Label { text: root.rowLabel(modelData); color: root.textColor; font.pixelSize: 12; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                                Label { text: root.rowPair(modelData); color: root.mutedTextColor; font.pixelSize: 11; elide: Text.ElideRight; Layout.preferredWidth: 160 }
                                Label { text: root.rowParams(modelData); color: root.mutedTextColor; font.pixelSize: 11; elide: Text.ElideRight; Layout.preferredWidth: 270 }
                                Label { text: root.rowPnl(modelData); color: Number(modelData.totalPnlE8 || modelData.bestPnlE8 || modelData.total_pnl_e8 || 0) < 0 ? root.badColor : root.goodColor; font.pixelSize: 12; font.bold: true; horizontalAlignment: Text.AlignRight; elide: Text.ElideRight; Layout.preferredWidth: 94 }
                                Label { text: root.rowRisk(modelData); color: root.mutedTextColor; font.pixelSize: 11; elide: Text.ElideRight; Layout.preferredWidth: 180 }
                            }
                        }
                    }
                }
            }
        }
    }
}
