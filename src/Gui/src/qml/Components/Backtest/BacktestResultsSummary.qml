import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HftRecorder 1.0

Rectangle {
    id: summary
    required property var viewRoot

    SplitView.fillWidth: true
    color: viewRoot.panelColor
    border.color: viewRoot.borderColor
    radius: 6

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            Label { text: viewRoot.backtestVm.selectedIsSweep ? "Sweep" : "Summary"; color: viewRoot.textColor; font.pixelSize: 15; font.bold: true; Layout.fillWidth: true }
            RecorderComboBox {
                visible: viewRoot.backtestVm.selectedIsSweep && viewRoot.backtestVm.selectedDetailsLoaded
                Layout.preferredWidth: 158
                caption: "View"
                textRole: "label"
                valueRole: "id"
                model: viewRoot.backtestVm.sweepViewChoices
                popupWidth: 180
                Component.onCompleted: currentIndex = indexOfValue(viewRoot.backtestVm.selectedSweepView)
                onActivated: viewRoot.backtestVm.setSelectedSweepView(currentValue)
            }
            RecorderComboBox {
                visible: viewRoot.backtestVm.selectedIsSweep && viewRoot.backtestVm.selectedDetailsLoaded && viewRoot.backtestVm.selectedSweepView === "curves"
                Layout.preferredWidth: 132
                caption: "Curves"
                textRole: "label"
                valueRole: "id"
                model: viewRoot.backtestVm.sweepCurveLimitChoices
                popupWidth: 140
                Component.onCompleted: currentIndex = indexOfValue(viewRoot.backtestVm.selectedSweepCurveLimit)
                onActivated: viewRoot.backtestVm.setSelectedSweepCurveLimit(currentValue)
            }
            RecorderComboBox {
                visible: viewRoot.backtestVm.selectedIsSweep && viewRoot.backtestVm.selectedDetailsLoaded
                Layout.preferredWidth: 170
                caption: "Metric"
                textRole: "label"
                valueRole: "id"
                model: viewRoot.backtestVm.sweepMetricChoices
                popupWidth: 220
                Component.onCompleted: currentIndex = indexOfValue(viewRoot.backtestVm.selectedSweepMetric)
                onActivated: viewRoot.backtestVm.setSelectedSweepMetric(currentValue)
            }
            RecorderComboBox {
                visible: viewRoot.backtestVm.selectedIsSweep && viewRoot.backtestVm.selectedDetailsLoaded && viewRoot.backtestVm.selectedSweepView === "distribution"
                Layout.preferredWidth: 180
                caption: "Parameter"
                textRole: "label"
                valueRole: "id"
                model: viewRoot.backtestVm.selectedSweepDistributionParamChoices
                popupWidth: 220
                Component.onCompleted: currentIndex = indexOfValue(viewRoot.backtestVm.selectedSweepDistributionParam)
                onActivated: viewRoot.backtestVm.setSelectedSweepDistributionParam(currentValue)
            }
            BacktestActionButton {
                text: viewRoot.backtestVm.selectedDetailsLoading ? "Loading" : (viewRoot.hasSelectedVisualData() ? "Visual loaded" : (viewRoot.backtestVm.selectedDetailsLoaded ? "Reload visual" : "Load visual"))
                visible: viewRoot.backtestVm.hasSelection
                enabledValue: !viewRoot.backtestVm.selectedDetailsLoading && !viewRoot.hasSelectedVisualData()
                accent: viewRoot.goodColor
                onClicked: viewRoot.loadOrReloadVisual()
            }
            BacktestActionButton { text: viewRoot.sweepPercentMode ? "PnL %" : "PnL $"; visible: viewRoot.backtestVm.selectedIsSweep && viewRoot.backtestVm.selectedDetailsLoaded; enabledValue: viewRoot.backtestVm.selectedInitialBalanceE8 > 0; onClicked: { viewRoot.sweepPercentMode = !viewRoot.sweepPercentMode; sweepPanel.requestPaint() } }
            BacktestActionButton { text: "Apply"; visible: viewRoot.backtestVm.selectedIsSweep; enabledValue: viewRoot.backtestVm.selectedDetailsLoaded && viewRoot.selectedSweepPointId >= 0 && !viewRoot.backtestVm.running; onClicked: viewRoot.backtestVm.applySweepPointById(viewRoot.selectedSweepPointId) }
            BacktestActionButton { text: "Detailed run"; visible: viewRoot.backtestVm.selectedIsSweep; enabledValue: viewRoot.backtestVm.selectedDetailsLoaded && viewRoot.selectedSweepPointId >= 0 && !viewRoot.backtestVm.running; accent: viewRoot.goodColor; onClicked: viewRoot.backtestVm.startDetailedRunFromSweepPointById(viewRoot.selectedSweepPointId) }
            BacktestActionButton { text: "Delete"; visible: viewRoot.backtestVm.hasSelection; enabledValue: viewRoot.backtestVm.hasSelection && !viewRoot.backtestVm.running; accent: viewRoot.badColor; onClicked: viewRoot.backtestVm.deleteSelectedRun() }
            Label {
                text: viewRoot.selectedErrorDisplayText()
                visible: text !== ""
                color: viewRoot.badColor
                font.pixelSize: 11
                elide: Text.ElideRight
                Layout.maximumWidth: 360
            }
        }

        Rectangle {
            visible: viewRoot.backtestVm.hasSelection
            Layout.fillWidth: true
            Layout.preferredHeight: 52
            color: viewRoot.panelDeepColor
            border.color: viewRoot.borderColor
            radius: 6
            RowLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 12
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Label { text: "Backtest parameters"; color: viewRoot.textColor; font.pixelSize: 13; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                    Label { text: viewRoot.backtestVm.selectedConfigText || viewRoot.backtestVm.selectedRunId; color: viewRoot.mutedTextColor; font.pixelSize: 11; Layout.fillWidth: true; elide: Text.ElideRight }
                }
                Label {
                    text: viewRoot.hasSelectedVisualData() ? "Visual loaded" : (viewRoot.backtestVm.selectedDetailsLoading ? "Loading visual" : (viewRoot.backtestVm.selectedPreviewLoading ? "Loading preview" : (viewRoot.backtestVm.selectedDetailsLoaded ? "Visual data missing" : "Visual optional")))
                    color: viewRoot.mutedTextColor
                    font.pixelSize: 11
                }
            }
        }

        BacktestMessagePanel {
            title: "Errors"
            message: viewRoot.selectedErrorDisplayText()
            panelColor: "#2c1f22"
            borderColor: viewRoot.badColor
            titleColor: viewRoot.badColor
        }

        BacktestMessagePanel {
            title: "Warnings"
            message: viewRoot.backtestVm.selectedWarningText
            panelColor: "#2a251b"
            borderColor: "#8f6b2d"
            titleColor: "#f0b35a"
        }

        ColumnLayout {
            visible: viewRoot.backtestVm.selectedPerformanceRows.length > 0
            Layout.fillWidth: true
            spacing: 6
            Label {
                text: "Performance"
                color: viewRoot.textColor
                font.pixelSize: 13
                font.bold: true
            }
            Flow {
                Layout.fillWidth: true
                Layout.preferredHeight: childrenRect.height
                spacing: 8
                Repeater {
                    model: viewRoot.backtestVm.selectedPerformanceRows
                    delegate: BacktestMetricCard {
                        metric: ({ "group": "Stage", "label": modelData.label, "value": modelData.value })
                    }
                }
            }
        }

        ColumnLayout {
            visible: viewRoot.backtestVm.selectedDepthExecutionRows.length > 0
            Layout.fillWidth: true
            spacing: 6
            Label {
                text: "Execution data"
                color: viewRoot.textColor
                font.pixelSize: 13
                font.bold: true
            }
            Flow {
                Layout.fillWidth: true
                Layout.preferredHeight: childrenRect.height
                spacing: 8
                Repeater {
                    model: viewRoot.backtestVm.selectedDepthExecutionRows
                    delegate: BacktestMetricCard {
                        metric: ({ "group": "Depth / BBO", "label": modelData.label, "value": modelData.value })
                    }
                }
            }
        }

        ColumnLayout {
            visible: !viewRoot.backtestVm.selectedIsSweep
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                Label { text: "Run metrics"; color: viewRoot.textColor; font.pixelSize: 14; font.bold: true; Layout.fillWidth: true }
                RecorderComboBox {
                    id: resultScopeBox
                    Layout.preferredWidth: 220
                    caption: "Scope"
                    textRole: "label"
                    valueRole: "id"
                    model: viewRoot.backtestVm.resultScopeChoices
                    popupWidth: 280
                    visible: viewRoot.backtestVm.resultScopeChoices.length > 1
                    enabled: viewRoot.backtestVm.resultScopeChoices.length > 1
                    Component.onCompleted: currentIndex = indexOfValue(viewRoot.backtestVm.selectedResultScope)
                    onActivated: viewRoot.backtestVm.setSelectedResultScope(currentValue)
                    Connections {
                        target: viewRoot.backtestVm
                        function onSelectedResultScopeChanged() { resultScopeBox.currentIndex = resultScopeBox.indexOfValue(viewRoot.backtestVm.selectedResultScope) }
                        function onSelectionChanged() { resultScopeBox.currentIndex = resultScopeBox.indexOfValue(viewRoot.backtestVm.selectedResultScope) }
                    }
                }
                Label {
                    text: viewRoot.backtestVm.selectedDetailsLoading ? "Loading visual data..." : (viewRoot.backtestVm.selectedPreviewLoading ? "Loading PnL preview..." : (!viewRoot.backtestVm.selectedDetailsLoaded ? "Load visual to open chart" : (viewRoot.backtestVm.selectedResultMetrics.length > 0 ? "" : "Selected run has no metrics")))
                    color: viewRoot.mutedTextColor
                    font.pixelSize: 11
                    visible: text !== ""
                }
            }
            Flow {
                visible: viewRoot.backtestVm.selectedResultMetrics.length > 0
                Layout.fillWidth: true
                Layout.preferredHeight: childrenRect.height
                spacing: 8
                Repeater {
                    model: viewRoot.backtestVm.selectedResultMetrics
                    delegate: BacktestMetricCard {
                        metric: modelData
                        selected: modelData.key === viewRoot.backtestVm.selectedResultMetricKey
                        onClicked: function(key) { viewRoot.backtestVm.setSelectedResultMetricKey(key) }
                    }
                }
            }
            Rectangle {
                visible: !viewRoot.backtestVm.selectedDetailsLoaded
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 220
                color: viewRoot.panelDeepColor
                border.color: viewRoot.borderColor
                radius: 6
                Label {
                    anchors.centerIn: parent
                    text: viewRoot.backtestVm.selectedDetailsLoading ? "Loading visual data..." : (viewRoot.backtestVm.selectedPreviewLoading ? "Loading PnL preview..." : "Load visual to open chart.")
                    color: viewRoot.mutedTextColor
                    font.pixelSize: 12
                }
            }
            Rectangle {
                visible: viewRoot.backtestVm.selectedDetailsLoaded && viewRoot.backtestVm.selectedResultMetrics.length > 0
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.minimumHeight: 300
                color: viewRoot.panelDeepColor
                border.color: viewRoot.borderColor
                radius: 6
                clip: true

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        Label { text: viewRoot.selectedMetricField("label"); color: viewRoot.textColor; font.pixelSize: 14; font.bold: true; Layout.fillWidth: true; elide: Text.ElideRight }
                        Label { text: viewRoot.selectedMetricField("value"); color: viewRoot.textColor; font.pixelSize: 14; font.bold: true }
                        RecorderComboBox {
                            id: metricRatioBox
                            Layout.preferredWidth: 172
                            caption: "Divide by"
                            textRole: "label"
                            valueRole: "id"
                            model: viewRoot.backtestVm.resultMetricRatioChoices
                            popupWidth: 190
                            Component.onCompleted: currentIndex = indexOfValue(viewRoot.backtestVm.selectedResultMetricRatioKey)
                            onActivated: viewRoot.backtestVm.setSelectedResultMetricRatioKey(currentValue)
                            Connections {
                                target: viewRoot.backtestVm
                                function onSelectedResultMetricChanged() { metricRatioBox.currentIndex = metricRatioBox.indexOfValue(viewRoot.backtestVm.selectedResultMetricRatioKey) }
                                function onSelectionChanged() { metricRatioBox.currentIndex = metricRatioBox.indexOfValue(viewRoot.backtestVm.selectedResultMetricRatioKey) }
                            }
                        }
                    }

                    GridLayout {
                        Layout.fillWidth: true
                        columns: 3
                        columnSpacing: 10
                        rowSpacing: 4
                        Label { text: "Что это"; color: viewRoot.mutedTextColor; font.pixelSize: 10; Layout.fillWidth: true }
                        Label { text: "Зачем смотреть"; color: viewRoot.mutedTextColor; font.pixelSize: 10; Layout.fillWidth: true }
                        Label { text: "Как читать"; color: viewRoot.mutedTextColor; font.pixelSize: 10; Layout.fillWidth: true }
                        Label { text: viewRoot.selectedMetricField("description"); color: viewRoot.textColor; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        Label { text: viewRoot.selectedMetricField("why"); color: viewRoot.textColor; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                        Label { text: viewRoot.selectedMetricField("interpretation"); color: viewRoot.textColor; font.pixelSize: 11; wrapMode: Text.WordWrap; Layout.fillWidth: true }
                    }

                    BacktestResultsMetricChart { viewRoot: summary.viewRoot }
                }
            }
            ColumnLayout {
                visible: viewRoot.backtestVm.selectedExecutionQualityPoints.length > 0
                Layout.fillWidth: true
                Layout.preferredHeight: 340
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    Label { text: "Execution edge: strategy action result"; color: viewRoot.textColor; font.pixelSize: 14; font.bold: true; Layout.fillWidth: true }
                    Label { text: "orange boundary  /  cyan at action  /  green actual VWAP"; color: viewRoot.mutedTextColor; font.pixelSize: 10 }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 8
                    BacktestExecutionEdgeChart { viewRoot: summary.viewRoot; entrySide: true; chartTitle: "Entry / Add"; Layout.fillWidth: true; Layout.fillHeight: true }
                    BacktestExecutionEdgeChart { viewRoot: summary.viewRoot; entrySide: false; chartTitle: "Exit / Reduce"; Layout.fillWidth: true; Layout.fillHeight: true }
                }
                Rectangle {
                    visible: viewRoot.incompleteExecutionPoints().length > 0
                    Layout.fillWidth: true
                    Layout.preferredHeight: 74
                    color: viewRoot.panelDeepColor
                    border.color: viewRoot.badColor
                    radius: 5
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 2
                        Label { text: "Incomplete / rejected actions"; color: viewRoot.badColor; font.pixelSize: 11; font.bold: true }
                        Flickable {
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            contentWidth: incompleteRow.implicitWidth
                            contentHeight: incompleteRow.implicitHeight
                            clip: true
                            Row {
                                id: incompleteRow
                                spacing: 14
                                Repeater {
                                    model: viewRoot.incompleteExecutionPoints()
                                    delegate: Label {
                                        text: "#" + modelData.actionId + "  phase=" + modelData.phase + "  status=" + modelData.status + "  filled edge=" + viewRoot.e8Text(modelData.fillEdgeBpsE8)
                                        color: viewRoot.textColor
                                        font.family: "monospace"
                                        font.pixelSize: 10
                                    }
                                }
                            }
                        }
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                BacktestActionButton { text: viewRoot.showRawSummary ? "Hide details" : "Details"; onClicked: viewRoot.showRawSummary = !viewRoot.showRawSummary }
                Label { text: "Raw summary JSON"; color: viewRoot.mutedTextColor; font.pixelSize: 11; visible: viewRoot.showRawSummary }
            }
            TextArea {
                visible: viewRoot.showRawSummary
                Layout.fillWidth: true
                Layout.preferredHeight: 180
                text: viewRoot.backtestVm.selectedSummaryJson
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.NoWrap
                color: viewRoot.textColor
                font.family: "monospace"
                font.pixelSize: 11
                background: Rectangle { color: viewRoot.panelDeepColor; border.color: viewRoot.borderColor; radius: 5 }
            }
        }

        BacktestResultsSweep { id: sweepPanel; viewRoot: summary.viewRoot }
    }
}
