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
    property color accentSellColor: "#da2536"

    component DarkButton: Button {
        id: control
        background: Rectangle {
            radius: 8
            color: control.enabled ? root.panelAltColor : "#232326"
            border.color: control.enabled ? root.borderColor : "#313136"
            border.width: 1
        }
        contentItem: Text {
            text: control.text
            color: control.enabled ? root.textColor : root.mutedTextColor
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

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

            Rectangle {
                Layout.fillWidth: true
                radius: 12
                color: root.panelColor
                border.color: root.borderColor
                border.width: 1
                implicitHeight: infoColumn.implicitHeight + 24

                ColumnLayout {
                    id: infoColumn
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10

                    Label { text: "Parent Directory"; font.bold: true; color: root.textColor }

                    TextField {
                        Layout.fillWidth: true
                        text: captureVm.outputDirectory
                        readOnly: true
                        color: root.textColor
                        selectedTextColor: root.textColor
                        selectionColor: root.accentBuyColor
                        background: Rectangle {
                            radius: 8
                            color: root.panelAltColor
                            border.color: root.borderColor
                            border.width: 1
                        }
                    }

                    Label { text: "Symbols"; font.bold: true; color: root.textColor }

                    TextField {
                        Layout.fillWidth: true
                        text: captureVm.symbolsText
                        placeholderText: "RAVE BTC ETH"
                        color: root.textColor
                        placeholderTextColor: root.mutedTextColor
                        selectedTextColor: root.textColor
                        selectionColor: root.accentBuyColor
                        enabled: !captureVm.sessionOpen
                        onTextChanged: captureVm.setSymbolsText(text)
                        background: Rectangle {
                            radius: 8
                            color: root.panelAltColor
                            border.color: root.borderColor
                            border.width: 1
                        }
                    }

                    Label {
                        text: "Normalized: " + (captureVm.normalizedSymbolsText === "" ? "<none>" : captureVm.normalizedSymbolsText)
                        color: root.mutedTextColor
                        wrapMode: Text.WordWrap
                    }

                    Label { text: "Session ID: " + (captureVm.sessionId === "" ? "<not started>" : captureVm.sessionId); color: root.textColor }
                    Label { text: "Session Path: " + (captureVm.sessionPath === "" ? "<not created>" : captureVm.sessionPath); color: root.textColor }
                    Label { text: "Status: " + captureVm.statusText; wrapMode: Text.WordWrap; color: root.mutedTextColor }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 18

                        Label { text: "Trades: " + captureVm.tradesCount; color: root.textColor }
                        Label { text: "BookTicker: " + captureVm.bookTickerCount; color: root.textColor }
                        Label { text: "Depth: " + captureVm.depthCount; color: root.textColor }
                        Item { Layout.fillWidth: true }
                    }
                }
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

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Button {
                            id: tradesButton
                            text: captureVm.tradesRunning ? "Stop Trades" : "Start Trades"
                            onClicked: {
                                if (captureVm.tradesRunning)
                                    captureVm.stopTrades()
                                else
                                    captureVm.startTrades()
                            }
                            background: Rectangle {
                                radius: 8
                                color: captureVm.tradesRunning ? root.accentSellColor : root.accentBuyColor
                                border.color: captureVm.tradesRunning ? root.accentSellColor : root.accentBuyColor
                                border.width: 1
                            }
                            contentItem: Text {
                                text: tradesButton.text
                                color: "#101314"
                                horizontalAlignment: Text.AlignHCenter
                                verticalAlignment: Text.AlignVCenter
                            }
                        }

                        DarkButton {
                            text: captureVm.bookTickerRunning ? "Stop BookTicker" : "Start BookTicker"
                            onClicked: {
                                if (captureVm.bookTickerRunning)
                                    captureVm.stopBookTicker()
                                else
                                    captureVm.startBookTicker()
                            }
                        }

                        DarkButton {
                            text: captureVm.orderbookRunning ? "Stop Orderbook" : "Start Orderbook"
                            onClicked: {
                                if (captureVm.orderbookRunning)
                                    captureVm.stopOrderbook()
                                else
                                    captureVm.startOrderbook()
                            }
                        }

                        Item { Layout.fillWidth: true }

                        DarkButton {
                            text: "Finalize Session"
                            onClicked: captureVm.finalizeSession()
                        }
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
