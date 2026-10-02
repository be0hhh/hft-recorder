import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HftRecorder 1.0

Rectangle {
    id: toolbar
    required property var viewRoot

    function syncSelections() {
        viewRoot.rebuildSecondarySessionRows()
        sessionBox.currentIndex = sessionBox.indexOfValue(viewRoot.backtestVm.selectedSessionId)
        secondarySessionBox.currentIndex = secondarySessionBox.indexOfValue(viewRoot.firstExtraSessionId())
        strategyBox.currentIndex = strategyBox.indexOfValue(viewRoot.backtestVm.selectedStrategy)
        configModeBox.currentIndex = configModeBox.indexOfValue(viewRoot.backtestVm.configMode)
        indicatorBox.currentIndex = indicatorBox.indexOfValue(viewRoot.backtestVm.selectedIndicatorProfile)
    }


    function openLegPopup() { legSelector.openLegPopup() }
    function setSymbol() { symbolField.text = viewRoot.backtestVm.selectedSymbol }

    Layout.fillWidth: true
    Layout.preferredHeight: headerLayout.implicitHeight + 16
    Layout.minimumHeight: headerLayout.implicitHeight + 16
    color: viewRoot.chromeColor
    border.color: viewRoot.borderColor
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
                rows: viewRoot.backtestVm.sessions
                emptyLabel: "Select session"
                popupWidth: 720
                allowGroupSelection: true
                onPicked: function(id) { viewRoot.pickPrimarySession(id) }
                Component.onCompleted: toolbar.syncSelections()
            }
            BacktestCompactField {
                id: symbolField
                caption: "Symbol"
                fieldWidth: 120
                text: viewRoot.backtestVm.selectedSymbol
                onEdited: function(value) { viewRoot.backtestVm.selectedSymbol = value }
            }
            RecorderComboBox {
                id: strategyBox
                Layout.fillWidth: true
                Layout.preferredWidth: 300
                caption: "Strategy"
                textRole: "label"
                valueRole: "id"
                model: viewRoot.backtestVm.strategyChoices
                popupWidth: 420
                popupAlignRight: true
                onActivated: viewRoot.backtestVm.setSelectedStrategy(currentValue)
                Component.onCompleted: toolbar.syncSelections()
            }
            RecorderComboBox {
                id: configModeBox
                Layout.preferredWidth: 132
                caption: "Config"
                textRole: "label"
                valueRole: "id"
                model: viewRoot.backtestVm.configModeChoices
                popupWidth: 150
                enabled: count > 1
                opacity: enabled ? 1.0 : 0.55
                onActivated: viewRoot.backtestVm.setConfigMode(currentValue)
                Component.onCompleted: toolbar.syncSelections()
            }
            RecorderComboBox {
                id: indicatorBox
                Layout.preferredWidth: 170
                caption: "Indicator"
                textRole: "label"
                valueRole: "id"
                model: viewRoot.backtestVm.indicatorProfileChoices
                popupWidth: 190
                visible: count > 0
                enabled: count > 0
                onActivated: viewRoot.backtestVm.setSelectedIndicatorProfile(currentValue)
                Component.onCompleted: toolbar.syncSelections()
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            BacktestCompactField {
                caption: "Balance USDT"
                fieldWidth: 126
                text: viewRoot.backtestVm.initialBalanceUsdt
                onEdited: function(value) { viewRoot.backtestVm.initialBalanceUsdt = value }
            }
            SessionPickerCombo {
                id: secondarySessionBox
                visible: !viewRoot.primarySessionIsGrouped()
                Layout.fillWidth: true
                Layout.preferredWidth: visible ? 260 : 0
                caption: "Extra legs"
                rows: viewRoot.secondarySessionRows
                emptyLabel: "No extra legs"
                popupWidth: 720
                allowGroupSelection: true
                preferredOpenGroupId: viewRoot.sessionGroupId(viewRoot.backtestVm.selectedSessionId)
                scrollToPreferredGroupOnOpen: true
                enabled: visible && (viewRoot.secondarySessionRows.length > 1 || viewRoot.firstExtraSessionId().length > 0)
                opacity: enabled ? 1.0 : 0.55
                onPicked: function(id) { viewRoot.backtestVm.setExtraSessionIds(id) }
                Component.onCompleted: toolbar.syncSelections()
            }
            BacktestCompactField {
                caption: "Sweep budget"
                fieldWidth: 106
                text: viewRoot.backtestVm.sweepBudget
                onEdited: function(value) { viewRoot.backtestVm.sweepBudget = value }
            }
            BacktestCompactField {
                caption: "Sweep seed"
                fieldWidth: 92
                text: viewRoot.backtestVm.sweepSeed
                onEdited: function(value) { viewRoot.backtestVm.sweepSeed = value }
            }
            BacktestActionButton {
                text: viewRoot.backtestVm.resultsLoading ? "Queue refresh" : "Refresh"
                onClicked: { viewRoot.backtestVm.reloadSessions(); viewRoot.backtestVm.refreshResults() }
            }
            BacktestActionButton { text: viewRoot.backtestVm.running ? "Running" : "Start"; enabledValue: viewRoot.backtestVm.canRun; accent: viewRoot.goodColor; onClicked: viewRoot.backtestVm.startBacktest() }
            BacktestActionButton { text: "Start sweep"; enabledValue: viewRoot.backtestVm.canRun; accent: viewRoot.accentColor; onClicked: viewRoot.backtestVm.startSweep() }
            BacktestActionButton { text: "Execution latency sweep"; enabledValue: viewRoot.backtestVm.canRun; accent: viewRoot.goodColor; onClicked: viewRoot.backtestVm.startExecutionLatencySweep() }
            BacktestActionButton { visible: viewRoot.backtestVm.running; text: "Cancel"; enabledValue: viewRoot.backtestVm.running; accent: viewRoot.badColor; onClicked: viewRoot.backtestVm.cancelBacktest() }
        }

        BacktestLegSelector {
            id: legSelector
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            Layout.maximumHeight: implicitHeight
            backtestVm: viewRoot.backtestVm
            panelDeepColor: viewRoot.panelDeepColor
            borderColor: viewRoot.borderColor
            textColor: viewRoot.textColor
            mutedTextColor: viewRoot.mutedTextColor
            accentColor: viewRoot.accentColor
            goodColor: viewRoot.goodColor
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            ProgressBar { Layout.preferredWidth: 220; from: 0; to: 100; value: viewRoot.backtestVm.progressPercent }
            Label { text: viewRoot.backtestVm.progressPercent + "%"; color: viewRoot.textColor; font.bold: true; font.pixelSize: 12; Layout.preferredWidth: 42 }
            Label { text: viewRoot.backtestVm.progressText; color: viewRoot.mutedTextColor; font.pixelSize: 12; elide: Text.ElideRight; Layout.preferredWidth: 260 }
            Label {
                text: viewRoot.backtestVm.resultsLoading ? viewRoot.backtestVm.resultsLoadingText : viewRoot.backtestVm.statusText
                color: viewRoot.backtestVm.resultsLoading ? viewRoot.accentColor : viewRoot.mutedTextColor
                font.pixelSize: 12
                elide: Text.ElideRight
                Layout.fillWidth: true
            }
        }
    }
}
