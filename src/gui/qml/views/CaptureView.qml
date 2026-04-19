import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HftRecorder 1.0

Pane {
    id: root

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

    CaptureViewModel {
        id: captureVm
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth

        ColumnLayout {
            width: parent.width
            spacing: 16

            Label {
                text: "Live Capture"
                font.pixelSize: 26
                font.bold: true
                color: root.textColor
            }

            Label {
                text: "Binance FAPI / canonical normalized JSON corpus"
                color: root.mutedTextColor
            }

            CaptureSessionSummaryCard {
                captureVm: captureVm
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
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 12

                    Label {
                        text: "Capture Controls"
                        font.bold: true
                        color: root.textColor
                    }

                    CaptureChannelCard {
                        captureVm: captureVm
                        channelKey: "trades"
                        titleText: "Trades Request"
                        emptyText: "No trade aliases available."
                        availableAliases: captureVm.tradesAvailableAliases
                        requestPreview: captureVm.tradesRequestPreview
                        weightSummary: captureVm.channelWeightSummary("trades")
                        running: captureVm.tradesRunning
                        actionText: captureVm.tradesRunning ? "Stop Trades" : "Start Trades"
                        panelColor: root.panelColor
                        panelAltColor: root.panelAltColor
                        borderColor: root.borderColor
                        textColor: root.textColor
                        mutedTextColor: root.mutedTextColor
                        accentRequiredColor: root.accentRequiredColor
                        accentOptionalColor: root.accentOptionalColor
                        accentSellColor: root.accentSellColor
                        actionAccentColor: captureVm.tradesRunning ? root.accentSellColor : root.accentRequiredColor
                        actionTextColor: captureVm.tradesRunning ? "#fff4f5" : "#071419"
                        onActionTriggered: {
                            if (captureVm.tradesRunning)
                                captureVm.stopTrades()
                            else
                                captureVm.startTrades()
                        }
                    }

                    CaptureChannelCard {
                        captureVm: captureVm
                        channelKey: "bookticker"
                        titleText: "BookTicker Request"
                        emptyText: "No book-ticker aliases available."
                        availableAliases: captureVm.bookTickerAvailableAliases
                        requestPreview: captureVm.bookTickerRequestPreview
                        weightSummary: captureVm.channelWeightSummary("bookticker")
                        running: captureVm.bookTickerRunning
                        actionText: captureVm.bookTickerRunning ? "Stop BookTicker" : "Start BookTicker"
                        panelColor: root.panelColor
                        panelAltColor: root.panelAltColor
                        borderColor: root.borderColor
                        textColor: root.textColor
                        mutedTextColor: root.mutedTextColor
                        accentRequiredColor: root.accentRequiredColor
                        accentOptionalColor: root.accentOptionalColor
                        accentSellColor: root.accentSellColor
                        actionAccentColor: captureVm.bookTickerRunning ? root.accentSellColor : root.accentRequiredColor
                        actionTextColor: captureVm.bookTickerRunning ? "#fff4f5" : "#071419"
                        onActionTriggered: {
                            if (captureVm.bookTickerRunning)
                                captureVm.stopBookTicker()
                            else
                                captureVm.startBookTicker()
                        }
                    }

                    CaptureChannelCard {
                        captureVm: captureVm
                        channelKey: "orderbook"
                        titleText: "Orderbook Request"
                        emptyText: "No orderbook aliases available."
                        availableAliases: captureVm.orderbookAvailableAliases
                        requestPreview: captureVm.orderbookRequestPreview
                        weightSummary: captureVm.channelWeightSummary("orderbook")
                        running: captureVm.orderbookRunning
                        actionText: captureVm.orderbookRunning ? "Stop Orderbook" : "Start Orderbook"
                        panelColor: root.panelColor
                        panelAltColor: root.panelAltColor
                        borderColor: root.borderColor
                        textColor: root.textColor
                        mutedTextColor: root.mutedTextColor
                        accentRequiredColor: root.accentRequiredColor
                        accentOptionalColor: root.accentOptionalColor
                        accentSellColor: root.accentSellColor
                        actionAccentColor: captureVm.orderbookRunning ? root.accentSellColor : root.accentRequiredColor
                        actionTextColor: captureVm.orderbookRunning ? "#fff4f5" : "#071419"
                        onActionTriggered: {
                            if (captureVm.orderbookRunning)
                                captureVm.stopOrderbook()
                            else
                                captureVm.startOrderbook()
                        }
                    }

                    CaptureDarkButton {
                        text: "Finalize Session"
                        panelAltColor: root.panelAltColor
                        borderColor: root.borderColor
                        textColor: root.textColor
                        mutedTextColor: root.mutedTextColor
                        onClicked: captureVm.finalizeSession()
                    }

                    Label {
                        text: "Orderbook button contract: first REST snapshot, then WS deltas into depth.jsonl."
                        color: root.mutedTextColor
                        wrapMode: Text.WordWrap
                    }
                }
            }
        }
    }
}
