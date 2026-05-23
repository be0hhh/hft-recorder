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
    property color warnColor: "#f0b35a"
    property color badColor: "#ef6f6c"

    function syncSelections() {
        sessionBox.currentIndex = sessionBox.indexOfValue(root.backtestVm.selectedSessionId)
        strategyBox.currentIndex = strategyBox.indexOfValue(root.backtestVm.selectedStrategy)
    }

    function useCaptureSession() {
        if (root.captureVm.sessionPath !== "") root.backtestVm.sessionPath = root.captureVm.sessionPath
    }

    background: Rectangle { color: root.windowColor }

    Connections {
        target: root.backtestVm
        function onSessionsChanged() { root.syncSelections() }
        function onSelectedSessionChanged() { root.syncSelections() }
        function onSelectedStrategyChanged() { root.syncSelections() }
    }

    component ActionButton: Rectangle {
        property string text: ""
        property bool enabledValue: true
        property color accent: root.accentColor
        signal clicked()
        radius: 6
        implicitWidth: Math.max(92, label.implicitWidth + 18)
        implicitHeight: 30
        color: enabledValue ? (mouse.containsMouse ? "#2b303a" : root.panelColor) : root.panelDeepColor
        border.color: enabledValue ? accent : root.borderColor
        border.width: 1
        opacity: enabledValue ? 1.0 : 0.5
        Text { id: label; anchors.centerIn: parent; width: parent.width - 12; text: parent.text; color: enabledValue ? root.textColor : root.mutedTextColor; font.pixelSize: 11; font.bold: true; elide: Text.ElideRight; horizontalAlignment: Text.AlignHCenter }
        MouseArea { id: mouse; anchors.fill: parent; hoverEnabled: true; enabled: parent.enabledValue; onClicked: parent.clicked() }
    }

    component MetricCard: Rectangle {
        property string title: ""
        property string value: ""
        property string subtext: ""
        property color accent: root.accentColor
        Layout.fillWidth: true
        Layout.preferredHeight: 58
        radius: 8
        color: root.panelColor
        border.color: root.borderColor
        border.width: 1
        Rectangle { width: 3; radius: 2; color: accent; anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom }
        ColumnLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 10
            anchors.topMargin: 6
            anchors.bottomMargin: 6
            spacing: 3
            Label { text: title; color: root.mutedTextColor; font.pixelSize: 9 }
            Label { Layout.fillWidth: true; text: value; color: root.textColor; font.pixelSize: 15; font.bold: true; elide: Text.ElideRight }
            Label { Layout.fillWidth: true; text: subtext; color: root.mutedTextColor; font.pixelSize: 9; elide: Text.ElideRight }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 108
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
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        Label { text: "Backtests"; color: root.textColor; font.pixelSize: 20; font.bold: true }
                        Label { text: root.backtestVm.statusText; color: root.mutedTextColor; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true }
                    }
                    ActionButton { text: "Refresh"; onClicked: { root.backtestVm.reloadSessions(); root.backtestVm.refreshResults() } }
                    ActionButton { text: "Use capture"; enabledValue: root.captureVm.sessionPath !== ""; onClicked: root.useCaptureSession() }
                }

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
                    RecorderComboBox {
                        id: strategyBox
                        Layout.preferredWidth: 280
                        caption: "Strategy"
                        textRole: "label"
                        valueRole: "id"
                        model: root.backtestVm.strategyChoices
                        popupWidth: 300
                        onActivated: root.backtestVm.setSelectedStrategy(currentValue)
                        Component.onCompleted: root.syncSelections()
                    }
                    ActionButton { text: root.backtestVm.running ? "Running" : "Start"; enabledValue: root.backtestVm.canRun; accent: root.goodColor; onClicked: root.backtestVm.startBacktest() }
                    ActionButton { text: "Cancel"; enabledValue: root.backtestVm.running; accent: root.badColor; onClicked: root.backtestVm.cancelBacktest() }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 44
            color: root.panelDeepColor
            border.color: root.borderColor
            border.width: 1
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                spacing: 10
                ProgressBar { Layout.preferredWidth: 260; from: 0; to: 100; value: root.backtestVm.progressPercent }
                Label { text: root.backtestVm.progressPercent + "%"; color: root.textColor; font.bold: true; font.pixelSize: 12; Layout.preferredWidth: 46 }
                Label { text: root.backtestVm.progressText; color: root.mutedTextColor; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true }
                Label { text: root.backtestVm.backtestsDirectory; color: root.mutedTextColor; font.pixelSize: 11; elide: Text.ElideLeft; Layout.maximumWidth: 520 }
            }
        }

        GridLayout {
            Layout.fillWidth: true
            Layout.margins: 12
            columns: 4
            columnSpacing: 10
            rowSpacing: 10
            MetricCard { title: "Runs"; value: root.backtestVm.runCount; subtext: "stored results"; accent: root.accentColor }
            MetricCard { title: "Strategy"; value: root.backtestVm.selectedStrategy; subtext: "skeleton parameter"; accent: root.warnColor }
            MetricCard { title: "Selected"; value: root.backtestVm.selectedRunId === "" ? "none" : root.backtestVm.selectedRunId; subtext: root.backtestVm.hasSelection ? "loaded" : "no result"; accent: root.goodColor }
            MetricCard { title: "Errors"; value: root.backtestVm.selectedErrorText === "" ? "0" : "see details"; subtext: root.backtestVm.selectedErrorText; accent: root.backtestVm.selectedErrorText === "" ? root.goodColor : root.badColor }
        }

        SplitView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.leftMargin: 12
            Layout.rightMargin: 12
            Layout.bottomMargin: 12
            orientation: Qt.Horizontal

            Rectangle {
                SplitView.preferredWidth: 420
                SplitView.minimumWidth: 280
                color: root.panelColor
                border.color: root.borderColor
                radius: 8

                ListView {
                    anchors.fill: parent
                    anchors.margins: 8
                    clip: true
                    model: root.backtestVm.runs
                    delegate: Rectangle {
                        required property var modelData
                        width: ListView.view.width
                        height: 58
                        radius: 6
                        color: modelData.runId === root.backtestVm.selectedRunId ? Qt.rgba(root.accentColor.r, root.accentColor.g, root.accentColor.b, 0.16) : root.panelDeepColor
                        border.color: modelData.runId === root.backtestVm.selectedRunId ? root.accentColor : root.borderColor
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 9
                            anchors.rightMargin: 9
                            spacing: 8
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 2
                                Label { text: modelData.runId; color: root.textColor; font.pixelSize: 12; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                                Label { text: modelData.strategy + " / " + modelData.status; color: root.mutedTextColor; font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true }
                                Label { text: modelData.modifiedText; color: root.mutedTextColor; font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true }
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
                radius: 8

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8
                    Label { text: "Summary"; color: root.textColor; font.pixelSize: 15; font.bold: true }
                    TextArea {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 180
                        text: root.backtestVm.selectedSummaryJson
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.NoWrap
                        color: root.textColor
                        font.family: "monospace"
                        font.pixelSize: 11
                        background: Rectangle { color: root.panelDeepColor; border.color: root.borderColor; radius: 6 }
                    }
                    Label { text: "Raw result"; color: root.textColor; font.pixelSize: 15; font.bold: true }
                    TextArea {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        text: root.backtestVm.selectedJson
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.NoWrap
                        color: root.textColor
                        font.family: "monospace"
                        font.pixelSize: 11
                        background: Rectangle { color: root.panelDeepColor; border.color: root.borderColor; radius: 6 }
                    }
                }
            }
        }
    }
}
