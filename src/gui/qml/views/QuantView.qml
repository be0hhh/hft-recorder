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

    function e8Text(value) {
        var raw = Number(value || 0)
        var negative = raw < 0
        var absValue = Math.abs(raw) / 100000000.0
        var text = absValue.toFixed(4)
        while (text.indexOf(".") >= 0 && text.endsWith("0")) text = text.slice(0, -1)
        if (text.endsWith(".")) text = text.slice(0, -1)
        return (negative ? "-" : "") + text
    }

    function safeList(value) {
        return value && value.length !== undefined ? value : []
    }

    function matrixCell(rowExchange, columnExchange) {
        var cells = safeList(backtestVm.batchPairMatrixCells)
        var key = rowExchange + "|" + columnExchange
        for (var i = 0; i < cells.length; ++i) {
            if (cells[i].cellKey === key)
                return cells[i]
        }
        return ({})
    }

    function syncControls() {
        universeBox.currentIndex = universeBox.indexOfValue(root.backtestVm.batchUniverseId)
        strategyBox.currentIndex = strategyBox.indexOfValue(root.backtestVm.selectedStrategy)
    }

    background: Rectangle { color: root.windowColor }

    Connections {
        target: root.backtestVm
        function onSessionsChanged() { root.syncControls() }
        function onBatchConfigChanged() { root.syncControls() }
        function onSelectedStrategyChanged() { root.syncControls() }
        function onStrategyParametersChanged() { root.syncControls() }
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
        Layout.preferredHeight: 64
        radius: 6
        color: root.panelDeepColor
        border.color: row.tone === "good" ? root.goodColor : (row.tone === "bad" ? root.badColor : root.borderColor)
        border.width: 1
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 2
            Label { text: row.label || ""; color: root.mutedTextColor; font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true }
            Label { text: row.value || ""; color: root.textColor; font.pixelSize: 17; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
            Label { text: row.detail || ""; color: root.mutedTextColor; font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 8

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 160
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
                        caption: "Session / folder"
                        textRole: "label"
                        valueRole: "id"
                        model: root.backtestVm.batchUniverseChoices
                        popupWidth: 760
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
                        onActivated: root.backtestVm.setSelectedStrategy(currentValue)
                        Component.onCompleted: root.syncControls()
                    }

                    ActionButton {
                        text: "Only futures"
                        selected: root.backtestVm.batchOnlyFutures
                        accent: root.goodColor
                        onClicked: root.backtestVm.setBatchOnlyFutures(!root.backtestVm.batchOnlyFutures)
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
                    Label {
                        text: root.backtestVm.batchSummaryText || root.backtestVm.statusText
                        color: root.mutedTextColor
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }

                QuantContextStrip {
                    Layout.fillWidth: true
                    backtestVm: root.backtestVm
                    panelColor: root.panelColor
                    panelDeepColor: root.panelDeepColor
                    borderColor: root.borderColor
                    textColor: root.textColor
                    mutedTextColor: root.mutedTextColor
                    accentColor: root.accentColor
                    goodColor: root.goodColor
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: Math.max(80, paramsFlow.childrenRect.height + 16)
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            color: root.panelColor
            border.color: root.borderColor
            radius: 6

            Flow {
                id: paramsFlow
                anchors.fill: parent
                anchors.margins: 8
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
            }
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            columns: 6
            rowSpacing: 8
            columnSpacing: 8
            Repeater {
                model: root.backtestVm.batchSummaryCards
                delegate: StatCard { required property var modelData; row: modelData }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.bottomMargin: 10
            spacing: 8

            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 5
                color: root.panelColor
                border.color: root.borderColor
                radius: 6

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: "Exchange matrix"; color: root.textColor; font.pixelSize: 13; font.bold: true; Layout.fillWidth: true }
                        Label { text: root.backtestVm.batchOnlyFutures ? "futures only" : "all markets"; color: root.mutedTextColor; font.pixelSize: 11 }
                    }

                    Flickable {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        contentWidth: matrixColumn.implicitWidth
                        contentHeight: matrixColumn.implicitHeight
                        clip: true

                        ColumnLayout {
                            id: matrixColumn
                            spacing: 6
                            visible: root.backtestVm.batchPairMatrixColumns.length > 0

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
                                            border.color: cell.rows > 0 ? (cell.avgPnlE8 >= 0 ? root.goodColor : root.badColor) : root.borderColor
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

                        Label {
                            anchors.centerIn: parent
                            visible: root.backtestVm.batchPairMatrixColumns.length === 0
                            text: "Choose a session folder, set strategy params, then Start."
                            color: root.mutedTextColor
                            font.pixelSize: 13
                        }
                    }
                }
            }

            QuantResultsPanel {
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.preferredWidth: 6
                mode: root.backtestVm.batchRawTableMode
                stableRows: root.backtestVm.batchStableRows
                profitRows: root.backtestVm.batchProfitRows
                symbolRows: root.backtestVm.batchSymbolRows
                pairRows: root.backtestVm.batchPairRows
                paramRows: root.backtestVm.batchParamRows
                timeRows: root.backtestVm.batchTimeRows
                plateauRows: root.backtestVm.batchPlateauRows
                skippedRows: root.backtestVm.batchSkippedRows
                statusText: root.backtestVm.batchSummaryText || root.backtestVm.statusText
                panelColor: root.panelColor
                panelDeepColor: root.panelDeepColor
                panelAltColor: root.panelAltColor
                borderColor: root.borderColor
                textColor: root.textColor
                mutedTextColor: root.mutedTextColor
                accentColor: root.accentColor
                goodColor: root.goodColor
                badColor: root.badColor
                onModeRequested: function(nextMode) { root.backtestVm.setBatchRawTableMode(nextMode) }
            }
        }
    }
}
