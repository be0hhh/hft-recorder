import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import HftRecorder 1.0

Pane {
    id: root

    CaptureViewModel {
        id: captureVm
    }

    FolderDialog {
        id: folderDialog
        title: "Choose parent directory for session folders"
        onAccepted: {
            captureVm.setOutputDirectory(selectedFolder.toString().replace("file:///", ""))
        }
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
            }

            Label {
                text: "Binance FAPI / ETHUSDT / canonical normalized JSON corpus"
                color: "#666666"
            }

            Rectangle {
                Layout.fillWidth: true
                radius: 12
                color: "#f5f7fb"
                border.color: "#d8deea"
                border.width: 1
                implicitHeight: infoColumn.implicitHeight + 24

                ColumnLayout {
                    id: infoColumn
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 10

                    Label { text: "Parent Directory" ; font.bold: true }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        TextField {
                            Layout.fillWidth: true
                            text: captureVm.outputDirectory
                            readOnly: true
                        }

                        Button {
                            text: "Choose Folder"
                            onClicked: folderDialog.open()
                        }
                    }

                    Label { text: "Session ID: " + (captureVm.sessionId === "" ? "<not started>" : captureVm.sessionId) }
                    Label { text: "Session Path: " + (captureVm.sessionPath === "" ? "<not created>" : captureVm.sessionPath) }
                    Label { text: "Status: " + captureVm.statusText; wrapMode: Text.WordWrap }
                }
            }

            Rectangle {
                Layout.fillWidth: true
                radius: 12
                color: "#fffaf1"
                border.color: "#ead8a7"
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
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10

                        Button {
                            text: captureVm.tradesRunning ? "Stop Trades" : "Start Trades"
                            onClicked: {
                                if (captureVm.tradesRunning) {
                                    captureVm.stopTrades()
                                } else {
                                    captureVm.startTrades()
                                }
                            }
                        }

                        Button {
                            text: captureVm.bookTickerRunning ? "Stop BookTicker" : "Start BookTicker"
                            onClicked: {
                                if (captureVm.bookTickerRunning) {
                                    captureVm.stopBookTicker()
                                } else {
                                    captureVm.startBookTicker()
                                }
                            }
                        }

                        Button {
                            text: captureVm.orderbookRunning ? "Stop Orderbook" : "Start Orderbook"
                            onClicked: {
                                if (captureVm.orderbookRunning) {
                                    captureVm.stopOrderbook()
                                } else {
                                    captureVm.startOrderbook()
                                }
                            }
                        }

                        Item { Layout.fillWidth: true }

                        Button {
                            text: "Finalize Session"
                            onClicked: captureVm.finalizeSession()
                        }
                    }

                    Label {
                        text: "Orderbook button contract: first REST snapshot, then WS deltas into depth.jsonl."
                        color: "#755d1d"
                        wrapMode: Text.WordWrap
                    }
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 3
                columnSpacing: 12
                rowSpacing: 12

                Rectangle {
                    Layout.fillWidth: true
                    radius: 12
                    color: "#eef7ff"
                    border.color: "#bfd8ef"
                    border.width: 1
                    implicitHeight: tradesColumn.implicitHeight + 24

                    ColumnLayout {
                        id: tradesColumn
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 8

                        Label { text: "Trades"; font.bold: true }
                        Label { text: "State: " + (captureVm.tradesRunning ? "running" : "idle") }
                        Label { text: "Events: " + captureVm.tradesCount }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    radius: 12
                    color: "#eefcf3"
                    border.color: "#c6e7d3"
                    border.width: 1
                    implicitHeight: l1Column.implicitHeight + 24

                    ColumnLayout {
                        id: l1Column
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 8

                        Label { text: "BookTicker / L1"; font.bold: true }
                        Label { text: "State: " + (captureVm.bookTickerRunning ? "running" : "idle") }
                        Label { text: "Events: " + captureVm.bookTickerCount }
                    }
                }

                Rectangle {
                    Layout.fillWidth: true
                    radius: 12
                    color: "#fff2f2"
                    border.color: "#ecc3c3"
                    border.width: 1
                    implicitHeight: depthColumn.implicitHeight + 24

                    ColumnLayout {
                        id: depthColumn
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 8

                        Label { text: "Orderbook Deltas"; font.bold: true }
                        Label { text: "State: " + (captureVm.orderbookRunning ? "running" : "idle") }
                        Label { text: "Events: " + captureVm.depthCount }
                    }
                }
            }
        }
    }
}
