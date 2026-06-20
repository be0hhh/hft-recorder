import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HftRecorder 1.0

Pane {
    id: root

    required property CaptureViewModel captureVm
    required property bool tabActive
    property bool anyChannelRunning: root.captureVm.tradesRunning || root.captureVm.liquidationsRunning || root.captureVm.bookTickerRunning || root.captureVm.orderbookRunning || root.captureVm.markPriceRunning || root.captureVm.indexPriceRunning || root.captureVm.fundingRunning || root.captureVm.priceLimitRunning

    property color windowColor: "#161616"
    property color panelColor: "#2c2c2f"
    property color panelAltColor: "#3c3c3c"
    property color borderColor: "#3c3d3f"
    property color textColor: "#f5f5f5"
    property color mutedTextColor: "#b6b6b6"
    property color accentBuyColor: "#24c2cb"
    property color accentRequiredColor: "#1ed8ff"
    property color accentOptionalColor: "#179ca4"
    property color accentSellColor: "#da2536"

    background: Rectangle { color: root.windowColor }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: 16

            Label { text: "Live Capture"; font.pixelSize: 26; font.bold: true; color: root.textColor }
            Label { text: root.captureVm.captureAvailable ? "Multi-venue spot / futures / margin / canonical normalized JSON corpus" : root.captureVm.captureUnavailableReason; color: root.captureVm.captureAvailable ? root.mutedTextColor : root.accentSellColor; wrapMode: Text.WordWrap }

            CaptureSessionSummaryCard {
                captureVm: root.captureVm
                panelColor: root.panelColor
                panelAltColor: root.panelAltColor
                borderColor: root.borderColor
                textColor: root.textColor
                mutedTextColor: root.mutedTextColor
                accentBuyColor: root.accentBuyColor
            }

            Rectangle {
                Layout.fillWidth: true
                radius: 12
                color: root.panelColor
                border.color: root.borderColor
                border.width: 1
                implicitHeight: controlsColumn.implicitHeight + 24

                ColumnLayout {
                    id: controlsColumn
                    enabled: root.captureVm.captureAvailable
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12

                    Label { text: "Capture Controls"; font.bold: true; color: root.textColor }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8

                        Label {
                            text: "Venues and markets"
                            color: root.mutedTextColor
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            TextField {
                                Layout.fillWidth: true
                                text: root.captureVm.symbolsText
                                placeholderText: "Global symbols, for example BTCUSDT ETHUSDT"
                                selectByMouse: true
                                color: root.textColor
                                placeholderTextColor: root.mutedTextColor
                                onTextEdited: root.captureVm.symbolsText = text
                                onAccepted: {
                                    root.captureVm.applyGlobalSymbolsToVenues()
                                    venueGrid.refreshVenueSymbolFields()
                                }
                                background: Rectangle {
                                    radius: 8
                                    color: root.panelAltColor
                                    border.color: root.borderColor
                                    border.width: 1
                                }
                            }

                            CaptureAccentActionButton {
                                text: "Apply to venues"
                                accentColor: root.accentRequiredColor
                                actionTextColor: "#071419"
                                mutedTextColor: root.mutedTextColor
                                enabled: root.captureVm.normalizedSymbolsText !== ""
                                onClicked: {
                                    root.captureVm.applyGlobalSymbolsToVenues()
                                    venueGrid.refreshVenueSymbolFields()
                                }
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 10

                            TextField {
                                Layout.fillWidth: true
                                text: root.captureVm.envPath
                                placeholderText: "Env file"
                                selectByMouse: true
                                enabled: !root.anyChannelRunning
                                color: root.textColor
                                placeholderTextColor: root.mutedTextColor
                                onTextEdited: root.captureVm.envPath = text
                                background: Rectangle {
                                    radius: 8
                                    color: root.panelAltColor
                                    border.color: root.borderColor
                                    border.width: 1
                                }
                            }

                            Label {
                                text: "API slot"
                                color: root.mutedTextColor
                            }

                            SpinBox {
                                from: 1
                                to: 255
                                value: root.captureVm.apiSlot
                                editable: true
                                enabled: !root.anyChannelRunning
                                onValueModified: root.captureVm.apiSlot = value
                            }
                        }

                        Rectangle {
                            Layout.fillWidth: true
                            radius: 8
                            color: root.panelAltColor
                            border.color: root.borderColor
                            border.width: 1
                            implicitHeight: detailedCandlesColumn.implicitHeight + 20

                            ColumnLayout {
                                id: detailedCandlesColumn
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 10

                                RowLayout {
                                    Layout.fillWidth: true
                                    spacing: 10

                                    Label {
                                        Layout.fillWidth: true
                                        text: "Detailed Candles v2"
                                        font.bold: true
                                        color: root.textColor
                                    }

                                    Label {
                                        text: "Rows " + root.captureVm.candles2Count
                                        color: root.mutedTextColor
                                    }
                                }

                                GridLayout {
                                    Layout.fillWidth: true
                                    columns: 4
                                    columnSpacing: 12
                                    rowSpacing: 8

                                    RecorderComboBox {
                                        id: detailedCandlesVenueCombo
                                        Layout.preferredWidth: 170
                                        Layout.minimumWidth: 150
                                        caption: "Venue"
                                        textRole: "label"
                                        valueRole: "key"
                                        popupWidth: 240
                                        panelColor: root.panelColor
                                        panelAltColor: root.panelAltColor
                                        panelDeepColor: root.panelColor
                                        borderColor: root.borderColor
                                        textColor: root.textColor
                                        mutedTextColor: root.mutedTextColor
                                        accentColor: root.accentRequiredColor
                                        model: root.captureVm.detailedCandlesVenueChoices
                                        Component.onCompleted: currentIndex = Math.max(0, indexOfValue(root.captureVm.detailedCandlesVenueKey))
                                        onActivated: root.captureVm.detailedCandlesVenueKey = currentValue
                                    }

                                    TextField {
                                        id: detailedCandlesSymbolField
                                        Layout.fillWidth: true
                                        Layout.minimumWidth: 180
                                        text: root.captureVm.detailedCandlesSymbolsText
                                        placeholderText: root.captureVm.venueSymbolPlaceholder(root.captureVm.detailedCandlesVenueKey)
                                        selectByMouse: true
                                        color: root.textColor
                                        placeholderTextColor: root.mutedTextColor
                                        onTextEdited: root.captureVm.detailedCandlesSymbolsText = text
                                        background: Rectangle {
                                            radius: 8
                                            color: root.panelAltColor
                                            border.color: root.borderColor
                                            border.width: 1
                                        }
                                    }

                                    RecorderComboBox {
                                        id: detailedCandlesTimeframeCombo
                                        Layout.preferredWidth: 170
                                        Layout.minimumWidth: 150
                                        caption: "Timeframe"
                                        textRole: "label"
                                        valueRole: "value"
                                        popupWidth: 240
                                        panelColor: root.panelColor
                                        panelAltColor: root.panelAltColor
                                        panelDeepColor: root.panelColor
                                        borderColor: root.borderColor
                                        textColor: root.textColor
                                        mutedTextColor: root.mutedTextColor
                                        accentColor: root.accentRequiredColor
                                        model: root.captureVm.detailedCandlesTimeframeChoices
                                        Component.onCompleted: currentIndex = Math.max(0, indexOfValue(root.captureVm.detailedCandlesTimeframe))
                                        onActivated: root.captureVm.detailedCandlesTimeframe = currentValue
                                    }

                                    SpinBox {
                                        Layout.preferredWidth: 170
                                        Layout.minimumWidth: 155
                                        from: 1
                                        to: 1000000
                                        stepSize: 1000
                                        editable: true
                                        value: root.captureVm.detailedCandlesLimit
                                        onValueModified: root.captureVm.detailedCandlesLimit = value
                                    }

                                }

                                Connections {
                                    target: root.captureVm
                                    function onDetailedCandlesChanged() {
                                        var venueIdx = detailedCandlesVenueCombo.indexOfValue(root.captureVm.detailedCandlesVenueKey)
                                        if (venueIdx >= 0 && detailedCandlesVenueCombo.currentIndex !== venueIdx)
                                            detailedCandlesVenueCombo.currentIndex = venueIdx

                                        var tfIdx = detailedCandlesTimeframeCombo.indexOfValue(root.captureVm.detailedCandlesTimeframe)
                                        if (tfIdx >= 0 && detailedCandlesTimeframeCombo.currentIndex !== tfIdx)
                                            detailedCandlesTimeframeCombo.currentIndex = tfIdx
                                    }
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: root.captureVm.detailedCandlesRequestPreview
                                    color: root.mutedTextColor
                                    wrapMode: Text.WordWrap
                                }

                                Label {
                                    Layout.fillWidth: true
                                    text: root.captureVm.detailedCandlesLimitWarning
                                    color: root.mutedTextColor
                                    wrapMode: Text.WordWrap
                                }

                                CaptureAccentActionButton {
                                    text: "Download Candles2"
                                    accentColor: root.accentRequiredColor
                                    actionTextColor: "#071419"
                                    mutedTextColor: root.mutedTextColor
                                    enabled: root.captureVm.captureAvailable
                                    onClicked: root.captureVm.startDetailedCandles()
                                }
                            }
                        }

                        GridLayout {
                            id: venueGrid
                            Layout.fillWidth: true
                            columns: 3
                            columnSpacing: 12
                            rowSpacing: 12

                            function refreshVenueSymbolFields() {
                                for (var i = 0; i < venueRepeater.count; ++i) {
                                    var item = venueRepeater.itemAt(i)
                                    if (!item) continue
                                    item.symbolsEditor.text = root.captureVm.venueSymbolsText(item.modelData.key)
                                }
                            }

                            Repeater {
                                id: venueRepeater
                                model: root.captureVm.venueChoices
                                delegate: ColumnLayout {
                                    id: venueRow
                                    required property var modelData
                                    Layout.fillWidth: true
                                    Layout.minimumWidth: 220
                                    spacing: 6
                                    property alias symbolsEditor: symbolsArea

                                    CheckBox {
                                        Layout.fillWidth: true
                                        text: venueRow.modelData.label
                                        checked: root.captureVm.isVenueSelected(venueRow.modelData.key)
                                        onClicked: root.captureVm.toggleVenue(venueRow.modelData.key)
                                    }

                                    TextArea {
                                        id: symbolsArea
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: Math.max(56, contentHeight + topPadding + bottomPadding + 4)
                                        text: root.captureVm.venueSymbolsText(venueRow.modelData.key)
                                        placeholderText: root.captureVm.venueSymbolPlaceholder(venueRow.modelData.key)
                                        wrapMode: TextEdit.Wrap
                                        selectByMouse: true
                                        color: root.textColor
                                        placeholderTextColor: root.mutedTextColor
                                        selectedTextColor: root.textColor
                                        selectionColor: root.accentBuyColor
                                        onTextChanged: root.captureVm.setVenueSymbolsText(venueRow.modelData.key, text)
                                        background: Rectangle {
                                            radius: 8
                                            color: root.panelAltColor
                                            border.color: root.borderColor
                                            border.width: 1
                                        }
                                    }
                                }
                            }
                        }
                    }

                    CaptureAccentActionButton {
                        Layout.fillWidth: true
                        text: root.anyChannelRunning ? "Stop All" : "Start All"
                        accentColor: root.anyChannelRunning ? root.accentSellColor : root.accentRequiredColor
                        actionTextColor: root.anyChannelRunning ? "#fff4f5" : "#071419"
                        mutedTextColor: root.mutedTextColor
                        enabled: root.captureVm.captureAvailable
                        onClicked: {
                            if (root.anyChannelRunning) root.captureVm.stopAllChannels()
                            else root.captureVm.startAllChannels()
                        }
                    }

                    CaptureChannelCard {
                        captureVm: root.captureVm
                        channelKey: "trades"
                        titleText: "Trades Request"
                        emptyText: "No trade aliases available."
                        availableAliases: root.captureVm.tradesAvailableAliases
                        requestPreview: root.captureVm.tradesRequestPreview
                        weightSummary: root.captureVm.channelWeightSummary("trades")
                        running: root.captureVm.tradesRunning
                        actionText: root.captureVm.tradesRunning ? "Stop Trades" : "Start Trades"
                        panelColor: root.panelColor
                        panelAltColor: root.panelAltColor
                        borderColor: root.borderColor
                        textColor: root.textColor
                        mutedTextColor: root.mutedTextColor
                        accentRequiredColor: root.accentRequiredColor
                        accentOptionalColor: root.accentOptionalColor
                        accentSellColor: root.accentSellColor
                        actionAccentColor: root.captureVm.tradesRunning ? root.accentSellColor : root.accentRequiredColor
                        actionTextColor: root.captureVm.tradesRunning ? "#fff4f5" : "#071419"

                        actionVisible: true
                        onActionTriggered: root.captureVm.tradesRunning ? root.captureVm.stopTrades() : root.captureVm.startTrades()

                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Label {
                            Layout.fillWidth: true
                            text: "Trades warmup"
                            color: root.mutedTextColor
                        }

                        SpinBox {
                            id: tradesWarmupSpin
                            from: 0
                            to: 86400
                            stepSize: 300
                            value: root.captureVm.tradesHistoryWarmupSec
                            editable: true
                            enabled: !root.captureVm.tradesRunning
                            onValueModified: root.captureVm.tradesHistoryWarmupSec = value
                        }

                        Label {
                            text: "sec"
                            color: root.mutedTextColor
                        }
                    }

                    CaptureChannelCard {
                        captureVm: root.captureVm
                        channelKey: "liquidations"
                        titleText: "Liquidations Request"
                        emptyText: "No liquidation aliases available."
                        availableAliases: root.captureVm.liquidationsAvailableAliases
                        requestPreview: root.captureVm.liquidationsRequestPreview
                        weightSummary: root.captureVm.channelWeightSummary("liquidations")
                        running: root.captureVm.liquidationsRunning
                        actionText: root.captureVm.liquidationsRunning ? "Stop Liquidations" : "Start Liquidations"
                        panelColor: root.panelColor
                        panelAltColor: root.panelAltColor
                        borderColor: root.borderColor
                        textColor: root.textColor
                        mutedTextColor: root.mutedTextColor
                        accentRequiredColor: root.accentRequiredColor
                        accentOptionalColor: root.accentOptionalColor
                        accentSellColor: root.accentSellColor
                        actionAccentColor: root.captureVm.liquidationsRunning ? root.accentSellColor : root.accentRequiredColor
                        actionTextColor: root.captureVm.liquidationsRunning ? "#fff4f5" : "#071419"

                        actionVisible: true
                        onActionTriggered: root.captureVm.liquidationsRunning ? root.captureVm.stopLiquidations() : root.captureVm.startLiquidations()

                    }
                    CaptureChannelCard {
                        captureVm: root.captureVm
                        channelKey: "bookticker"
                        titleText: "BookTicker Request"
                        emptyText: "No book-ticker aliases available."
                        availableAliases: root.captureVm.bookTickerAvailableAliases
                        requestPreview: root.captureVm.bookTickerRequestPreview
                        weightSummary: root.captureVm.channelWeightSummary("bookticker")
                        running: root.captureVm.bookTickerRunning
                        actionText: root.captureVm.bookTickerRunning ? "Stop BookTicker" : "Start BookTicker"
                        panelColor: root.panelColor
                        panelAltColor: root.panelAltColor
                        borderColor: root.borderColor
                        textColor: root.textColor
                        mutedTextColor: root.mutedTextColor
                        accentRequiredColor: root.accentRequiredColor
                        accentOptionalColor: root.accentOptionalColor
                        accentSellColor: root.accentSellColor
                        actionAccentColor: root.captureVm.bookTickerRunning ? root.accentSellColor : root.accentRequiredColor
                        actionTextColor: root.captureVm.bookTickerRunning ? "#fff4f5" : "#071419"

                        actionVisible: true
                        onActionTriggered: root.captureVm.bookTickerRunning ? root.captureVm.stopBookTicker() : root.captureVm.startBookTicker()

                    }

                    CaptureChannelCard {
                        captureVm: root.captureVm
                        channelKey: "mark_price"
                        titleText: "MarkPrice"
                        emptyText: "CXET runtime stream: mark price."
                        availableAliases: []
                        requestPreview: "subscribe().object(mark_price) -> jsonl/mark_price.jsonl"
                        weightSummary: "Rows " + root.captureVm.markPriceCount
                        running: root.captureVm.markPriceRunning
                        actionText: root.captureVm.markPriceRunning ? "Stop MarkPrice" : "Start MarkPrice"
                        panelColor: root.panelColor
                        panelAltColor: root.panelAltColor
                        borderColor: root.borderColor
                        textColor: root.textColor
                        mutedTextColor: root.mutedTextColor
                        accentRequiredColor: root.accentRequiredColor
                        accentOptionalColor: root.accentOptionalColor
                        accentSellColor: root.accentSellColor
                        actionAccentColor: root.captureVm.markPriceRunning ? root.accentSellColor : root.accentRequiredColor
                        actionTextColor: root.captureVm.markPriceRunning ? "#fff4f5" : "#071419"
                        actionVisible: true
                        onActionTriggered: root.captureVm.markPriceRunning ? root.captureVm.stopMarkPrice() : root.captureVm.startMarkPrice()
                    }

                    CaptureChannelCard {
                        captureVm: root.captureVm
                        channelKey: "index_price"
                        titleText: "IndexPrice"
                        emptyText: "CXET runtime stream: index price."
                        availableAliases: []
                        requestPreview: "subscribe().object(index_price) -> jsonl/index_price.jsonl"
                        weightSummary: "Rows " + root.captureVm.indexPriceCount
                        running: root.captureVm.indexPriceRunning
                        actionText: root.captureVm.indexPriceRunning ? "Stop IndexPrice" : "Start IndexPrice"
                        panelColor: root.panelColor
                        panelAltColor: root.panelAltColor
                        borderColor: root.borderColor
                        textColor: root.textColor
                        mutedTextColor: root.mutedTextColor
                        accentRequiredColor: root.accentRequiredColor
                        accentOptionalColor: root.accentOptionalColor
                        accentSellColor: root.accentSellColor
                        actionAccentColor: root.captureVm.indexPriceRunning ? root.accentSellColor : root.accentRequiredColor
                        actionTextColor: root.captureVm.indexPriceRunning ? "#fff4f5" : "#071419"
                        actionVisible: true
                        onActionTriggered: root.captureVm.indexPriceRunning ? root.captureVm.stopIndexPrice() : root.captureVm.startIndexPrice()
                    }

                    CaptureChannelCard {
                        captureVm: root.captureVm
                        channelKey: "funding"
                        titleText: "Funding"
                        emptyText: "CXET funding stream/poller; writes only changed funding tuples."
                        availableAliases: []
                        requestPreview: "subscribe().object(funding) -> jsonl/funding.jsonl"
                        weightSummary: "Rows " + root.captureVm.fundingCount
                        running: root.captureVm.fundingRunning
                        actionText: root.captureVm.fundingRunning ? "Stop Funding" : "Start Funding"
                        panelColor: root.panelColor
                        panelAltColor: root.panelAltColor
                        borderColor: root.borderColor
                        textColor: root.textColor
                        mutedTextColor: root.mutedTextColor
                        accentRequiredColor: root.accentRequiredColor
                        accentOptionalColor: root.accentOptionalColor
                        accentSellColor: root.accentSellColor
                        actionAccentColor: root.captureVm.fundingRunning ? root.accentSellColor : root.accentRequiredColor
                        actionTextColor: root.captureVm.fundingRunning ? "#fff4f5" : "#071419"
                        actionVisible: true
                        onActionTriggered: root.captureVm.fundingRunning ? root.captureVm.stopFunding() : root.captureVm.startFunding()
                    }

                    CaptureChannelCard {
                        captureVm: root.captureVm
                        channelKey: "price_limit"
                        titleText: "PriceLimit"
                        emptyText: "CXET runtime stream: buy/sell limits."
                        availableAliases: []
                        requestPreview: "subscribe().object(price_limit) -> jsonl/price_limit.jsonl"
                        weightSummary: "Rows " + root.captureVm.priceLimitCount
                        running: root.captureVm.priceLimitRunning
                        actionText: root.captureVm.priceLimitRunning ? "Stop PriceLimit" : "Start PriceLimit"
                        panelColor: root.panelColor
                        panelAltColor: root.panelAltColor
                        borderColor: root.borderColor
                        textColor: root.textColor
                        mutedTextColor: root.mutedTextColor
                        accentRequiredColor: root.accentRequiredColor
                        accentOptionalColor: root.accentOptionalColor
                        accentSellColor: root.accentSellColor
                        actionAccentColor: root.captureVm.priceLimitRunning ? root.accentSellColor : root.accentRequiredColor
                        actionTextColor: root.captureVm.priceLimitRunning ? "#fff4f5" : "#071419"
                        actionVisible: true
                        onActionTriggered: root.captureVm.priceLimitRunning ? root.captureVm.stopPriceLimit() : root.captureVm.startPriceLimit()
                    }

                    CaptureChannelCard {
                        captureVm: root.captureVm
                        channelKey: "candles"
                        titleText: "Candles History"
                        emptyText: "Tiered REST candles: M1, M15, D1."
                        availableAliases: []
                        requestPreview: "subscribe().object(candles).tiered(M1,M15,D1) -> jsonl/candles.jsonl"
                        weightSummary: "Rows " + root.captureVm.candlesCount
                        running: false
                        actionText: "Start Candles"
                        panelColor: root.panelColor
                        panelAltColor: root.panelAltColor
                        borderColor: root.borderColor
                        textColor: root.textColor
                        mutedTextColor: root.mutedTextColor
                        accentRequiredColor: root.accentRequiredColor
                        accentOptionalColor: root.accentOptionalColor
                        accentSellColor: root.accentSellColor
                        actionAccentColor: root.accentRequiredColor
                        actionTextColor: "#071419"
                        actionVisible: true
                        onActionTriggered: root.captureVm.startCandles()
                    }

                    CaptureChannelCard {
                        captureVm: root.captureVm
                        channelKey: "orderbook"
                        titleText: "Orderbook Request"
                        emptyText: "No orderbook aliases available."
                        availableAliases: root.captureVm.orderbookAvailableAliases
                        requestPreview: root.captureVm.orderbookRequestPreview
                        weightSummary: root.captureVm.channelWeightSummary("orderbook")
                        running: root.captureVm.orderbookRunning
                        actionText: root.captureVm.orderbookRunning ? "Stop Orderbook" : "Start Orderbook"
                        panelColor: root.panelColor
                        panelAltColor: root.panelAltColor
                        borderColor: root.borderColor
                        textColor: root.textColor
                        mutedTextColor: root.mutedTextColor
                        accentRequiredColor: root.accentRequiredColor
                        accentOptionalColor: root.accentOptionalColor
                        accentSellColor: root.accentSellColor
                        actionAccentColor: root.captureVm.orderbookRunning ? root.accentSellColor : root.accentRequiredColor
                        actionTextColor: root.captureVm.orderbookRunning ? "#fff4f5" : "#071419"

                        actionVisible: true
                        onActionTriggered: root.captureVm.orderbookRunning ? root.captureVm.stopOrderbook() : root.captureVm.startOrderbook()

                    }
                    CaptureDarkButton {
                        text: "Finalize Session"
                        panelAltColor: root.panelAltColor
                        borderColor: root.borderColor
                        textColor: root.textColor
                        mutedTextColor: root.mutedTextColor
                        enabled: root.captureVm.captureAvailable
                        onClicked: root.captureVm.finalizeSession()
                    }

                    Label {
                        text: "Orderbook capture writes WS depth as depth_tape.jsonl plus depth_sidecar.jsonl; REST snapshot is optional."
                        color: root.mutedTextColor
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }
}
