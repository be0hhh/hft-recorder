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

    function syncSelections() {
        sessionBox.currentIndex = sessionBox.indexOfValue(root.backtestVm.selectedSessionId)
        strategyBox.currentIndex = strategyBox.indexOfValue(root.backtestVm.selectedStrategy)
        configModeBox.currentIndex = configModeBox.indexOfValue(root.backtestVm.configMode)
    }

    background: Rectangle { color: root.windowColor }

    Connections {
        target: root.backtestVm
        function onSessionsChanged() { root.syncSelections() }
        function onSelectedSessionChanged() { root.syncSelections() }
        function onSymbolChanged() { symbolField.text = root.backtestVm.selectedSymbol }
        function onSelectedStrategyChanged() { root.syncSelections() }
        function onConfigChanged() { root.syncSelections() }
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
                        popupWidth: 320
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
                        caption: "Ping us"
                        fieldWidth: 92
                        text: root.backtestVm.pingLatencyUs
                        onEdited: function(value) { root.backtestVm.pingLatencyUs = value }
                    }
                    Item { Layout.fillWidth: true }
                    ActionButton { text: "Refresh"; onClicked: { root.backtestVm.reloadSessions(); root.backtestVm.refreshResults() } }
                    ActionButton { text: root.backtestVm.running ? "Running" : "Start"; enabledValue: root.backtestVm.canRun; accent: root.goodColor; onClicked: root.backtestVm.startBacktest() }
                    ActionButton { visible: root.backtestVm.running; text: "Cancel"; enabledValue: root.backtestVm.running; accent: root.badColor; onClicked: root.backtestVm.cancelBacktest() }
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
            Layout.fillWidth: true
            Layout.preferredHeight: 116
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            color: root.panelColor
            border.color: root.borderColor
            radius: 6

            GridView {
                anchors.fill: parent
                anchors.margins: 8
                clip: true
                cellWidth: 244
                cellHeight: 46
                model: root.backtestVm.strategyParameters
                delegate: Item {
                    required property var modelData
                    width: GridView.view.cellWidth
                    height: GridView.view.cellHeight
                    RowLayout {
                        anchors.fill: parent
                        anchors.rightMargin: 10
                        spacing: 6
                        Label { text: modelData.label; color: root.textColor; font.pixelSize: 11; elide: Text.ElideRight; Layout.preferredWidth: 112 }
                        TextField {
                            Layout.preferredWidth: 88
                            Layout.preferredHeight: 26
                            text: modelData.value
                            selectByMouse: true
                            color: root.textColor
                            font.pixelSize: 12
                            background: Rectangle { color: root.panelDeepColor; border.color: root.borderColor; radius: 5 }
                            onEditingFinished: root.backtestVm.setStrategyParameter(modelData.key, text)
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
                        Label { text: "Summary"; color: root.textColor; font.pixelSize: 15; font.bold: true; Layout.fillWidth: true }
                        Label { text: root.backtestVm.selectedErrorText; visible: text !== ""; color: root.badColor; font.pixelSize: 11; elide: Text.ElideRight; Layout.maximumWidth: 420 }
                    }
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
                    CheckBox {
                        id: rawToggle
                        text: "Raw"
                        checked: false
                        visible: false
                        contentItem: Text { text: rawToggle.text; color: root.mutedTextColor; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; leftPadding: rawToggle.indicator.width + rawToggle.spacing }
                    }
                    TextArea {
                        visible: false
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        text: root.backtestVm.selectedJson
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.NoWrap
                        color: root.textColor
                        font.family: "monospace"
                        font.pixelSize: 11
                        background: Rectangle { color: root.panelDeepColor; border.color: root.borderColor; radius: 5 }
                    }
                }
            }
        }
    }
}
