import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    id: root
    required property var tradingVm
    required property bool tabActive

    property color bg: "#111216"
    property color panel: "#1b1d23"
    property color soft: "#22252d"
    property color deep: "#15171c"
    property color line: "#343844"
    property color text: "#f1f4f8"
    property color muted: "#a8afbd"
    property color cyan: "#24c2cb"
    property color green: "#82d46b"
    property color amber: "#f0b35a"
    property color red: "#ef6f6c"

    background: Rectangle { color: root.bg }

    Timer { interval: 16; repeat: true; running: root.tabActive; onTriggered: root.tradingVm.refreshSnapshot() }

    component Pill: Rectangle {
        property string title: ""
        property string value: ""
        property color accent: root.cyan
        Layout.fillWidth: true
        Layout.preferredHeight: 44
        radius: 7
        color: root.soft
        border.color: root.line
        border.width: 1
        Rectangle { width: 3; color: accent; anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom }
        ColumnLayout {
            anchors.fill: parent
            anchors.leftMargin: 11
            anchors.rightMargin: 8
            anchors.topMargin: 5
            anchors.bottomMargin: 5
            spacing: 1
            Label { text: title; color: root.muted; font.pixelSize: 10 }
            Label { Layout.fillWidth: true; text: value; color: root.text; font.pixelSize: 13; font.bold: true; elide: Text.ElideRight }
        }
    }

    component Box: Rectangle {
        default property alias content: slot.data
        property string title: ""
        property string headerDetail: ""
        Layout.fillWidth: true
        implicitHeight: slot.implicitHeight + 43
        radius: 8
        color: root.panel
        border.color: root.line
        border.width: 1
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                Label { text: title; color: root.text; font.pixelSize: 15; font.bold: true }
                Item { Layout.fillWidth: true }
                Label { text: headerDetail; color: root.muted; font.pixelSize: 11; elide: Text.ElideRight }
            }
            ColumnLayout { id: slot; Layout.fillWidth: true; spacing: 8 }
        }
    }

    component Btn: Rectangle {
        property string text: ""
        property bool enabledValue: true
        property color accent: root.cyan
        signal clicked()
        radius: 6
        implicitWidth: Math.max(88, label.implicitWidth + 18)
        implicitHeight: 30
        color: enabledValue ? (mouse.containsMouse ? "#2b303a" : root.soft) : root.deep
        border.color: enabledValue ? accent : root.line
        border.width: 1
        opacity: enabledValue ? 1.0 : 0.48
        Text { id: label; anchors.centerIn: parent; width: parent.width - 12; text: parent.text; color: enabledValue ? root.text : root.muted; font.pixelSize: 11; font.bold: true; elide: Text.ElideRight; horizontalAlignment: Text.AlignHCenter }
        MouseArea { id: mouse; anchors.fill: parent; hoverEnabled: true; enabled: parent.enabledValue; onClicked: parent.clicked() }
    }

    component Field: TextField {
        property string caption: ""
        Layout.fillWidth: true
        Layout.preferredHeight: 38
        color: root.text
        selectedTextColor: root.deep
        selectionColor: root.cyan
        font.pixelSize: 12
        leftPadding: 10
        rightPadding: 10
        topPadding: 15
        placeholderTextColor: root.muted
        background: Rectangle { radius: 7; color: root.deep; border.color: parent.activeFocus ? root.cyan : root.line; border.width: 1 }
        Label { text: parent.caption; color: root.muted; font.pixelSize: 9; x: 10; y: 4 }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: root.width - 24
            x: 12
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 8
                spacing: 10
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Label { text: "Binance Futures"; color: root.text; font.pixelSize: 20; font.bold: true }
                    Label { text: root.tradingVm.statusText; color: root.muted; font.pixelSize: 12; elide: Text.ElideRight; Layout.fillWidth: true }
                }
                Pill { Layout.preferredWidth: 110; title: "CXET"; value: root.tradingVm.cxetAvailable ? "linked" : "off"; accent: root.tradingVm.cxetAvailable ? root.green : root.red }
                Pill { Layout.preferredWidth: 120; title: "L1"; value: root.tradingVm.bookTickerRunning ? "running" : "stopped"; accent: root.tradingVm.bookTickerRunning ? root.green : root.amber }
                Pill { Layout.preferredWidth: 120; title: "User"; value: root.tradingVm.userStreamRunning ? "running" : "stopped"; accent: root.tradingVm.userStreamRunning ? root.green : root.amber }
                Pill { Layout.preferredWidth: 120; title: "Orders"; value: root.tradingVm.orderSessionRunning ? "warm" : "stopped"; accent: root.tradingVm.orderSessionRunning ? root.green : root.amber }
            }

            Box {
                title: "Config"
                headerDetail: root.tradingVm.symbol + " / API " + root.tradingVm.apiSlot
                GridLayout {
                    Layout.fillWidth: true
                    columns: 8
                    columnSpacing: 8
                    rowSpacing: 8
                    Field { id: symbolField; Layout.columnSpan: 2; caption: "Symbol"; text: root.tradingVm.symbol; onEditingFinished: root.tradingVm.setSymbol(text) }
                    SpinBox {
                        id: apiSlotBox
                        Layout.preferredWidth: 86
                        Layout.preferredHeight: 38
                        from: 1
                        to: 16
                        value: root.tradingVm.apiSlot
                        onValueModified: root.tradingVm.setApiSlot(value)
                        background: Rectangle { radius: 7; color: root.deep; border.color: root.line }
                        contentItem: TextInput { text: apiSlotBox.textFromValue(apiSlotBox.value, apiSlotBox.locale); color: root.text; horizontalAlignment: Qt.AlignHCenter; verticalAlignment: Qt.AlignVCenter; readOnly: true }
                    }
                    Btn { text: "Fetch info"; enabledValue: root.tradingVm.cxetAvailable && !root.tradingVm.busy; onClicked: { root.tradingVm.setSymbol(symbolField.text); root.tradingVm.fetchInstrumentInfo() } }
                    Btn { text: "Refresh cfg"; enabledValue: root.tradingVm.cxetAvailable && !root.tradingVm.busy; onClicked: root.tradingVm.refreshSymbolConfig() }
                    SpinBox {
                        id: leverageBox
                        Layout.preferredWidth: 92
                        Layout.preferredHeight: 38
                        from: 1
                        to: 150
                        value: Number(root.tradingVm.leverageText) > 0 ? Number(root.tradingVm.leverageText) : 1
                        background: Rectangle { radius: 7; color: root.deep; border.color: root.line }
                        contentItem: TextInput { text: leverageBox.textFromValue(leverageBox.value, leverageBox.locale) + "x"; color: root.text; horizontalAlignment: Qt.AlignHCenter; verticalAlignment: Qt.AlignVCenter; readOnly: true }
                    }
                    Btn { text: "Set lev"; enabledValue: root.tradingVm.cxetAvailable && !root.tradingVm.busy; onClicked: root.tradingVm.setLeverage(leverageBox.value) }
                    ComboBox { id: marginBox; Layout.preferredWidth: 124; Layout.preferredHeight: 38; model: ["CROSSED", "ISOLATED"]; currentIndex: root.tradingVm.marginTypeText === "ISOLATED" ? 1 : 0; onActivated: root.tradingVm.setMarginType(currentText) }
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 5
                    columnSpacing: 8
                    Pill { title: "Tick"; value: root.tradingVm.instrumentInfoText; accent: root.tradingVm.instrumentInfoLoaded ? root.green : root.amber }
                    Pill { title: "Margin"; value: root.tradingVm.marginTypeText; accent: root.cyan }
                    Pill { title: "Leverage"; value: root.tradingVm.leverageText + "x"; accent: root.amber }
                    Pill { title: "Max notional"; value: root.tradingVm.maxNotionalText; accent: root.cyan }
                    Pill { title: "Busy"; value: root.tradingVm.busy ? "yes" : "no"; accent: root.tradingVm.busy ? root.amber : root.green }
                }
            }

            Box {
                title: "L1"
                headerDetail: root.tradingVm.lastBookTickerText
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Btn { text: "Start L1"; enabledValue: root.tradingVm.cxetAvailable && !root.tradingVm.bookTickerRunning; onClicked: root.tradingVm.startBookTicker() }
                    Btn { text: "Stop L1"; accent: root.amber; enabledValue: root.tradingVm.bookTickerRunning; onClicked: root.tradingVm.stopBookTicker() }
                    Item { Layout.fillWidth: true }
                    Label { text: "spread " + root.tradingVm.spreadText; color: root.muted; font.pixelSize: 12 }
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 3
                    columnSpacing: 8
                    Pill { title: "Bid"; value: root.tradingVm.bidPriceText + " x " + root.tradingVm.bidQtyText; accent: root.green }
                    Pill { title: "Ask"; value: root.tradingVm.askPriceText + " x " + root.tradingVm.askQtyText; accent: root.red }
                    Pill { title: "Spread"; value: root.tradingVm.spreadText; accent: root.cyan }
                }
            }

            Box {
                title: "Private"
                headerDetail: root.tradingVm.lastUserEventText
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Btn { text: "Start user"; enabledValue: root.tradingVm.cxetAvailable && !root.tradingVm.userStreamRunning; onClicked: root.tradingVm.startUserStream() }
                    Btn { text: "Stop user"; accent: root.amber; enabledValue: root.tradingVm.userStreamRunning; onClicked: root.tradingVm.stopUserStream() }
                    Btn { text: "Start orders"; enabledValue: root.tradingVm.cxetAvailable && !root.tradingVm.busy && !root.tradingVm.orderSessionRunning; onClicked: root.tradingVm.startOrderSession() }
                    Btn { text: "Stop orders"; accent: root.amber; enabledValue: root.tradingVm.orderSessionRunning && !root.tradingVm.busy; onClicked: root.tradingVm.stopOrderSession() }
                    Item { Layout.fillWidth: true }
                    Btn { text: "Hard stop"; accent: root.red; enabledValue: true; onClicked: root.tradingVm.hardStopAll() }
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 3
                    columnSpacing: 8
                    rowSpacing: 8
                    Pill { title: "Wallet"; value: root.tradingVm.walletBalanceText; accent: root.cyan }
                    Pill { title: "Available"; value: root.tradingVm.availableBalanceText; accent: root.green }
                    Pill { title: "Realized PnL"; value: root.tradingVm.realizedPnlText; accent: root.green }
                    Pill { title: "Open"; value: root.tradingVm.openOrderCountText; accent: root.cyan }
                    Pill { title: "Filled"; value: root.tradingVm.filledTradeCountText; accent: root.green }
                    Pill { title: "Canceled"; value: root.tradingVm.canceledOrderCountText; accent: root.amber }
                }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    Field { id: cancelField; Layout.fillWidth: true; caption: "Orig client id"; placeholderText: "client id to cancel" }
                    Btn { text: "Cancel"; accent: root.red; enabledValue: root.tradingVm.orderSessionRunning && !root.tradingVm.busy; onClicked: root.tradingVm.cancelOrder(cancelField.text) }
                    Pill { Layout.fillWidth: true; Layout.preferredWidth: 360; title: "Last event"; value: root.tradingVm.lastUserEventText; accent: root.cyan }
                }
            }

            Item { Layout.preferredHeight: 10 }
        }
    }
}
