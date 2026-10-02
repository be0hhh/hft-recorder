import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HftRecorder 1.0

Rectangle {
    id: executionEdge
    required property var viewRoot

    required property bool entrySide
    required property string chartTitle
    color: viewRoot.windowColor
    border.color: viewRoot.borderColor
    radius: 5
    clip: true

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4
        Label { text: chartTitle; color: viewRoot.textColor; font.pixelSize: 12; font.bold: true }
        Canvas {
            id: executionCanvas
            Layout.fillWidth: true
            Layout.fillHeight: true
            property var points: viewRoot.executionPoints(entrySide)
            onPointsChanged: requestPaint()
            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            Connections { target: viewRoot.backtestVm; function onSelectionChanged() { executionCanvas.requestPaint() } }
            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                var rows = viewRoot.executionPoints(entrySide)
                if (rows.length === 0) return
                var minValue = 0
                var maxValue = 0
                for (var i = 0; i < rows.length; ++i) {
                    minValue = Math.min(minValue, Number(rows[i].decisionEdgeBpsE8), Number(rows[i].fillEdgeBpsE8), Number(rows[i].requiredEdgeBpsE8))
                    maxValue = Math.max(maxValue, Number(rows[i].decisionEdgeBpsE8), Number(rows[i].fillEdgeBpsE8), Number(rows[i].requiredEdgeBpsE8))
                }
                var span = Math.max(1000000, maxValue - minValue)
                minValue -= span * 0.08
                maxValue += span * 0.08
                var left = 44
                var top = 5
                var plotW = Math.max(20, width - left - 8)
                var plotH = Math.max(20, height - top - 18)
                function y(value) { return top + plotH - ((Number(value) - minValue) / (maxValue - minValue)) * plotH }
                ctx.strokeStyle = "#343844"
                ctx.lineWidth = 1
                for (var grid = 0; grid <= 4; ++grid) {
                    var gy = top + plotH * grid / 4
                    ctx.beginPath(); ctx.moveTo(left, gy); ctx.lineTo(left + plotW, gy); ctx.stroke()
                }
                function drawSeries(field, color) {
                    ctx.strokeStyle = color
                    ctx.fillStyle = color
                    ctx.lineWidth = 1.5
                    ctx.beginPath()
                    for (var p = 0; p < rows.length; ++p) {
                        var x = left + (rows.length === 1 ? plotW / 2 : p * plotW / (rows.length - 1))
                        var py = y(rows[p][field])
                        if (p === 0) ctx.moveTo(x, py); else ctx.lineTo(x, py)
                    }
                    ctx.stroke()
                    for (var q = 0; q < rows.length; ++q) {
                        var qx = left + (rows.length === 1 ? plotW / 2 : q * plotW / (rows.length - 1))
                        ctx.beginPath(); ctx.arc(qx, y(rows[q][field]), 2.5, 0, Math.PI * 2); ctx.fill()
                    }
                }
                drawSeries("requiredEdgeBpsE8", "#f0b35a")
                drawSeries("decisionEdgeBpsE8", viewRoot.accentColor)
                drawSeries("fillEdgeBpsE8", viewRoot.goodColor)
                ctx.font = "10px sans-serif"
                ctx.fillStyle = viewRoot.mutedTextColor
                ctx.fillText("boundary", left, height - 2)
                ctx.fillStyle = viewRoot.accentColor
                ctx.fillText("decision", left + 58, height - 2)
                ctx.fillStyle = viewRoot.goodColor
                ctx.fillText("actual fill", left + 112, height - 2)
            }
            Label {
                anchors.centerIn: parent
                visible: viewRoot.executionPoints(entrySide).length === 0
                text: "No completed actions"
                color: viewRoot.mutedTextColor
                font.pixelSize: 11
            }
        }
    }
}
