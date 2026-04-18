import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HftRecorder 1.0

Pane {
    id: root
    padding: 0

    background: Rectangle { color: "#0E0E12" }

    SessionListModel { id: sessionsModel; Component.onCompleted: reload() }
    ChartController   { id: chart }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Row 1: session dropdown + open-whole-session.
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 42
            spacing: 8

            Label {
                text: "Session:"
                color: "#D4D4D4"
                Layout.leftMargin: 12
            }
            ComboBox {
                id: sessionPicker
                Layout.fillWidth: true
                model: sessionsModel
                textRole: "sessionId"
                onActivated: {
                    if (currentIndex < 0) return
                    chart.loadSession("./recordings/" + currentText)
                }
            }
            Button {
                text: "Reload"
                onClicked: sessionsModel.reload()
            }
            Button {
                text: "Open"
                onClicked: {
                    if (sessionPicker.currentIndex < 0) return
                    chart.loadSession("./recordings/" + sessionPicker.currentText)
                }
            }
            Item { Layout.preferredWidth: 12 }
        }

        // Row 2: manual path entry for individual channels.
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 38
            spacing: 6

            Label {
                text: "Path:"
                color: "#D4D4D4"
                Layout.leftMargin: 12
            }
            TextField {
                id: pathField
                Layout.fillWidth: true
                placeholderText: "e.g. ./recordings/<sid>/trades.jsonl  (or snapshot_000.json / bookticker.jsonl / depth.jsonl)"
                selectByMouse: true
            }
            Button { text: "+ Snapshot";   onClicked: chart.addSnapshotFile(pathField.text)   }
            Button { text: "+ Trades";     onClicked: chart.addTradesFile(pathField.text)     }
            Button { text: "+ BookTicker"; onClicked: chart.addBookTickerFile(pathField.text) }
            Button { text: "+ Depth";      onClicked: chart.addDepthFile(pathField.text)      }
            Button { text: "Reset";        onClicked: chart.resetSession() }
            Button { text: "Finalize";     onClicked: chart.finalizeFiles() }
            Item { Layout.preferredWidth: 12 }
        }

        // Row 3: zoom / navigation.
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            spacing: 6

            Label {
                text: "View:"
                color: "#D4D4D4"
                Layout.leftMargin: 12
            }
            Button { text: "Fit"; onClicked: chart.autoFit() }
            Button { text: "|<";  onClicked: chart.jumpToStart() }
            Button { text: ">|";  onClicked: chart.jumpToEnd() }
            Button { text: "−T";  onClicked: chart.zoomTime(0.5) }
            Button { text: "+T";  onClicked: chart.zoomTime(2.0) }
            Button { text: "−P";  onClicked: chart.zoomPrice(0.5) }
            Button { text: "+P";  onClicked: chart.zoomPrice(2.0) }
            Item { Layout.fillWidth: true }
            Label {
                color: "#808090"
                text: "drag = pan, wheel = zoom time, Ctrl+wheel = zoom price"
            }
            Item { Layout.preferredWidth: 12 }
        }

        // Status line.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 22
            color: "#151521"
            Label {
                anchors.fill: parent
                anchors.leftMargin: 12
                verticalAlignment: Text.AlignVCenter
                color: "#A0A0B0"
                text: chart.statusText +
                      (chart.loaded
                        ? "  |  events=" + (chart.tradeCount + chart.depthCount) +
                          "  viewport ts: " + chart.tsMin + "..." + chart.tsMax +
                          "  price: " + (chart.priceMinE8 / 1e8).toFixed(4) +
                          "..." + (chart.priceMaxE8 / 1e8).toFixed(4)
                        : "")
            }
        }

        // Chart area.
        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            ChartItem {
                id: chartItem
                anchors.fill: parent
                controller: chart

                MouseArea {
                    anchors.fill: parent
                    property real lastX: 0
                    property real lastY: 0
                    property bool dragging: false
                    acceptedButtons: Qt.LeftButton

                    onPressed: function(mouse) {
                        lastX = mouse.x
                        lastY = mouse.y
                        dragging = true
                    }
                    onReleased: dragging = false
                    onPositionChanged: function(mouse) {
                        if (!dragging) return
                        var dx = mouse.x - lastX
                        var dy = mouse.y - lastY
                        lastX = mouse.x
                        lastY = mouse.y
                        if (chartItem.width > 0)
                            chart.panTime(-dx / chartItem.width)
                        if (chartItem.height > 0)
                            chart.panPrice(dy / chartItem.height)
                    }
                    onWheel: function(wheel) {
                        var factor = wheel.angleDelta.y > 0 ? 1.25 : 0.8
                        if (wheel.modifiers & Qt.ControlModifier)
                            chart.zoomPrice(factor)
                        else
                            chart.zoomTime(factor)
                    }
                }
            }
        }
    }
}
