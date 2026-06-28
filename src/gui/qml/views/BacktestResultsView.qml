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

    function rebuildSecondarySessionRows() {
        var rows = [{ "id": "", "label": "No second leg", "rightText": "" }]
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
        root.rebuildSecondarySessionRows()
        sessionBox.currentIndex = sessionBox.indexOfValue(root.backtestVm.selectedSessionId)
        secondarySessionBox.currentIndex = secondarySessionBox.indexOfValue(root.firstExtraSessionId())
        strategyBox.currentIndex = strategyBox.indexOfValue(root.backtestVm.selectedStrategy)
        configModeBox.currentIndex = configModeBox.indexOfValue(root.backtestVm.configMode)
        indicatorBox.currentIndex = indicatorBox.indexOfValue(root.backtestVm.selectedIndicatorProfile)
    }

    background: Rectangle { color: root.windowColor }

    Connections {
        target: root.backtestVm
        function onSessionsChanged() { root.syncSelections() }
        function onSelectedSessionChanged() { root.syncSelections() }
        function onMultiSessionChanged() { root.syncSelections() }
        function onSymbolChanged() { symbolField.text = root.backtestVm.selectedSymbol }
        function onSelectedStrategyChanged() { root.syncSelections() }
        function onConfigChanged() { root.syncSelections() }
        function onIndicatorProfileChanged() { root.syncSelections() }
        function onSelectionChanged() { root.selectedSweepPointId = -1; root.sweepPercentMode = false; root.showRawSummary = false }
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

    component MetricCard: Rectangle {
        property var metric: ({})
        property bool selected: false
        signal clicked(string key)
        width: 146
        height: 58
        radius: 7
        color: metricMouse.containsMouse ? root.panelColor : root.panelDeepColor
        border.color: selected ? root.accentColor : (metric.primary ? "#4a9aa0" : root.borderColor)
        border.width: selected ? 2 : 1
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 2
            Label { text: metric.group || "Metric"; color: root.mutedTextColor; font.pixelSize: 9; elide: Text.ElideRight; Layout.fillWidth: true }
            Label { text: metric.value || ""; color: root.textColor; font.pixelSize: 15; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
            Label { text: metric.label || ""; color: root.mutedTextColor; font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true }
        }
        MouseArea { id: metricMouse; anchors.fill: parent; hoverEnabled: true; onClicked: parent.clicked(metric.key || "") }
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

    component ExecutionField: TextField {
        property int fieldWidth: 70
        selectByMouse: true
        color: root.textColor
        font.pixelSize: 12
        Layout.preferredWidth: fieldWidth
        Layout.preferredHeight: 24
        background: Rectangle { color: root.panelDeepColor; border.color: root.borderColor; radius: 5 }
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
                    SessionPickerCombo {
                        id: sessionBox
                        Layout.fillWidth: true
                        Layout.preferredWidth: 520
                        caption: "Session"
                        rows: root.backtestVm.sessions
                        emptyLabel: "Select session"
                        popupWidth: 720
                        onPicked: function(id) {
                            if (root.firstExtraSessionId() === id)
                                root.backtestVm.setExtraSessionIds("")
                            root.backtestVm.setSelectedSessionId(id)
                        }
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
                    SessionPickerCombo {
                        id: secondarySessionBox
                        Layout.fillWidth: true
                        Layout.preferredWidth: 260
                        caption: "Second leg"
                        rows: root.secondarySessionRows
                        emptyLabel: "No second leg"
                        popupWidth: 720
                        preferredOpenGroupId: root.sessionGroupId(root.backtestVm.selectedSessionId)
                        scrollToPreferredGroupOnOpen: true
                        enabled: root.secondarySessionRows.length > 1 || root.firstExtraSessionId().length > 0
                        opacity: enabled ? 1.0 : 0.55
                        onPicked: function(id) { root.backtestVm.setExtraSessionIds(id) }
                        Component.onCompleted: root.syncSelections()
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
                    ActionButton { text: "Refresh"; onClicked: { root.backtestVm.reloadSessions(); root.backtestVm.refreshResults() } }
                    ActionButton { text: root.backtestVm.running ? "Running" : "Start"; enabledValue: root.backtestVm.canRun; accent: root.goodColor; onClicked: root.backtestVm.startBacktest() }
                    ActionButton { text: "Start sweep"; enabledValue: root.backtestVm.canRun; accent: root.accentColor; onClicked: root.backtestVm.startSweep() }
                    ActionButton { visible: root.backtestVm.running; text: "Cancel"; enabledValue: root.backtestVm.running; accent: root.badColor; onClicked: root.backtestVm.cancelBacktest() }
                }

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: Math.max(104, 44 + root.backtestVm.selectedSessionLegs.length * 74)
                    Layout.maximumHeight: Layout.preferredHeight
                    Layout.leftMargin: 0
                    Layout.rightMargin: 0
                    color: "transparent"

                    Flickable {
                        id: executionFlick
                        property int tableWidth: 1160
                        anchors.fill: parent
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        contentWidth: Math.max(width, tableWidth)
                        contentHeight: executionContent.implicitHeight

                        ColumnLayout {
                            id: executionContent
                            width: Math.max(executionFlick.width, executionFlick.tableWidth)
                            spacing: 4

                            RowLayout {
                                width: executionContent.width
                                spacing: 8
                                Label { text: "Leg"; color: root.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 210 }
                                Label { text: "Balance"; color: root.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 82 }
                                Label { text: "MD base"; color: root.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 70 }
                                Label { text: "MD jit"; color: root.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 70 }
                                Label { text: "Mkt base"; color: root.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 74 }
                                Label { text: "Mkt jit"; color: root.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 74 }
                                Label { text: "Limit base"; color: root.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 78 }
                                Label { text: "Limit jit"; color: root.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 78 }
                                Label { text: "Cancel base"; color: root.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 82 }
                                Label { text: "Cancel jit"; color: root.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 78 }
                                Label { text: "User base"; color: root.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 78 }
                                Label { text: "User jit"; color: root.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 74 }
                                CompactField {
                                    caption: "Seed"
                                    fieldWidth: 82
                                    text: root.backtestVm.latencySeed
                                    onEdited: function(value) { root.backtestVm.latencySeed = value }
                                }
                                Item { Layout.fillWidth: true }
                            }

                            Repeater {
                                model: root.backtestVm.selectedSessionLegs
                                delegate: ColumnLayout {
                                    required property var modelData
                                    width: executionContent.width
                                    spacing: 4

                                    RowLayout {
                                        width: parent.width
                                        spacing: 8
                                        Label {
                                            text: modelData.label
                                            color: root.textColor
                                            font.pixelSize: 12
                                            elide: Text.ElideRight
                                            Layout.preferredWidth: 210
                                        }
                                        TextField {
                                            text: modelData.initialBalanceUsdt
                                            selectByMouse: true
                                            color: root.textColor
                                            font.pixelSize: 12
                                            Layout.preferredWidth: 82
                                            background: Rectangle { color: root.panelDeepColor; border.color: root.borderColor; radius: 5 }
                                            onEditingFinished: root.backtestVm.setVenueExecutionValue(modelData.index, "initial_balance_usdt", text)
                                        }
                                        TextField {
                                            text: modelData.marketDataLatencyUs
                                            selectByMouse: true
                                            color: root.textColor
                                            font.pixelSize: 12
                                            Layout.preferredWidth: 70
                                            background: Rectangle { color: root.panelDeepColor; border.color: root.borderColor; radius: 5 }
                                            onEditingFinished: root.backtestVm.setVenueExecutionValue(modelData.index, "market_data_latency_us", text)
                                        }
                                        TextField {
                                            text: modelData.marketDataJitterUs
                                            selectByMouse: true
                                            color: root.textColor
                                            font.pixelSize: 12
                                            Layout.preferredWidth: 70
                                            background: Rectangle { color: root.panelDeepColor; border.color: root.borderColor; radius: 5 }
                                            onEditingFinished: root.backtestVm.setVenueExecutionValue(modelData.index, "market_data_jitter_us", text)
                                        }
                                        TextField {
                                            text: modelData.marketOrderLatencyUs
                                            selectByMouse: true
                                            color: root.textColor
                                            font.pixelSize: 12
                                            Layout.preferredWidth: 74
                                            background: Rectangle { color: root.panelDeepColor; border.color: root.borderColor; radius: 5 }
                                            onEditingFinished: root.backtestVm.setVenueExecutionValue(modelData.index, "market_order_latency_us", text)
                                        }
                                        TextField {
                                            text: modelData.marketOrderJitterUs
                                            selectByMouse: true
                                            color: root.textColor
                                            font.pixelSize: 12
                                            Layout.preferredWidth: 74
                                            background: Rectangle { color: root.panelDeepColor; border.color: root.borderColor; radius: 5 }
                                            onEditingFinished: root.backtestVm.setVenueExecutionValue(modelData.index, "market_order_jitter_us", text)
                                        }
                                        TextField {
                                            text: modelData.limitOrderLatencyUs
                                            selectByMouse: true
                                            color: root.textColor
                                            font.pixelSize: 12
                                            Layout.preferredWidth: 78
                                            background: Rectangle { color: root.panelDeepColor; border.color: root.borderColor; radius: 5 }
                                            onEditingFinished: root.backtestVm.setVenueExecutionValue(modelData.index, "limit_order_latency_us", text)
                                        }
                                        TextField {
                                            text: modelData.limitOrderJitterUs
                                            selectByMouse: true
                                            color: root.textColor
                                            font.pixelSize: 12
                                            Layout.preferredWidth: 78
                                            background: Rectangle { color: root.panelDeepColor; border.color: root.borderColor; radius: 5 }
                                            onEditingFinished: root.backtestVm.setVenueExecutionValue(modelData.index, "limit_order_jitter_us", text)
                                        }
                                        TextField {
                                            text: modelData.cancelOrderLatencyUs
                                            selectByMouse: true
                                            color: root.textColor
                                            font.pixelSize: 12
                                            Layout.preferredWidth: 82
                                            background: Rectangle { color: root.panelDeepColor; border.color: root.borderColor; radius: 5 }
                                            onEditingFinished: root.backtestVm.setVenueExecutionValue(modelData.index, "cancel_order_latency_us", text)
                                        }
                                        TextField {
                                            text: modelData.cancelOrderJitterUs
                                            selectByMouse: true
                                            color: root.textColor
                                            font.pixelSize: 12
                                            Layout.preferredWidth: 78
                                            background: Rectangle { color: root.panelDeepColor; border.color: root.borderColor; radius: 5 }
                                            onEditingFinished: root.backtestVm.setVenueExecutionValue(modelData.index, "cancel_order_jitter_us", text)
                                        }
                                        TextField {
                                            text: modelData.userDataLatencyUs
                                            selectByMouse: true
                                            color: root.textColor
                                            font.pixelSize: 12
                                            Layout.preferredWidth: 78
                                            background: Rectangle { color: root.panelDeepColor; border.color: root.borderColor; radius: 5 }
                                            onEditingFinished: root.backtestVm.setVenueExecutionValue(modelData.index, "user_data_latency_us", text)
                                        }
                                        TextField {
                                            text: modelData.userDataJitterUs
                                            selectByMouse: true
                                            color: root.textColor
                                            font.pixelSize: 12
                                            Layout.preferredWidth: 74
                                            background: Rectangle { color: root.panelDeepColor; border.color: root.borderColor; radius: 5 }
                                            onEditingFinished: root.backtestVm.setVenueExecutionValue(modelData.index, "user_data_jitter_us", text)
                                        }
                                        Item { Layout.fillWidth: true }
                                    }

                                    Label {
                                        text: modelData.executionPresetSummary
                                        color: root.mutedTextColor
                                        font.pixelSize: 11
                                        wrapMode: Text.Wrap
                                        maximumLineCount: 2
                                        elide: Text.ElideRight
                                        Layout.leftMargin: 0
                                        Layout.fillWidth: true
                                    }
                                    Label {
                                        visible: root.backtestVm.selectedSessionCount > 1 && !root.backtestVm.canRun
                                        text: visible ? root.backtestVm.statusText : ""
                                        color: root.badColor
                                        font.pixelSize: 12
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                    }
                            }
                        }
                    }
                    }
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
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(78, paramsFlow.childrenRect.height + 16)
            Layout.minimumHeight: Layout.preferredHeight
            Layout.maximumHeight: Layout.preferredHeight
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
                            ToolTip.visible: paramHover.hovered && String(modelData.description || "").length > 0
                            ToolTip.text: modelData.description || ""
                            ToolTip.delay: 350

                            HoverHandler { id: paramHover }

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
                                text: modelData.min || ""
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
                                text: modelData.max || ""
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
                                text: modelData.step || ""
                                selectByMouse: true
                                color: root.textColor
                                font.pixelSize: 11
                                background: Rectangle { color: root.panelDeepColor; border.color: root.borderColor; radius: 5 }
                                onEditingFinished: root.backtestVm.setStrategyParameterRange(modelData.key, minField.text, maxField.text, text)
                            }
                        }
                    }

                    Item {
                        width: 150
                        height: 62

                        Label {
                            x: 0
                            y: 0
                            width: parent.width
                            height: 18
                            text: "Rate limits"
                            color: root.textColor
                            font.pixelSize: 11
                            font.bold: true
                        }

                        CheckBox {
                            id: rateLimitsEnabledBox
                            x: 0
                            y: 22
                            width: 78
                            height: 28
                            checked: root.backtestVm.rateLimitsEnabled
                            text: "On"
                            font.pixelSize: 12
                            palette.text: root.textColor
                            indicator: Rectangle {
                                implicitWidth: 18
                                implicitHeight: 18
                                width: 18
                                height: 18
                                x: 0
                                y: parent.height / 2 - height / 2
                                radius: 4
                                color: rateLimitsEnabledBox.checked ? root.panelAltColor : root.panelDeepColor
                                border.width: 1
                                border.color: rateLimitsEnabledBox.checked ? root.accentColor : root.borderColor

                                Rectangle {
                                    width: 8
                                    height: 8
                                    anchors.centerIn: parent
                                    radius: 2
                                    color: root.accentColor
                                    visible: rateLimitsEnabledBox.checked
                                }
                            }
                            contentItem: Text {
                                leftPadding: rateLimitsEnabledBox.indicator.width + rateLimitsEnabledBox.spacing
                                text: rateLimitsEnabledBox.text
                                color: root.textColor
                                font: rateLimitsEnabledBox.font
                                verticalAlignment: Text.AlignVCenter
                            }
                            onToggled: root.backtestVm.rateLimitsEnabled = checked
                        }
                    }

                    Item {
                        width: 620
                        height: 62

                        Label {
                            x: 0
                            y: 0
                            width: parent.width
                            height: 18
                            text: "Risk"
                            color: root.textColor
                            font.pixelSize: 11
                            font.bold: true
                        }

                        CheckBox {
                            id: riskEnabledBox
                            x: 0
                            y: 22
                            width: 62
                            height: 28
                            checked: root.backtestVm.riskEnabled
                            text: "On"
                            font.pixelSize: 12
                            palette.text: root.textColor
                            contentItem: Text {
                                leftPadding: riskEnabledBox.indicator.width + riskEnabledBox.spacing
                                text: riskEnabledBox.text
                                color: root.textColor
                                font: riskEnabledBox.font
                                verticalAlignment: Text.AlignVCenter
                            }
                            onToggled: root.backtestVm.riskEnabled = checked
                        }

                        TextField {
                            x: 70
                            y: 24
                            width: 92
                            height: 26
                            placeholderText: "equity %"
                            text: root.backtestVm.riskMinEquityPct
                            enabled: root.backtestVm.riskEnabled
                            opacity: enabled ? 1.0 : 0.45
                            selectByMouse: true
                            color: root.textColor
                            placeholderTextColor: root.mutedTextColor
                            font.pixelSize: 12
                            background: Rectangle { color: root.panelDeepColor; border.color: root.borderColor; radius: 5 }
                            onEditingFinished: root.backtestVm.riskMinEquityPct = text
                        }

                        TextField {
                            x: 170
                            y: 24
                            width: 92
                            height: 26
                            placeholderText: "leg %"
                            text: root.backtestVm.riskMinLegEquityPct
                            enabled: root.backtestVm.riskEnabled
                            opacity: enabled ? 1.0 : 0.45
                            selectByMouse: true
                            color: root.textColor
                            placeholderTextColor: root.mutedTextColor
                            font.pixelSize: 12
                            background: Rectangle { color: root.panelDeepColor; border.color: root.borderColor; radius: 5 }
                            onEditingFinished: root.backtestVm.riskMinLegEquityPct = text
                        }

                        TextField {
                            x: 270
                            y: 24
                            width: 112
                            height: 26
                            placeholderText: "leg USDT"
                            text: root.backtestVm.riskMinLegEquityUsdt
                            enabled: root.backtestVm.riskEnabled
                            opacity: enabled ? 1.0 : 0.45
                            selectByMouse: true
                            color: root.textColor
                            placeholderTextColor: root.mutedTextColor
                            font.pixelSize: 12
                            background: Rectangle { color: root.panelDeepColor; border.color: root.borderColor; radius: 5 }
                            onEditingFinished: root.backtestVm.riskMinLegEquityUsdt = text
                        }

                        TextField {
                            x: 390
                            y: 24
                            width: 120
                            height: 26
                            placeholderText: "max pos USDT"
                            text: root.backtestVm.riskMaxPositionUsdt
                            enabled: root.backtestVm.riskEnabled
                            opacity: enabled ? 1.0 : 0.45
                            selectByMouse: true
                            color: root.textColor
                            placeholderTextColor: root.mutedTextColor
                            font.pixelSize: 12
                            background: Rectangle { color: root.panelDeepColor; border.color: root.borderColor; radius: 5 }
                            onEditingFinished: root.backtestVm.riskMaxPositionUsdt = text
                        }

                        TextField {
                            x: 520
                            y: 24
                            width: 92
                            height: 26
                            placeholderText: "RL min"
                            text: root.backtestVm.riskRateLimitGuardMinRemaining
                            enabled: root.backtestVm.rateLimitsEnabled
                            opacity: enabled ? 1.0 : 0.45
                            selectByMouse: true
                            color: root.textColor
                            placeholderTextColor: root.mutedTextColor
                            font.pixelSize: 12
                            background: Rectangle { color: root.panelDeepColor; border.color: root.borderColor; radius: 5 }
                            onEditingFinished: root.backtestVm.riskRateLimitGuardMinRemaining = text
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
                            visible: root.backtestVm.selectedIsSweep && root.backtestVm.selectedDetailsLoaded
                            Layout.preferredWidth: 158
                            caption: "View"
                            textRole: "label"
                            valueRole: "id"
                            model: root.backtestVm.sweepViewChoices
                            popupWidth: 180
                            Component.onCompleted: currentIndex = indexOfValue(root.backtestVm.selectedSweepView)
                            onActivated: root.backtestVm.setSelectedSweepView(currentValue)
                        }
                        RecorderComboBox {
                            visible: root.backtestVm.selectedIsSweep && root.backtestVm.selectedDetailsLoaded && root.backtestVm.selectedSweepView === "curves"
                            Layout.preferredWidth: 132
                            caption: "Curves"
                            textRole: "label"
                            valueRole: "id"
                            model: root.backtestVm.sweepCurveLimitChoices
                            popupWidth: 140
                            Component.onCompleted: currentIndex = indexOfValue(root.backtestVm.selectedSweepCurveLimit)
                            onActivated: root.backtestVm.setSelectedSweepCurveLimit(currentValue)
                        }
                        RecorderComboBox {
                            visible: root.backtestVm.selectedIsSweep && root.backtestVm.selectedDetailsLoaded
                            Layout.preferredWidth: 170
                            caption: "Metric"
                            textRole: "label"
                            valueRole: "id"
                            model: root.backtestVm.sweepMetricChoices
                            popupWidth: 220
                            Component.onCompleted: currentIndex = indexOfValue(root.backtestVm.selectedSweepMetric)
                            onActivated: root.backtestVm.setSelectedSweepMetric(currentValue)
                        }
                        RecorderComboBox {
                            visible: root.backtestVm.selectedIsSweep && root.backtestVm.selectedDetailsLoaded && root.backtestVm.selectedSweepView === "distribution"
                            Layout.preferredWidth: 180
                            caption: "Parameter"
                            textRole: "label"
                            valueRole: "id"
                            model: root.backtestVm.selectedSweepDistributionParamChoices
                            popupWidth: 220
                            Component.onCompleted: currentIndex = indexOfValue(root.backtestVm.selectedSweepDistributionParam)
                            onActivated: root.backtestVm.setSelectedSweepDistributionParam(currentValue)
                        }
                        ActionButton {
                            text: root.backtestVm.selectedDetailsLoading ? "Loading" : (root.hasSelectedVisualData() ? "Visual loaded" : (root.backtestVm.selectedDetailsLoaded ? "Reload visual" : "Load visual"))
                            visible: root.backtestVm.hasSelection
                            enabledValue: !root.backtestVm.selectedDetailsLoading && !root.hasSelectedVisualData()
                            accent: root.goodColor
                            onClicked: root.loadOrReloadVisual()
                        }
                        ActionButton { text: root.sweepPercentMode ? "PnL %" : "PnL $"; visible: root.backtestVm.selectedIsSweep && root.backtestVm.selectedDetailsLoaded; enabledValue: root.backtestVm.selectedInitialBalanceE8 > 0; onClicked: { root.sweepPercentMode = !root.sweepPercentMode; sweepCanvas.requestPaint(); sweepHoverCanvas.requestPaint(); distributionCanvas.requestPaint(); distributionHoverCanvas.requestPaint() } }
                        ActionButton { text: "Apply"; visible: root.backtestVm.selectedIsSweep; enabledValue: root.backtestVm.selectedDetailsLoaded && root.selectedSweepPointId >= 0 && !root.backtestVm.running; onClicked: root.backtestVm.applySweepPointById(root.selectedSweepPointId) }
                        ActionButton { text: "Detailed run"; visible: root.backtestVm.selectedIsSweep; enabledValue: root.backtestVm.selectedDetailsLoaded && root.selectedSweepPointId >= 0 && !root.backtestVm.running; accent: root.goodColor; onClicked: root.backtestVm.startDetailedRunFromSweepPointById(root.selectedSweepPointId) }
                        ActionButton { text: "Delete"; visible: root.backtestVm.hasSelection; enabledValue: root.backtestVm.hasSelection && !root.backtestVm.running; accent: root.badColor; onClicked: root.backtestVm.deleteSelectedRun() }
                        Label {
                            text: root.selectedErrorDisplayText()
                            visible: text !== ""
                            color: root.badColor
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            Layout.maximumWidth: 360
                        }
                    }

                    Rectangle {
                        visible: root.backtestVm.hasSelection
                        Layout.fillWidth: true
                        Layout.preferredHeight: 52
                        color: root.panelDeepColor
                        border.color: root.borderColor
                        radius: 6
                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 12
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Label { text: "Backtest parameters"; color: root.textColor; font.pixelSize: 13; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                                Label { text: root.backtestVm.selectedConfigText || root.backtestVm.selectedRunId; color: root.mutedTextColor; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
                            }
                            Label {
                                text: root.hasSelectedVisualData() ? "Visual loaded" : (root.backtestVm.selectedDetailsLoading ? "Loading visual" : (root.backtestVm.selectedPreviewLoading ? "Loading preview" : (root.backtestVm.selectedDetailsLoaded ? "Visual data missing" : "Visual optional")))
                                color: root.mutedTextColor
                                font.pixelSize: 11
                            }
                        }
                    }

                    Rectangle {
                        visible: root.selectedErrorDisplayText() !== ""
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(132, Math.max(54, errorContent.implicitHeight + 20))
                        color: "#2c1f22"
                        border.color: root.badColor
                        radius: 6
                        clip: true

                        Flickable {
                            anchors.fill: parent
                            anchors.margins: 10
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            contentWidth: Math.max(1, width)
                            contentHeight: errorContent.implicitHeight
                            interactive: contentHeight > height

                            ColumnLayout {
                                id: errorContent
                                width: parent.width
                                spacing: 4
                                Label {
                                    text: "Errors"
                                    color: root.badColor
                                    font.pixelSize: 12
                                    font.bold: true
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: root.selectedErrorDisplayText()
                                    color: root.textColor
                                    font.pixelSize: 11
                                    wrapMode: Text.WrapAnywhere
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }

                    Rectangle {
                        visible: root.backtestVm.selectedWarningText !== ""
                        Layout.fillWidth: true
                        Layout.preferredHeight: Math.min(132, Math.max(54, warningContent.implicitHeight + 20))
                        color: "#2a251b"
                        border.color: "#8f6b2d"
                        radius: 6
                        clip: true

                        Flickable {
                            anchors.fill: parent
                            anchors.margins: 10
                            clip: true
                            boundsBehavior: Flickable.StopAtBounds
                            contentWidth: Math.max(1, width)
                            contentHeight: warningContent.implicitHeight
                            interactive: contentHeight > height

                            ColumnLayout {
                                id: warningContent
                                width: parent.width
                                spacing: 4
                                Label {
                                    text: "Warnings"
                                    color: "#f0b35a"
                                    font.pixelSize: 12
                                    font.bold: true
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: root.backtestVm.selectedWarningText
                                    color: root.textColor
                                    font.pixelSize: 11
                                    wrapMode: Text.WrapAnywhere
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }

                    ColumnLayout {
                        visible: !root.backtestVm.selectedIsSweep
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        spacing: 8
                        RowLayout {
                            Layout.fillWidth: true
                            Label { text: "Run metrics"; color: root.textColor; font.pixelSize: 14; font.bold: true; Layout.fillWidth: true }
                            RecorderComboBox {
                                id: resultScopeBox
                                Layout.preferredWidth: 220
                                caption: "Scope"
                                textRole: "label"
                                valueRole: "id"
                                model: root.backtestVm.resultScopeChoices
                                popupWidth: 280
                                visible: root.backtestVm.resultScopeChoices.length > 1
                                enabled: root.backtestVm.resultScopeChoices.length > 1
                                Component.onCompleted: currentIndex = indexOfValue(root.backtestVm.selectedResultScope)
                                onActivated: root.backtestVm.setSelectedResultScope(currentValue)
                                Connections {
                                    target: root.backtestVm
                                    function onSelectedResultScopeChanged() { resultScopeBox.currentIndex = resultScopeBox.indexOfValue(root.backtestVm.selectedResultScope) }
                                    function onSelectionChanged() { resultScopeBox.currentIndex = resultScopeBox.indexOfValue(root.backtestVm.selectedResultScope) }
                                }
                            }
                            Label {
                                text: root.backtestVm.selectedDetailsLoading ? "Loading visual data..." : (root.backtestVm.selectedPreviewLoading ? "Loading PnL preview..." : (!root.backtestVm.selectedDetailsLoaded ? "Load visual to open chart" : (root.backtestVm.selectedResultMetrics.length > 0 ? "" : "Selected run has no metrics")))
                                color: root.mutedTextColor
                                font.pixelSize: 11
                                visible: text !== ""
                            }
                        }
                        Flow {
                            visible: root.backtestVm.selectedResultMetrics.length > 0
                            Layout.fillWidth: true
                            Layout.preferredHeight: childrenRect.height
                            spacing: 8
                            Repeater {
                                model: root.backtestVm.selectedResultMetrics
                                delegate: MetricCard {
                                    metric: modelData
                                    selected: modelData.key === root.backtestVm.selectedResultMetricKey
                                    onClicked: function(key) { root.backtestVm.setSelectedResultMetricKey(key) }
                                }
                            }
                        }
                        Rectangle {
                            visible: !root.backtestVm.selectedDetailsLoaded
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumHeight: 220
                            color: root.panelDeepColor
                            border.color: root.borderColor
                            radius: 6
                            Label {
                                anchors.centerIn: parent
                                text: root.backtestVm.selectedDetailsLoading ? "Loading visual data..." : (root.backtestVm.selectedPreviewLoading ? "Loading PnL preview..." : "Load visual to open chart.")
                                color: root.mutedTextColor
                                font.pixelSize: 12
                            }
                        }
                        Rectangle {
                            visible: root.backtestVm.selectedDetailsLoaded && root.backtestVm.selectedResultMetrics.length > 0
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            Layout.minimumHeight: 300
                            color: root.panelDeepColor
                            border.color: root.borderColor
                            radius: 6
                            clip: true

                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 8

                                RowLayout {
                                    Layout.fillWidth: true
                                    Label { text: root.selectedMetricField("label"); color: root.textColor; font.pixelSize: 14; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                                    Label { text: root.selectedMetricField("value"); color: root.textColor; font.pixelSize: 14; font.bold: true }
                                    RecorderComboBox {
                                        id: metricRatioBox
                                        Layout.preferredWidth: 172
                                        caption: "Divide by"
                                        textRole: "label"
                                        valueRole: "id"
                                        model: root.backtestVm.resultMetricRatioChoices
                                        popupWidth: 190
                                        Component.onCompleted: currentIndex = indexOfValue(root.backtestVm.selectedResultMetricRatioKey)
                                        onActivated: root.backtestVm.setSelectedResultMetricRatioKey(currentValue)
                                        Connections {
                                            target: root.backtestVm
                                            function onSelectedResultMetricChanged() { metricRatioBox.currentIndex = metricRatioBox.indexOfValue(root.backtestVm.selectedResultMetricRatioKey) }
                                            function onSelectionChanged() { metricRatioBox.currentIndex = metricRatioBox.indexOfValue(root.backtestVm.selectedResultMetricRatioKey) }
                                        }
                                    }
                                }

                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: 3
                                    columnSpacing: 10
                                    rowSpacing: 4
                                    Label { text: "Что это"; color: root.mutedTextColor; font.pixelSize: 10; Layout.fillWidth: true }
                                    Label { text: "Зачем смотреть"; color: root.mutedTextColor; font.pixelSize: 10; Layout.fillWidth: true }
                                    Label { text: "Как читать"; color: root.mutedTextColor; font.pixelSize: 10; Layout.fillWidth: true }
                                    Label { text: root.selectedMetricField("description"); color: root.textColor; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                                    Label { text: root.selectedMetricField("why"); color: root.textColor; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                                    Label { text: root.selectedMetricField("interpretation"); color: root.textColor; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                                }

                                Rectangle {
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    Layout.minimumHeight: 170
                                    color: root.windowColor
                                    border.color: root.borderColor
                                    radius: 5
                                    clip: true

                                    Canvas {
                                        id: metricCanvas
                                        anchors.fill: parent
                                        anchors.margins: 10
                                        property var points: root.backtestVm.selectedResultMetricSeries

                                        Connections {
                                            target: root.backtestVm
                                            function onSelectedResultMetricChanged() { metricCanvas.requestPaint() }
                                            function onSelectionChanged() { metricCanvas.requestPaint() }
                                        }
                                        onWidthChanged: requestPaint()
                                        onHeightChanged: requestPaint()
                                        onPointsChanged: requestPaint()

                                        function seriesValue(point) {
                                            if (root.backtestVm.selectedResultMetricRatioKey.length > 0) {
                                                if (!point.hasRatio) return NaN
                                                return Number(point.valueRaw) / Number(point.denominatorRaw)
                                            }
                                            return Number(point.valueRaw)
                                        }

                                        function paddedBounds(points) {
                                            var has = false
                                            var minValue = 0
                                            var maxValue = 0
                                            for (var i = 0; i < points.length; ++i) {
                                                var value = seriesValue(points[i])
                                                if (!isFinite(value)) continue
                                                if (!has) { minValue = value; maxValue = value; has = true }
                                                else { minValue = Math.min(minValue, value); maxValue = Math.max(maxValue, value) }
                                            }
                                            if (!has) return null
                                            minValue = Math.min(minValue, 0)
                                            maxValue = Math.max(maxValue, 0)
                                            var span = maxValue - minValue
                                            if (span <= 0) span = root.backtestVm.selectedResultMetricRatioKey.length > 0 ? 1.0 : 100000000
                                            var pad = Math.max(span * 0.08, root.backtestVm.selectedResultMetricRatioKey.length > 0 ? 0.01 : 1000000)
                                            return { "min": minValue - pad, "max": maxValue + pad }
                                        }

                                        function yFor(value, minValue, maxValue, plotY, plotH) {
                                            return plotY + plotH - ((value - minValue) / (maxValue - minValue)) * plotH
                                        }

                                        function drawScale(ctx, bounds, plotX, plotY, plotW, plotH) {
                                            ctx.font = "10px sans-serif"
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
                                                ctx.fillText(root.metricPointText(value, root.backtestVm.selectedResultMetricKey), plotX - 8, y)
                                            }
                                        }

                                        onPaint: {
                                            var ctx = getContext("2d")
                                            ctx.clearRect(0, 0, width, height)
                                            var points = root.backtestVm.selectedResultMetricSeries
                                            if (points.length < 2) return
                                            var plotX = 78
                                            var plotY = 8
                                            var plotW = Math.max(20, width - plotX - 10)
                                            var plotH = Math.max(20, height - plotY - 24)
                                            var bounds = paddedBounds(points)
                                            if (bounds === null) return
                                            drawScale(ctx, bounds, plotX, plotY, plotW, plotH)
                                            if (bounds.min < 0 && bounds.max > 0) {
                                                var zeroY = yFor(0, bounds.min, bounds.max, plotY, plotH)
                                                ctx.strokeStyle = "#8a92a0"
                                                ctx.lineWidth = 1
                                                ctx.beginPath()
                                                ctx.moveTo(plotX, zeroY)
                                                ctx.lineTo(plotX + plotW, zeroY)
                                                ctx.stroke()
                                            }
                                            ctx.strokeStyle = root.accentColor
                                            ctx.lineWidth = 2
                                            ctx.beginPath()
                                            var started = false
                                            for (var p = 0; p < points.length; ++p) {
                                                var pointValue = seriesValue(points[p])
                                                if (!isFinite(pointValue)) continue
                                                var x = plotX + (p / (points.length - 1)) * plotW
                                                var y = yFor(pointValue, bounds.min, bounds.max, plotY, plotH)
                                                if (!started) { ctx.moveTo(x, y); started = true }
                                                else ctx.lineTo(x, y)
                                            }
                                            if (started) ctx.stroke()
                                            var lastValue = NaN
                                            for (var last = points.length - 1; last >= 0; --last) {
                                                lastValue = seriesValue(points[last])
                                                if (isFinite(lastValue)) break
                                            }
                                            ctx.font = "11px sans-serif"
                                            ctx.textAlign = "left"
                                            ctx.textBaseline = "bottom"
                                            ctx.fillStyle = root.textColor
                                            if (isFinite(lastValue)) ctx.fillText(root.metricPointText(lastValue, root.backtestVm.selectedResultMetricKey), plotX, height - 2)
                                        }
                                    }

                                    Label {
                                        anchors.centerIn: parent
                                        visible: root.backtestVm.selectedResultMetricSeries.length < 2
                                        text: "Для этой карточки есть только итоговое значение, без временного ряда."
                                        color: root.mutedTextColor
                                        font.pixelSize: 12
                                    }
                                }
                            }
                        }
                        RowLayout {
                            Layout.fillWidth: true
                            ActionButton { text: root.showRawSummary ? "Hide details" : "Details"; onClicked: root.showRawSummary = !root.showRawSummary }
                            Label { text: "Raw summary JSON"; color: root.mutedTextColor; font.pixelSize: 11; visible: root.showRawSummary }
                        }
                        TextArea {
                            visible: root.showRawSummary
                            Layout.fillWidth: true
                            Layout.preferredHeight: 180
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

                        Label {
                            visible: !root.backtestVm.selectedDetailsLoaded
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            text: root.backtestVm.selectedDetailsLoading ? "Loading visual data..." : "Load visual to open sweep charts."
                            color: root.mutedTextColor
                            font.pixelSize: 12
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }

                        Rectangle {
                            visible: root.backtestVm.selectedDetailsLoaded
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            color: root.panelDeepColor
                            border.color: root.borderColor
                            radius: 6
                            clip: true

                            Canvas {
                                id: sweepCanvas
                                visible: root.backtestVm.selectedSweepView === "curves"
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
                                property var sweepCurveValues: []

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

                                function curveValues(curve) {
                                    var values = []
                                    for (var i = 0; i < curve.curve.length; ++i) values.push(root.sweepValue(curve.curve[i], curve.initialBalanceE8))
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
                                    if (span <= 0) span = root.sweepPercentMode ? 2.0 : 200000000
                                    var pad = Math.max(root.sweepPercentMode ? 0.01 : 1000000, span * 0.08)
                                    return { min: minPnl - pad, max: maxPnl + pad }
                                }

                                function sweepStateCacheKey(curves) {
                                    var key = root.backtestVm.selectedRunId + ":" + root.backtestVm.selectedSweepMetric + ":" + root.backtestVm.selectedSweepCurveLimit + ":" + root.sweepPercentMode + ":" + curves.length
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
                                    var values = sweepCurveValues[index] || []
                                    ctx.strokeStyle = colorFor(index)
                                    ctx.globalAlpha = root.selectedSweepPointId < 0 || root.selectedSweepPointId === curve.pointId ? 0.95 : 0.35
                                    ctx.lineWidth = root.selectedSweepPointId === curve.pointId ? 3 : 1.7
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
                                    root.selectedSweepPointId = hoverPointId
                                    root.backtestVm.applySweepPointById(hoverPointId)
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
                                    ctx.fillStyle = root.textColor
                                    ctx.fillText("#" + curve.pointId + " step " + (hoverStep + 1), cardX + 10, cardY + 8)
                                    ctx.fillStyle = Number(curve.totalPnlE8) < 0 ? root.badColor : root.goodColor
                                    ctx.fillText((curve.metricLabel || "PnL") + " " + root.sweepText(value, curve.initialBalanceE8), cardX + 10, cardY + 28)
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
                                visible: root.backtestVm.selectedSweepView === "curves"
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
                                visible: root.backtestVm.selectedSweepView === "curves"
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
                                visible: root.backtestVm.selectedSweepView === "distribution"
                                anchors.fill: parent
                                anchors.margins: 12
                                property int hoverPointId: -1
                                property int hoverIndex: -1
                                property real hoverX: 0
                                property real hoverY: 0
                                property var bars: root.backtestVm.selectedSweepDistributionBars
                                property string stateKey: ""
                                property var bounds: ({ "min": 0, "max": 1 })
                                property var layoutRows: []
                                property var barValues: []

                                Connections {
                                    target: root.backtestVm
                                    function onSelectionChanged() { distributionCanvas.clearState(); distributionCanvas.clearHover(); distributionCanvas.requestPaint(); distributionHoverCanvas.requestPaint() }
                                }
                                onWidthChanged: requestPaint()
                                onHeightChanged: requestPaint()

                                function barValue(bar) { return root.sweepValue(bar.metricRaw, bar.initialBalanceE8 || root.backtestVm.selectedInitialBalanceE8) }

                                function stateCacheKey(rows) {
                                    var key = root.backtestVm.selectedRunId + ":" + root.backtestVm.selectedSweepMetric + ":" + root.backtestVm.selectedSweepDistributionParam + ":" + root.sweepPercentMode + ":" + rows.length
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
                                    if (span <= 0) span = root.sweepPercentMode ? 2.0 : 200000000
                                    var pad = Math.max(root.sweepPercentMode ? 0.01 : 1000000, span * 0.08)
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
                                        ctx.fillStyle = root.mutedTextColor
                                        ctx.fillText(root.sweepPercentMode ? value.toFixed(2) + "%" : root.e8Text(value), plotX - 8, y)
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
                                        ctx.globalAlpha = root.selectedSweepPointId < 0 || root.selectedSweepPointId === bar.pointId ? 0.95 : 0.45
                                        ctx.fillStyle = value < 0 ? root.badColor : root.goodColor
                                        ctx.fillRect(item.x, top, item.width, h)
                                        ctx.globalAlpha = 1.0
                                        if (bar.paramText !== lastLabel) {
                                            if (item.centerX - lastLabelX >= 46) {
                                                ctx.fillStyle = root.mutedTextColor
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
                                    root.selectedSweepPointId = hoverPointId
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
                                    ctx.fillStyle = root.textColor
                                    ctx.fillText("#" + bar.pointId + "  " + bar.paramKey + "=" + bar.paramText, cardX + 10, cardY + 8)
                                    ctx.fillStyle = Number(bar.metricRaw) < 0 ? root.badColor : root.goodColor
                                    ctx.fillText("PnL " + root.sweepText(value, root.backtestVm.selectedInitialBalanceE8), cardX + 10, cardY + 28)
                                    ctx.fillStyle = root.mutedTextColor
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
                                    ctx.fillStyle = root.mutedTextColor
                                    ctx.font = "11px sans-serif"
                                    ctx.textAlign = "center"
                                    ctx.fillText(root.backtestVm.selectedSweepDistributionParam, plotX + plotW / 2, plotY + plotH + 42)
                                }
                            }

                            Canvas {
                                id: distributionHoverCanvas
                                visible: root.backtestVm.selectedSweepView === "distribution"
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
                                visible: root.backtestVm.selectedSweepView === "distribution"
                                anchors.fill: distributionHoverCanvas
                                hoverEnabled: true
                                acceptedButtons: Qt.LeftButton
                                onPositionChanged: function(mouse) { distributionCanvas.updateHover(mouse.x, mouse.y) }
                                onClicked: distributionCanvas.selectHover()
                                onExited: distributionCanvas.clearHover()
                            }

                            Label {
                                anchors.centerIn: parent
                                visible: (root.backtestVm.selectedSweepView === "curves" && sweepCanvas.sweepCurves.length === 0) ||
                                         (root.backtestVm.selectedSweepView === "distribution" && distributionCanvas.bars.length === 0)
                                text: root.backtestVm.selectedSweepView === "distribution" ? "No sweep distribution" : "No sweep curves"
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
