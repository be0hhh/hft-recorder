import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: panel

    property string mode: "stable"
    property var stableRows: []
    property var profitRows: []
    property var symbolRows: []
    property var pairRows: []
    property var paramRows: []
    property var timeRows: []
    property var plateauRows: []
    property var skippedRows: []
    property string statusText: ""
    property color panelColor: "#22252d"
    property color panelDeepColor: "#15171c"
    property color panelAltColor: "#2b303a"
    property color borderColor: "#343844"
    property color textColor: "#f1f4f8"
    property color mutedTextColor: "#a8afbd"
    property color accentColor: "#24c2cb"
    property color goodColor: "#82d46b"
    property color badColor: "#ef6f6c"

    signal modeRequested(string mode)

    readonly property bool aggregateMode: mode === "symbol" || mode === "pair" || mode === "params"
    readonly property var activeRows: rowsForMode(mode)

    function safeList(value) {
        return value && value.length !== undefined ? value : []
    }

    function rowsForMode(rowMode) {
        if (rowMode === "profit") return safeList(profitRows)
        if (rowMode === "symbol") return safeList(symbolRows)
        if (rowMode === "pair") return safeList(pairRows)
        if (rowMode === "params") return safeList(paramRows)
        if (rowMode === "time") return safeList(timeRows)
        if (rowMode === "plateau") return safeList(plateauRows)
        return safeList(stableRows)
    }

    function tabCount(rowMode) {
        return rowsForMode(rowMode).length
    }

    function e8Text(value) {
        var raw = Number(value || 0)
        var negative = raw < 0
        var absValue = Math.abs(raw) / 100000000.0
        var text = absValue.toFixed(4)
        while (text.indexOf(".") >= 0 && text.endsWith("0")) text = text.slice(0, -1)
        if (text.endsWith(".")) text = text.slice(0, -1)
        return (negative ? "-" : "") + text
    }

    function rowPnl(row) {
        return Number(row.totalPnlE8 || row.total_pnl_e8 || row.avgPnlE8 || 0)
    }

    function rowDrawdown(row) {
        return Number(row.maxDrawdownE8 || row.max_drawdown_e8 || row.worstDrawdownE8 || 0)
    }

    function rowRisk(row) {
        if (aggregateMode)
            return String(row.positivePct || 0) + "% pos"
        var flags = []
        if (row.riskStopped || row.risk_stopped) flags.push("risk")
        if (row.liquidated) flags.push("liq")
        return flags.length > 0 ? flags.join(", ") : "clean"
    }

    function rowTitle(row) {
        if (aggregateMode)
            return row.label || row.key || ""
        return row.symbol || row.canonicalSymbol || row.label || ""
    }

    function rowSubtitle(row) {
        if (aggregateMode)
            return "rows " + String(row.rows || 0) + " clean " + String(row.okRows || 0)
        return row.exchangePair || row.exchange_pair || row.pair || ""
    }

    function rowParams(row) {
        if (aggregateMode)
            return row.key || ""
        return row.paramsLabel || row.params_label || ""
    }

    function rowExtra(row) {
        if (mode === "time") {
            return "time " + (row.stabilityText || "") +
                   " q " + e8Text(row.chunk0E8 || 0) + " / " + e8Text(row.chunk1E8 || 0) +
                   " / " + e8Text(row.chunk2E8 || 0) + " / " + e8Text(row.chunk3E8 || 0)
        }
        if (mode === "plateau") {
            return "neighbors " + String(row.neighbors || 0) +
                   " pos " + String(row.neighborPositivePct || 0) + "%" +
                   " conf " + String(row.confidence || 0) + "%"
        }
        if (aggregateMode) {
            return "best " + e8Text(row.bestPnlE8 || 0) + " fills " + String(row.fills || 0)
        }
        return row.status || ""
    }

    function skippedTitle(row) {
        var symbol = row.symbol || row.canonicalSymbol || ""
        var venue = row.exchange || row.exchangePair || ""
        return (venue.length > 0 ? venue + " " : "") + symbol
    }

    color: panelColor
    border.color: borderColor
    border.width: 1
    radius: 6
    clip: true

    component TabButton: Rectangle {
        property string tabId: ""
        property string title: ""
        property int count: 0
        property bool selected: false
        signal clicked(string tabId)

        width: Math.max(72, tabText.implicitWidth + 28)
        height: 28
        radius: 5
        color: selected ? panel.panelAltColor : panel.panelDeepColor
        border.color: selected ? panel.accentColor : panel.borderColor
        border.width: selected ? 2 : 1

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 5
            Text {
                id: tabText
                text: title
                color: panel.textColor
                font.pixelSize: 11
                font.bold: selected
                elide: Text.ElideRight
            }
            Text {
                text: String(count)
                color: selected ? panel.accentColor : panel.mutedTextColor
                font.pixelSize: 10
                font.bold: true
            }
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            onClicked: parent.clicked(parent.tabId)
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8

            Label {
                text: "Research rows"
                color: panel.textColor
                font.pixelSize: 13
                font.bold: true
                Layout.preferredWidth: 110
            }

            Flickable {
                Layout.fillWidth: true
                Layout.preferredHeight: 30
                contentWidth: Math.max(width, tabRow.implicitWidth)
                contentHeight: tabRow.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                clip: true

                Row {
                    id: tabRow
                    height: 30
                    spacing: 6

                    Repeater {
                        model: [
                            { "id": "stable", "title": "Stable" },
                            { "id": "profit", "title": "Profit" },
                            { "id": "symbol", "title": "Symbol" },
                            { "id": "pair", "title": "Pair" },
                            { "id": "params", "title": "Params" },
                            { "id": "time", "title": "Time" },
                            { "id": "plateau", "title": "Plateau" }
                        ]
                        delegate: TabButton {
                            required property var modelData
                            tabId: modelData.id
                            title: modelData.title
                            count: panel.tabCount(modelData.id)
                            selected: panel.mode === tabId
                            onClicked: function(nextMode) { panel.modeRequested(nextMode) }
                        }
                    }
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Label { text: "#"; color: panel.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 30 }
            Label { text: panel.aggregateMode ? "Group" : "Symbol"; color: panel.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 112 }
            Label { text: panel.aggregateMode ? "Coverage" : "Pair"; color: panel.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 144 }
            Label { text: panel.aggregateMode ? "Key / params" : "Best params"; color: panel.mutedTextColor; font.pixelSize: 11; Layout.fillWidth: true }
            Label { text: panel.aggregateMode ? "Avg" : "PnL"; color: panel.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 82; horizontalAlignment: Text.AlignRight }
            Label { text: panel.aggregateMode ? "Worst DD" : "DD"; color: panel.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 82; horizontalAlignment: Text.AlignRight }
            Label { text: panel.aggregateMode ? "Rows" : "Fills"; color: panel.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 54; horizontalAlignment: Text.AlignRight }
            Label { text: panel.aggregateMode ? "Hit" : "Risk"; color: panel.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 66 }
        }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: panel.activeRows
            delegate: Rectangle {
                required property var modelData
                required property int index

                width: ListView.view.width
                height: panel.aggregateMode ? 46 : (panel.mode === "time" || panel.mode === "plateau" ? 58 : 46)
                radius: 4
                color: index % 2 === 0 ? panel.panelDeepColor : "#181b21"
                border.color: panel.borderColor

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    spacing: 10

                    Label { text: String(index + 1); color: panel.mutedTextColor; font.pixelSize: 11; Layout.preferredWidth: 30 }
                    Label { text: panel.rowTitle(modelData); color: panel.textColor; font.bold: true; font.pixelSize: 12; elide: Text.ElideRight; Layout.preferredWidth: 112 }
                    Label { text: panel.rowSubtitle(modelData); color: panel.mutedTextColor; font.pixelSize: 11; elide: Text.ElideRight; Layout.preferredWidth: 144 }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Label { text: panel.rowParams(modelData); color: panel.mutedTextColor; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
                        Label {
                            visible: panel.rowExtra(modelData).length > 0
                            text: panel.rowExtra(modelData)
                            color: panel.mutedTextColor
                            font.pixelSize: 10
                            elide: Text.ElideRight
                            Layout.fillWidth: true
                        }
                    }

                    Label { text: panel.e8Text(panel.rowPnl(modelData)); color: panel.rowPnl(modelData) < 0 ? panel.badColor : panel.goodColor; font.bold: true; font.pixelSize: 12; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 82 }
                    Label { text: panel.e8Text(panel.rowDrawdown(modelData)); color: panel.mutedTextColor; font.pixelSize: 11; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 82 }
                    Label { text: String(panel.aggregateMode ? (modelData.rows || 0) : (modelData.fills || 0)); color: panel.mutedTextColor; font.pixelSize: 11; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 54 }
                    Label { text: panel.rowRisk(modelData); color: panel.rowRisk(modelData) === "clean" ? panel.goodColor : (panel.aggregateMode ? panel.accentColor : panel.badColor); font.pixelSize: 11; elide: Text.ElideRight; Layout.preferredWidth: 66 }
                }
            }
        }

        Label {
            visible: panel.activeRows.length === 0
            Layout.fillWidth: true
            Layout.preferredHeight: visible ? 28 : 0
            text: panel.statusText.length > 0 ? panel.statusText : "No rows for this view yet."
            color: panel.mutedTextColor
            font.pixelSize: 12
            horizontalAlignment: Text.AlignHCenter
            elide: Text.ElideRight
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: panel.safeList(panel.skippedRows).length > 0 ? 96 : 46
            color: panel.panelDeepColor
            border.color: panel.borderColor
            radius: 5
            clip: true

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                spacing: 4

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Label {
                        text: "Coverage"
                        color: panel.textColor
                        font.pixelSize: 11
                        font.bold: true
                    }
                    Label {
                        text: "stable " + panel.safeList(panel.stableRows).length +
                              " / profit " + panel.safeList(panel.profitRows).length +
                              " / skipped " + panel.safeList(panel.skippedRows).length
                        color: panel.mutedTextColor
                        font.pixelSize: 11
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    visible: panel.safeList(panel.skippedRows).length > 0
                    clip: true
                    model: panel.safeList(panel.skippedRows)
                    delegate: Label {
                        required property var modelData
                        width: ListView.view.width
                        height: 18
                        text: panel.skippedTitle(modelData) + " - " + (modelData.reason || modelData.status || "skipped")
                        color: panel.mutedTextColor
                        font.pixelSize: 10
                        elide: Text.ElideRight
                    }
                }
            }
        }
    }
}
