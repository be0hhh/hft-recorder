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
    property color borderColor: "#343844"
    property color textColor: "#f1f4f8"
    property color mutedTextColor: "#a8afbd"
    property color accentColor: "#24c2cb"
    property color goodColor: "#82d46b"
    property color badColor: "#ef6f6c"
    property int selectedSweepPointId: -1
    property bool sweepPercentMode: false
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

    function syncSelections() {
        sessionBox.currentIndex = sessionBox.indexOfValue(root.backtestVm.selectedSessionId)
        strategyBox.currentIndex = strategyBox.indexOfValue(root.backtestVm.selectedStrategy)
        configModeBox.currentIndex = configModeBox.indexOfValue(root.backtestVm.configMode)
        indicatorBox.currentIndex = indicatorBox.indexOfValue(root.backtestVm.selectedIndicatorProfile)
    }

    background: Rectangle { color: root.windowColor }

    Connections {
        target: root.backtestVm
        function onSessionsChanged() { root.syncSelections() }
        function onSelectedSessionChanged() { root.syncSelections() }
        function onSymbolChanged() { symbolField.text = root.backtestVm.selectedSymbol }
        function onSelectedStrategyChanged() { root.syncSelections() }
        function onConfigChanged() { root.syncSelections() }
        function onIndicatorProfileChanged() { root.syncSelections() }
        function onSelectionChanged() { root.selectedSweepPointId = -1; root.sweepPercentMode = false }
    }

    component ActionButton: Rectangle {
        property string text: ""
        property bool enabledValue: true
        property color accent: root.accentColor
        signal clicked()
        radius: 6
        implicitWidth: Math.max(76, label.implicitWidth + 18)
        implicitHeight: 30
        Layout.minimumWidth: implicitWidth
        Layout.preferredWidth: implicitWidth
        Layout.preferredHeight: implicitHeight
        color: enabledValue ? (mouse.containsMouse ? "#2b303a" : root.panelColor) : root.panelDeepColor
        border.color: enabledValue ? accent : root.borderColor
        border.width: 1
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

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: headerLayout.implicitHeight + 16
            Layout.minimumHeight: headerLayout.implicitHeight + 16
            color: root.chromeColor
            border.color: root.borderColor
            border.width: 1

            ColumnLayout {
                id: headerLayout
                anchors.fill: parent
                anchors.margins: 8
                spacing: 6

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    RecorderComboBox {
                        id: sessionBox
                        Layout.fillWidth: true
                        Layout.preferredWidth: 520
                        caption: "Session"
                        textRole: "label"
                        valueRole: "id"
                        model: root.backtestVm.sessions
                        popupWidth: 720
                        onActivated: root.backtestVm.setSelectedSessionId(currentValue)
                        Component.onCompleted: root.syncSelections()
                    }
                    CompactField {
                        id: symbolField
                        caption: "Symbol"
                        fieldWidth: 120
                        text: root.backtestVm.selectedSymbol
                        onEdited: function(value) { root.backtestVm.selectedSymbol = value }
                    }
                    RecorderComboBox {
                        id: strategyBox
                        Layout.fillWidth: true
                        Layout.preferredWidth: 300
                        caption: "Strategy"
                        textRole: "label"
                        valueRole: "id"
                        model: root.backtestVm.strategyChoices
                        popupWidth: 420
                        popupAlignRight: true
                        onActivated: root.backtestVm.setSelectedStrategy(currentValue)
                        Component.onCompleted: root.syncSelections()
                    }
                    RecorderComboBox {
                        id: configModeBox
                        Layout.preferredWidth: 132
                        caption: "Config"
                        textRole: "label"
                        valueRole: "id"
                        model: root.backtestVm.configModeChoices
                        popupWidth: 150
                        enabled: count > 1
                        opacity: enabled ? 1.0 : 0.55
                        onActivated: root.backtestVm.setConfigMode(currentValue)
                        Component.onCompleted: root.syncSelections()
                    }
                    RecorderComboBox {
                        id: indicatorBox
                        Layout.preferredWidth: 170
                        caption: "Indicator"
                        textRole: "label"
                        valueRole: "id"
                        model: root.backtestVm.indicatorProfileChoices
                        popupWidth: 190
                        visible: count > 0
                        enabled: count > 0
                        onActivated: root.backtestVm.setSelectedIndicatorProfile(currentValue)
                        Component.onCompleted: root.syncSelections()
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    CompactField {
                        caption: "Balance USDT"
                        fieldWidth: 126
                        text: root.backtestVm.initialBalanceUsdt
                        onEdited: function(value) { root.backtestVm.initialBalanceUsdt = value }
                    }
                    CompactField {
                        caption: "Maker fee bps"
                        fieldWidth: 116
                        text: root.backtestVm.makerFeeBps
                        onEdited: function(value) { root.backtestVm.makerFeeBps = value }
                    }
                    CompactField {
                        caption: "Taker fee bps"
                        fieldWidth: 116
                        text: root.backtestVm.takerFeeBps
                        onEdited: function(value) { root.backtestVm.takerFeeBps = value }
                    }
                    CompactField {
                        caption: "Sweep budget"
                        fieldWidth: 106
                        text: root.backtestVm.sweepBudget
                        onEdited: function(value) { root.backtestVm.sweepBudget = value }
                    }
                    CompactField {
                        caption: "Sweep seed"
                        fieldWidth: 92
                        text: root.backtestVm.sweepSeed
                        onEdited: function(value) { root.backtestVm.sweepSeed = value }
                    }
                    Item { Layout.fillWidth: true }
                    ActionButton { text: "Refresh"; onClicked: { root.backtestVm.reloadSessions(); root.backtestVm.refreshResults() } }
                    ActionButton { text: root.backtestVm.running ? "Running" : "Start"; enabledValue: root.backtestVm.canRun; accent: root.goodColor; onClicked: root.backtestVm.startBacktest() }
                    ActionButton { text: "Start sweep"; enabledValue: root.backtestVm.canRun; accent: root.accentColor; onClicked: root.backtestVm.startSweep() }
                    ActionButton { visible: root.backtestVm.running; text: "Cancel"; enabledValue: root.backtestVm.running; accent: root.badColor; onClicked: root.backtestVm.cancelBacktest() }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    CompactField {
                        caption: "MD base us"
                        fieldWidth: 92
                        text: root.backtestVm.marketDataLatencyUs
                        onEdited: function(value) { root.backtestVm.marketDataLatencyUs = value }
                    }
                    CompactField {
                        caption: "MD jitter us"
                        fieldWidth: 96
                        text: root.backtestVm.marketDataJitterUs
                        onEdited: function(value) { root.backtestVm.marketDataJitterUs = value }
                    }
                    CompactField {
                        caption: "Mkt base us"
                        fieldWidth: 98
                        text: root.backtestVm.marketOrderLatencyUs
                        onEdited: function(value) { root.backtestVm.marketOrderLatencyUs = value }
                    }
                    CompactField {
                        caption: "Mkt jitter us"
                        fieldWidth: 102
                        text: root.backtestVm.marketOrderJitterUs
                        onEdited: function(value) { root.backtestVm.marketOrderJitterUs = value }
                    }
                    CompactField {
                        caption: "Limit base us"
                        fieldWidth: 108
                        text: root.backtestVm.limitOrderLatencyUs
                        onEdited: function(value) { root.backtestVm.limitOrderLatencyUs = value }
                    }
                    CompactField {
                        caption: "Limit jitter us"
                        fieldWidth: 112
                        text: root.backtestVm.limitOrderJitterUs
                        onEdited: function(value) { root.backtestVm.limitOrderJitterUs = value }
                    }
                    CompactField {
                        caption: "Seed"
                        fieldWidth: 84
                        text: root.backtestVm.latencySeed
                        onEdited: function(value) { root.backtestVm.latencySeed = value }
                    }
                    Item { Layout.fillWidth: true }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    ProgressBar { Layout.preferredWidth: 220; from: 0; to: 100; value: root.backtestVm.progressPercent }
                    Label { text: root.backtestVm.progressPercent + "%"; color: root.textColor; font.bold: true; font.pixelSize: 12; Layout.preferredWidth: 42 }
                    Label { text: root.backtestVm.progressText; color: root.mutedTextColor; font.pixelSize: 12; elide: Text.ElideRight; Layout.preferredWidth: 260 }
                    Label { text: root.backtestVm.statusText; color: root.mutedTextColor; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true }
                }
            }
        }

        Rectangle {
            property int parameterCount: root.backtestVm.strategyParameters ? root.backtestVm.strategyParameters.length : 0
            property int parameterColumns: Math.max(1, Math.floor((width - 16 + 10) / (292 + 10)))
            property int parameterRows: Math.ceil(parameterCount / parameterColumns)
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(108, parameterRows * 92 + Math.max(0, parameterRows - 1) * 10 + 16)
            Layout.minimumHeight: Layout.preferredHeight
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            color: root.panelColor
            border.color: root.borderColor
            radius: 6

            Item {
                anchors.fill: parent
                anchors.margins: 8

                Flow {
                    id: paramsFlow
                    width: Math.max(1, parent.width)
                    spacing: 10

                    Repeater {
                        model: root.backtestVm.strategyParameters
                        delegate: Item {
                            required property var modelData
                            property bool choiceRow: modelData.isChoice === true
                            property bool fixedRow: modelData.mode === "fixed"
                            width: 292
                            height: choiceRow || fixedRow ? 62 : 92

                            Label {
                                x: 0
                                y: 0
                                width: parent.width
                                height: 18
                                text: modelData.label
                                color: root.textColor
                                font.pixelSize: 11
                                font.bold: true
                                elide: Text.ElideRight
                            }

                            RecorderComboBox {
                                visible: choiceRow
                                x: 0
                                y: 24
                                width: parent.width
                                height: 28
                                caption: ""
                                textRole: "label"
                                valueRole: "id"
                                model: modelData.choices || []
                                popupWidth: 170
                                Component.onCompleted: currentIndex = indexOfValue(modelData.value)
                                onActivated: root.backtestVm.setStrategyParameterGroup(modelData.group, currentValue)
                            }

                            RecorderComboBox {
                                visible: !choiceRow
                                x: 0
                                y: 24
                                width: 110
                                height: 28
                                caption: ""
                                textRole: "label"
                                valueRole: "id"
                                model: modelData.modeChoices || []
                                popupWidth: 120
                                Component.onCompleted: currentIndex = indexOfValue(modelData.mode)
                                onActivated: root.backtestVm.setStrategyParameterMode(modelData.key, currentValue)
                            }

                            TextField {
                                visible: !choiceRow && fixedRow
                                x: 118
                                y: 24
                                width: 112
                                height: 26
                                text: modelData.value
                                selectByMouse: true
                                color: root.textColor
                                font.pixelSize: 12
                                background: Rectangle { color: root.panelDeepColor; border.color: root.borderColor; radius: 5 }
                                onEditingFinished: root.backtestVm.setStrategyParameter(modelData.key, text)
                            }

                            TextField {
                                id: minField
                                visible: !choiceRow && !fixedRow
                                x: 0
                                y: 56
                                width: 88
                                height: 24
                                placeholderText: "min"
                                text: modelData.min
                                selectByMouse: true
                                color: root.textColor
                                font.pixelSize: 11
                                background: Rectangle { color: root.panelDeepColor; border.color: root.borderColor; radius: 5 }
                                onEditingFinished: root.backtestVm.setStrategyParameterRange(modelData.key, text, maxField.text, stepField.text)
                            }

                            TextField {
                                id: maxField
                                visible: !choiceRow && !fixedRow
                                x: 96
                                y: 56
                                width: 88
                                height: 24
                                placeholderText: "max"
                                text: modelData.max
                                selectByMouse: true
                                color: root.textColor
                                font.pixelSize: 11
                                background: Rectangle { color: root.panelDeepColor; border.color: root.borderColor; radius: 5 }
                                onEditingFinished: root.backtestVm.setStrategyParameterRange(modelData.key, minField.text, text, stepField.text)
                            }

                            TextField {
                                id: stepField
                                visible: !choiceRow && !fixedRow
                                x: 192
                                y: 56
                                width: 88
                                height: 24
                                placeholderText: "step"
                                text: modelData.step
                                selectByMouse: true
                                color: root.textColor
                                font.pixelSize: 11
                                background: Rectangle { color: root.panelDeepColor; border.color: root.borderColor; radius: 5 }
                                onEditingFinished: root.backtestVm.setStrategyParameterRange(modelData.key, minField.text, maxField.text, text)
                            }
                        }
                    }
                }
            }
        }
        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.bottomMargin: 10
            orientation: Qt.Horizontal

            Rectangle {
                SplitView.preferredWidth: 360
                SplitView.minimumWidth: 260
                color: root.panelColor
                border.color: root.borderColor
                radius: 6

                ListView {
                    anchors.fill: parent
                    anchors.margins: 8
                    clip: true
                    model: root.backtestVm.runs
                    delegate: Rectangle {
                        required property var modelData
                        width: ListView.view.width
                        height: 64
                        radius: 5
                        color: modelData.runId === root.backtestVm.selectedRunId ? Qt.rgba(root.accentColor.r, root.accentColor.g, root.accentColor.b, 0.16) : root.panelDeepColor
                        border.color: modelData.runId === root.backtestVm.selectedRunId ? root.accentColor : root.borderColor
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            spacing: 8
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Label { text: modelData.label; color: root.textColor; font.pixelSize: 12; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                                Label { text: modelData.status + " / " + modelData.modifiedText; color: root.mutedTextColor; font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true }
                                Label { text: modelData.configText || modelData.runId; color: root.mutedTextColor; font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true }
                            }
                            Label {
                                Layout.preferredWidth: 58
                                text: modelData.pnlText || ""
                                visible: text.length > 0
                                color: modelData.pnlNegative ? root.badColor : root.accentColor
                                font.pixelSize: 12
                                font.bold: true
                                horizontalAlignment: Text.AlignRight
                                verticalAlignment: Text.AlignVCenter
                            }
                        }
                        MouseArea { anchors.fill: parent; onClicked: root.backtestVm.selectRun(modelData.runId) }
                    }
                }
            }

            Rectangle {
                SplitView.fillWidth: true
                color: root.panelColor
                border.color: root.borderColor
                radius: 6

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8
                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: root.backtestVm.selectedIsSweep ? "Sweep" : "Summary"; color: root.textColor; font.pixelSize: 15; font.bold: true; Layout.fillWidth: true }
                        RecorderComboBox {
                            visible: root.backtestVm.selectedIsSweep
                            Layout.preferredWidth: 132
                            caption: "Curves"
                            textRole: "label"
                            valueRole: "id"
                            model: root.backtestVm.sweepCurveLimitChoices
                            popupWidth: 140
                            Component.onCompleted: currentIndex = indexOfValue(root.backtestVm.selectedSweepCurveLimit)
                            onActivated: root.backtestVm.setSelectedSweepCurveLimit(currentValue)
                        }
                        ActionButton { text: root.sweepPercentMode ? "PnL %" : "PnL $"; visible: root.backtestVm.selectedIsSweep; enabledValue: root.backtestVm.selectedInitialBalanceE8 > 0; onClicked: { root.sweepPercentMode = !root.sweepPercentMode; sweepCanvas.requestPaint(); sweepHoverCanvas.requestPaint() } }
                        ActionButton { text: "Apply"; visible: root.backtestVm.selectedIsSweep; enabledValue: root.selectedSweepPointId >= 0 && !root.backtestVm.running; onClicked: root.backtestVm.applySweepPointById(root.selectedSweepPointId) }
                        ActionButton { text: "Detailed run"; visible: root.backtestVm.selectedIsSweep; enabledValue: root.selectedSweepPointId >= 0 && !root.backtestVm.running; accent: root.goodColor; onClicked: root.backtestVm.startDetailedRunFromSweepPointById(root.selectedSweepPointId) }
                        ActionButton { text: "Delete"; visible: root.backtestVm.hasSelection; enabledValue: root.backtestVm.hasSelection && !root.backtestVm.running; accent: root.badColor; onClicked: root.backtestVm.deleteSelectedRun() }
                        Label { text: root.backtestVm.selectedErrorText; visible: text !== ""; color: root.badColor; font.pixelSize: 11; elide: Text.ElideRight; Layout.maximumWidth: 360 }
                    }

                    ColumnLayout {
                        visible: !root.backtestVm.selectedIsSweep
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 8
                        TextArea {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.preferredHeight: 320
                            text: root.backtestVm.selectedSummaryJson
                            readOnly: true
                            selectByMouse: true
                            wrapMode: TextEdit.NoWrap
                            color: root.textColor
                            font.family: "monospace"
                            font.pixelSize: 11
                            background: Rectangle { color: root.panelDeepColor; border.color: root.borderColor; radius: 5 }
                        }
                    }

                    ColumnLayout {
                        visible: root.backtestVm.selectedIsSweep
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 8

                        Rectangle {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: root.panelDeepColor
                            border.color: root.borderColor
                            radius: 6
                            clip: true

                            Canvas {
                                id: sweepCanvas
                                anchors.fill: parent
                                anchors.margins: 12
                                property int hoverPointId: -1
                                property int hoverCurveIndex: -1
                                property int hoverStep: -1
                                property real hoverX: 0
                                property real hoverY: 0
                                property var sweepCurves: root.backtestVm.selectedSweepCurves
                                property string sweepStateKey: ""
                                property int sweepSteps: 1
                                property var sweepBounds: ({ "min": 0, "max": 1 })

                                Connections {
                                    target: root.backtestVm
                                    function onSelectionChanged() { sweepCanvas.clearSweepState(); sweepCanvas.clearHover(); sweepCanvas.requestPaint(); sweepHoverCanvas.requestPaint() }
                                }
                                onWidthChanged: requestPaint()
                                onHeightChanged: requestPaint()

                                function maxSteps(curves) {
                                    var max = 1
                                    for (var i = 0; i < curves.length; ++i) max = Math.max(max, curves[i].curve.length)
                                    return max
                                }

                                function curveValue(curve, step) {
                                    if (curve.curve.length === 0) return 0
                                    var idx = Math.min(step, curve.curve.length - 1)
                                    return root.sweepValue(curve.curve[idx], curve.initialBalanceE8)
                                }

                                function paddedBounds(curves, steps) {
                                    var minPnl = 0
                                    var maxPnl = 0
                                    var has = false
                                    for (var c = 0; c < curves.length; ++c) {
                                        for (var s = 0; s < steps; ++s) {
                                            var value = curveValue(curves[c], s)
                                            if (!has) { minPnl = value; maxPnl = value; has = true }
                                            minPnl = Math.min(minPnl, value)
                                            maxPnl = Math.max(maxPnl, value)
                                        }
                                    }
                                    minPnl = Math.min(minPnl, 0)
                                    maxPnl = Math.max(maxPnl, 0)
                                    var span = maxPnl - minPnl
                                    if (span <= 0) span = root.sweepPercentMode ? 2.0 : 200000000
                                    var pad = Math.max(root.sweepPercentMode ? 0.01 : 1000000, span * 0.08)
                                    return { min: minPnl - pad, max: maxPnl + pad }
                                }

                                function sweepStateCacheKey(curves) {
                                    var key = root.backtestVm.selectedRunId + ":" + root.backtestVm.selectedSweepCurveLimit + ":" + root.sweepPercentMode + ":" + curves.length
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
                                    sweepSteps = maxSteps(curves)
                                    sweepBounds = paddedBounds(curves, sweepSteps)
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
                                    return root.sweepPalette[index % root.sweepPalette.length]
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
                                        ctx.fillStyle = root.mutedTextColor
                                        ctx.fillText(root.sweepPercentMode ? value.toFixed(2) + "%" : root.e8Text(value), plotX - 8, y)
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
                                    ctx.strokeStyle = colorFor(index)
                                    ctx.globalAlpha = root.selectedSweepPointId < 0 || root.selectedSweepPointId === curve.pointId ? 0.95 : 0.35
                                    ctx.lineWidth = root.selectedSweepPointId === curve.pointId ? 3 : 1.7
                                    ctx.beginPath()
                                    for (var step = 0; step < steps; ++step) {
                                        var x = xFor(step, steps, plotX, plotW)
                                        var y = yFor(curveValue(curve, step), bounds.min, bounds.max, plotY, plotH)
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
                                        for (var s = firstStep; s <= lastStep; ++s) {
                                            var x = xFor(s, steps, plotX, plotW)
                                            var y = yFor(curveValue(curves[c], s), bounds.min, bounds.max, plotY, plotH)
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
                                    hoverY = yFor(curveValue(curves[bestCurve], bestStep), bounds.min, bounds.max, plotY, plotH)
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
                                    root.selectedSweepPointId = hoverPointId
                                    root.backtestVm.applySweepPointById(hoverPointId)
                                    requestPaint()
                                    sweepHoverCanvas.requestPaint()
                                }

                                function drawHover(ctx, curves, steps, bounds, plotX, plotY, plotW, plotH) {
                                    if (hoverCurveIndex < 0 || hoverCurveIndex >= curves.length) return
                                    var curve = curves[hoverCurveIndex]
                                    var value = curveValue(curve, hoverStep)
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
                                    ctx.fillStyle = root.textColor
                                    ctx.fillText("#" + curve.pointId + " step " + (hoverStep + 1), cardX + 10, cardY + 8)
                                    ctx.fillStyle = Number(curve.totalPnlE8) < 0 ? root.badColor : root.goodColor
                                    ctx.fillText("Total PnL " + root.sweepText(value, curve.initialBalanceE8), cardX + 10, cardY + 28)
                                    ctx.fillStyle = root.mutedTextColor
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
                                    ctx.fillStyle = root.mutedTextColor
                                    ctx.font = "11px sans-serif"
                                    ctx.textAlign = "center"
                                    ctx.fillText("fills / equity steps", plotX + plotW / 2, plotY + plotH + 16)
                                }
                            }

                            Canvas {
                                id: sweepHoverCanvas
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

                            Label {
                                anchors.centerIn: parent
                                visible: sweepCanvas.sweepCurves.length === 0
                                text: "No sweep curves"
                                color: root.mutedTextColor
                                font.pixelSize: 14
                            }
                        }
                    }
                }
            }
        }
    }
}
