import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: selector

    required property var backtestVm
    property color panelDeepColor: "#15171c"
    property color borderColor: "#343844"
    property color textColor: "#f1f4f8"
    property color mutedTextColor: "#a8afbd"
    property color accentColor: "#21c7d9"
    property color goodColor: "#8ee88f"
    property string legSearchText: ""
    property var popupLegRows: []
    property bool legPopupApplied: false

    implicitHeight: Math.min(330, Math.max(150, 118 + Math.min(5, backtestVm.selectedLegCount) * 36))

    function rowValue(row, key) {
        if (row === undefined || row === null || row[key] === undefined || row[key] === null) return ""
        return String(row[key])
    }

    function rowMatches(row) {
        var q = selector.legSearchText.trim().toLowerCase()
        if (q.length === 0) return true
        var text = [
            rowValue(row, "label"),
            rowValue(row, "exchange"),
            rowValue(row, "market"),
            rowValue(row, "symbol"),
            rowValue(row, "venue"),
            rowValue(row, "dataSummary")
        ].join(" ").toLowerCase()
        return text.indexOf(q) >= 0
    }

    function latencyText(row) {
        return "MD " + rowValue(row, "marketDataLatencyUs") + "/" + rowValue(row, "marketDataJitterUs")
            + " | Mkt " + rowValue(row, "marketOrderLatencyUs") + "/" + rowValue(row, "marketOrderJitterUs")
            + " | Lim " + rowValue(row, "limitOrderLatencyUs") + "/" + rowValue(row, "limitOrderJitterUs")
            + " | Cxl " + rowValue(row, "cancelOrderLatencyUs") + "/" + rowValue(row, "cancelOrderJitterUs")
            + " | User " + rowValue(row, "userDataLatencyUs") + "/" + rowValue(row, "userDataJitterUs")
    }

    function stagedCount() {
        var rows = selector.popupLegRows.length > 0 ? selector.popupLegRows : selector.backtestVm.legSelectionRows
        var count = 0
        for (var i = 0; i < rows.length; ++i) {
            if (rows[i].stagedEnabled !== false) ++count
        }
        return count
    }

    function cloneRow(row) {
        var out = ({})
        if (!row) return out
        for (var key in row)
            out[key] = row[key]
        return out
    }

    function cloneRows(rows) {
        var out = []
        if (!rows) return out
        for (var i = 0; i < rows.length; ++i)
            out.push(selector.cloneRow(rows[i]))
        return out
    }

    function setPopupRowStaged(path, enabled) {
        var rows = selector.cloneRows(selector.popupLegRows)
        for (var i = 0; i < rows.length; ++i) {
            if (String(rows[i].path || "") === String(path || "")) {
                rows[i].stagedEnabled = enabled
                break
            }
        }
        selector.popupLegRows = rows
    }

    function setAllPopupRowsStaged(enabled) {
        var rows = selector.cloneRows(selector.popupLegRows)
        for (var i = 0; i < rows.length; ++i)
            rows[i].stagedEnabled = enabled
        selector.popupLegRows = rows
    }

    function rowIsFutures(row) {
        var market = selector.rowValue(row, "market").trim().toLowerCase()
        return market === "futures"
            || market === "future"
            || market === "forts"
            || market === "linear"
            || market === "swap"
            || market === "perp"
            || market === "usdt"
            || market === "usdc"
            || market.indexOf("futures") === 0
    }

    function setFuturesPopupRowsStaged() {
        var rows = selector.cloneRows(selector.popupLegRows)
        for (var i = 0; i < rows.length; ++i)
            rows[i].stagedEnabled = selector.rowIsFutures(rows[i])
        selector.popupLegRows = rows
    }

    function commitPopupRowsToViewModel() {
        selector.backtestVm.setLegSelectionRowsStaged(selector.popupLegRows)
    }

    function openLegPopup() {
        selector.legPopupApplied = false
        selector.backtestVm.resetLegSelectionStaged()
        selector.popupLegRows = selector.cloneRows(selector.backtestVm.legSelectionRows)
        selector.legSearchText = ""
        legPopup.open()
    }

    component ColumnCaption: Label {
        property int columnWidth: 80
        color: selector.mutedTextColor
        font.pixelSize: 11
        elide: Text.ElideRight
        Layout.preferredWidth: columnWidth
    }

    Rectangle {
        anchors.fill: parent
        color: "transparent"

        ColumnLayout {
            anchors.fill: parent
            spacing: 6

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                BacktestActionButton {
                    text: "Select legs"
                    accent: selector.accentColor
                    textColor: selector.textColor
                    mutedTextColor: selector.mutedTextColor
                    panelDeepColor: selector.panelDeepColor
                    borderColor: selector.borderColor
                    onClicked: selector.openLegPopup()
                }

                Label {
                    text: selector.backtestVm.selectedLegSummary
                    color: selector.textColor
                    font.pixelSize: 12
                    font.bold: true
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                BacktestCompactField {
                    caption: "Ping us"
                    fieldWidth: 90
                    text: selector.backtestVm.pingLatencyUs
                    textColor: selector.textColor
                    mutedTextColor: selector.mutedTextColor
                    panelDeepColor: selector.panelDeepColor
                    borderColor: selector.borderColor
                    onEdited: function(value) { selector.backtestVm.pingLatencyUs = value }
                }

                BacktestCompactField {
                    caption: "Seed"
                    fieldWidth: 82
                    text: selector.backtestVm.latencySeed
                    textColor: selector.textColor
                    mutedTextColor: selector.mutedTextColor
                    panelDeepColor: selector.panelDeepColor
                    borderColor: selector.borderColor
                    onEdited: function(value) { selector.backtestVm.latencySeed = value }
                }
            }

            BacktestPrimaryTradeControls {
                Layout.fillWidth: true
                Layout.preferredHeight: 42
                backtestVm: selector.backtestVm
                panelColor: selector.panelDeepColor
                panelDeepColor: selector.panelDeepColor
                borderColor: selector.borderColor
                textColor: selector.textColor
                mutedTextColor: selector.mutedTextColor
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                ColumnCaption { text: "Leg"; columnWidth: 280 }
                ColumnCaption { text: "Balance"; columnWidth: 92 }
                ColumnCaption { text: "Latency"; columnWidth: 420; Layout.fillWidth: true; Layout.minimumWidth: 420 }
                ColumnCaption { text: "Fees"; columnWidth: 210 }
            }

            ListView {
                id: selectedLegList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: selector.backtestVm.selectedSessionLegs
                delegate: RowLayout {
                    required property var modelData

                    width: selectedLegList.width
                    height: modelData.enabled !== false ? 34 : 0
                    visible: modelData.enabled !== false
                    spacing: 8

                    Label {
                        text: (modelData.primary === true ? "Primary | " : "") + modelData.label
                        color: selector.textColor
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        Layout.preferredWidth: 280
                    }

                    TextField {
                        text: modelData.initialBalanceUsdt
                        selectByMouse: true
                        color: selector.textColor
                        font.pixelSize: 12
                        Layout.preferredWidth: 92
                        Layout.preferredHeight: 26
                        background: Rectangle { color: selector.panelDeepColor; border.color: selector.borderColor; radius: 5 }
                        onEditingFinished: selector.backtestVm.setVenueExecutionValue(modelData.index, "initial_balance_usdt", text)
                    }

                    BacktestActionButton {
                        text: selector.latencyText(modelData)
                        accent: selector.accentColor
                        textColor: selector.textColor
                        mutedTextColor: selector.mutedTextColor
                        panelColor: selector.panelDeepColor
                        panelDeepColor: selector.panelDeepColor
                        borderColor: selector.borderColor
                        Layout.fillWidth: true
                        Layout.minimumWidth: 420
                        onClicked: latencyPopup.openFor(modelData)
                    }

                    Label {
                        text: modelData.executionPresetSummary
                        color: selector.mutedTextColor
                        font.pixelSize: 11
                        elide: Text.ElideRight
                        Layout.preferredWidth: 210
                    }
                }
            }

            Label {
                visible: selector.backtestVm.selectedLegCount === 0
                text: "No legs selected"
                color: selector.mutedTextColor
                font.pixelSize: 12
                Layout.fillWidth: true
            }
        }
    }

    Popup {
        id: legPopup
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        width: Math.min(820, Math.max(520, selector.width - 24))
        height: 560
        x: Math.max(12, (selector.width - width) / 2)
        y: 24
        padding: 10
        background: Rectangle {
            color: "#1b1f27"
            border.color: selector.accentColor
            border.width: 1
            radius: 6
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Label {
                    text: "Select legs"
                    color: selector.textColor
                    font.pixelSize: 15
                    font.bold: true
                    Layout.fillWidth: true
                }

                Label {
                    text: selector.stagedCount() + " / " + selector.backtestVm.candidateLegCount
                    color: selector.mutedTextColor
                    font.pixelSize: 12
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                TextField {
                    id: searchField
                    placeholderText: "binance, bitget, kucoin..."
                    color: selector.textColor
                    placeholderTextColor: selector.mutedTextColor
                    selectByMouse: true
                    Layout.fillWidth: true
                    Layout.preferredHeight: 30
                    background: Rectangle { color: selector.panelDeepColor; border.color: selector.borderColor; radius: 5 }
                    onTextChanged: selector.legSearchText = text
                }

                BacktestActionButton {
                    text: "All"
                    onClicked: {
                        selector.setAllPopupRowsStaged(true)
                        selector.backtestVm.setAllSessionLegsSelectionStaged(true)
                    }
                }
                BacktestActionButton {
                    text: "None"
                    onClicked: {
                        selector.setAllPopupRowsStaged(false)
                        selector.backtestVm.setAllSessionLegsSelectionStaged(false)
                    }
                }
                BacktestActionButton {
                    text: "Futures"
                    onClicked: {
                        selector.setFuturesPopupRowsStaged()
                        selector.backtestVm.setFuturesSessionLegsSelectionStaged()
                    }
                }
            }

            ListView {
                id: legList
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                model: selector.popupLegRows
                spacing: 4
                delegate: Rectangle {
                    required property var modelData

                    width: legList.width
                    height: selector.rowMatches(modelData) ? 42 : 0
                    visible: height > 0
                    color: modelData.stagedEnabled === false ? "#151820" : "#20252d"
                    border.color: modelData.currentEnabled === false ? selector.borderColor : "#3d4554"
                    border.width: 1
                    radius: 5

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 6
                        spacing: 8

                        CheckBox {
                            checked: modelData.stagedEnabled !== false
                            Layout.preferredWidth: 36
                            Layout.preferredHeight: 24
                            onToggled: {
                                selector.setPopupRowStaged(modelData.path, checked)
                                selector.backtestVm.setSessionLegSelectionStaged(modelData.path, checked)
                            }
                        }

                        Label {
                            text: modelData.label
                            color: modelData.stagedEnabled === false ? selector.mutedTextColor : selector.textColor
                            font.pixelSize: 12
                            font.bold: modelData.primary === true
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }

                        Label {
                            text: modelData.exchange + " / " + modelData.market
                            color: selector.mutedTextColor
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            Layout.preferredWidth: 128
                        }

                        Label {
                            text: modelData.symbol
                            color: selector.mutedTextColor
                            font.pixelSize: 11
                            elide: Text.ElideRight
                            Layout.preferredWidth: 94
                        }

                        Label {
                            text: modelData.dataSummary || "BTK 0 | TRD 0"
                            color: selector.mutedTextColor
                            font.pixelSize: 11
                            horizontalAlignment: Text.AlignRight
                            elide: Text.ElideRight
                            Layout.preferredWidth: 150
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Item { Layout.fillWidth: true }
                BacktestActionButton {
                    text: "Cancel"
                    borderColor: selector.borderColor
                    onClicked: {
                        selector.legPopupApplied = false
                        legPopup.close()
                    }
                }
                BacktestActionButton {
                    text: "Apply"
                    accent: selector.goodColor
                    onClicked: {
                        selector.legPopupApplied = true
                        selector.commitPopupRowsToViewModel()
                        selector.backtestVm.applyLegSelection()
                        legPopup.close()
                    }
                }
            }
        }

        onOpened: {
            if (selector.popupLegRows.length === 0)
                selector.popupLegRows = selector.cloneRows(selector.backtestVm.legSelectionRows)
            searchField.text = ""
        }
        onClosed: {
            if (selector.legPopupApplied) {
                selector.backtestVm.resetLegSelectionStaged()
            } else {
                selector.backtestVm.cancelLegSelection()
            }
            selector.popupLegRows = []
            selector.legPopupApplied = false
        }
    }

    Popup {
        id: latencyPopup
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        property var rowData: ({})

        function openFor(row) {
            rowData = row
            open()
        }

        function value(key) {
            return selector.rowValue(rowData, key)
        }

        width: 460
        height: 360
        x: Math.max(12, (selector.width - width) / 2)
        y: 48
        padding: 10
        background: Rectangle {
            color: "#1b1f27"
            border.color: selector.borderColor
            border.width: 1
            radius: 6
        }

        ColumnLayout {
            anchors.fill: parent
            spacing: 8

            Label {
                text: latencyPopup.value("label")
                color: selector.textColor
                font.pixelSize: 14
                font.bold: true
                elide: Text.ElideRight
                Layout.fillWidth: true
            }

            GridLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                columns: 2
                columnSpacing: 8
                rowSpacing: 6

                Repeater {
                    model: [
                        { caption: "MD base", key: "marketDataLatencyUs", field: "market_data_latency_us" },
                        { caption: "MD jit", key: "marketDataJitterUs", field: "market_data_jitter_us" },
                        { caption: "Mkt base", key: "marketOrderLatencyUs", field: "market_order_latency_us" },
                        { caption: "Mkt jit", key: "marketOrderJitterUs", field: "market_order_jitter_us" },
                        { caption: "Limit base", key: "limitOrderLatencyUs", field: "limit_order_latency_us" },
                        { caption: "Limit jit", key: "limitOrderJitterUs", field: "limit_order_jitter_us" },
                        { caption: "Cancel base", key: "cancelOrderLatencyUs", field: "cancel_order_latency_us" },
                        { caption: "Cancel jit", key: "cancelOrderJitterUs", field: "cancel_order_jitter_us" },
                        { caption: "User base", key: "userDataLatencyUs", field: "user_data_latency_us" },
                        { caption: "User jit", key: "userDataJitterUs", field: "user_data_jitter_us" }
                    ]
                    delegate: BacktestCompactField {
                        required property var modelData
                        caption: modelData.caption
                        fieldWidth: 210
                        text: latencyPopup.value(modelData.key)
                        textColor: selector.textColor
                        mutedTextColor: selector.mutedTextColor
                        panelDeepColor: selector.panelDeepColor
                        borderColor: selector.borderColor
                        onEdited: function(value) {
                            selector.backtestVm.setVenueExecutionValue(latencyPopup.rowData.index, modelData.field, value)
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                BacktestActionButton {
                    text: "Close"
                    onClicked: latencyPopup.close()
                }
            }
        }
    }
}
