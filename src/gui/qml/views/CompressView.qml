import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Pane {
    id: root
    required property var compressionVm
    required property bool tabActive

    property int activePage: 0
    property color windowColor: "#111216"
    property color panelColor: "#1b1d23"
    property color panelSoftColor: "#22252d"
    property color panelDeepColor: "#15171c"
    property color borderColor: "#343844"
    property color textColor: "#f1f4f8"
    property color mutedTextColor: "#a8afbd"
    property color accentColor: "#24c2cb"
    property color greenColor: "#82d46b"
    property color violetColor: "#b68cff"
    property color warnColor: "#f0b35a"
    property color badColor: "#ef6f6c"
    property var previewData: ({})
    property string previewTitle: ""

    background: Rectangle { color: root.windowColor }

    function syncSelections() {
        sessionBox.currentIndex = sessionBox.indexOfValue(root.compressionVm.selectedSessionId)
        channelBox.currentIndex = channelBox.indexOfValue(root.compressionVm.selectedChannel)
        pipelineBox.currentIndex = pipelineBox.indexOfValue(root.compressionVm.selectedPipelineId)
    }

    function bestEncodeSpeed() {
        var rows = root.compressionVm.speedSeries
        var best = 0
        for (var i = 0; i < rows.length; ++i) best = Math.max(best, rows[i].encodeMbPerSec)
        return best
    }

    function worstEncodeSpeed() {
        var rows = root.compressionVm.speedSeries
        var worst = 0
        for (var i = 0; i < rows.length; ++i) {
            var value = rows[i].encodeMbPerSec
            if (value <= 0) continue
            worst = worst === 0 ? value : Math.min(worst, value)
        }
        return worst
    }

    function bestRatio() {
        var rows = root.compressionVm.compressionBars
        var best = 0
        for (var i = 0; i < rows.length; ++i) best = Math.max(best, rows[i].ratio)
        return best
    }

    function selectedChannelTitle() {
        if (root.compressionVm.selectedChannel === "bookticker") return "BookTicker"
        if (root.compressionVm.selectedChannel === "depth") return "Depth"
        return "Trades"
    }

    function latestRunRow() {
        var rows = root.compressionVm.runRows
        return rows.length > 0 ? rows[rows.length - 1] : null
    }

    function latestVerifyRow() {
        var rows = root.compressionVm.verifyRows
        return rows.length > 0 ? rows[rows.length - 1] : null
    }

    function shortStatus(text) {
        if (text === "ok") return "\u0433\u043e\u0442\u043e\u0432\u043e"
        if (text === "Compression complete" || text === "\u041a\u043e\u0434\u0438\u0440\u043e\u0432\u0430\u043d\u0438\u0435 \u0437\u0430\u0432\u0435\u0440\u0448\u0435\u043d\u043e") return "\u0433\u043e\u0442\u043e\u0432\u043e"
        if (text === "Compression batch complete" || text === "\u041f\u0430\u043a\u0435\u0442\u043d\u043e\u0435 \u043a\u043e\u0434\u0438\u0440\u043e\u0432\u0430\u043d\u0438\u0435 \u0437\u0430\u0432\u0435\u0440\u0448\u0435\u043d\u043e") return "\u043f\u0430\u043a\u0435\u0442 \u0433\u043e\u0442\u043e\u0432"
        if (text === "Select a recording session and channel" || text === "\u0416\u0434\u0443 \u0432\u044b\u0431\u043e\u0440 \u0441\u0435\u0441\u0441\u0438\u0438 \u0438 \u043a\u0430\u043d\u0430\u043b\u0430") return "\u0436\u0434\u0443 \u0432\u044b\u0431\u043e\u0440"
        if (text === "\u041a\u043e\u0434\u0438\u0440\u043e\u0432\u0430\u043d\u0438\u0435 \u0432\u044b\u043f\u043e\u043b\u043d\u044f\u0435\u0442\u0441\u044f...") return "\u043a\u043e\u0434\u0438\u0440\u0443\u044e"
        if (text === "verification_failed") return "\u043d\u0435 \u0441\u043e\u0432\u043f\u0430\u043b\u043e"
        if (text === "io_error") return "\u0444\u0430\u0439\u043b \u043d\u0435 \u043d\u0430\u0439\u0434\u0435\u043d"
        if (text === "not_implemented") return "\u043d\u0435\u0442 \u0440\u0435\u0430\u043b\u0438\u0437\u0430\u0446\u0438\u0438"
        if (text === "unsupported_pipeline") return "\u043c\u0435\u0442\u043e\u0434 \u043d\u0435\u0434\u043e\u0441\u0442\u0443\u043f\u0435\u043d"
        return text
    }

    function selectFirstEncodedForDecode() {
        var id = root.compressionVm.firstEncodedPipelineId()
        if (id.length > 0 && !root.compressionVm.hasEncodedArtifact(root.compressionVm.selectedPipelineId))
            root.compressionVm.setSelectedPipelineId(id)
        root.syncSelections()
    }

    function openJsonlPreview() {
        root.previewData = root.compressionVm.previewJsonl("")
        root.previewTitle = "JSONL preview"
        filePreviewDialog.open()
    }

    function openArtifactPreview() {
        root.previewData = root.compressionVm.previewArtifact("")
        root.previewTitle = "Binary artifact preview"
        filePreviewDialog.open()
    }

    function bestBy(rows, key) {
        var best = null
        for (var i = 0; i < rows.length; ++i) {
            if (!best || rows[i][key] > best[key]) best = rows[i]
        }
        return best
    }

    function conclusionText() {
        var compression = root.compressionVm.compressionBars
        var verify = root.compressionVm.decodeBars
        if (compression.length < 1) return "\u041d\u0435\u0442 \u0440\u0435\u0437\u0443\u043b\u044c\u0442\u0430\u0442\u043e\u0432: \u0437\u0430\u043a\u043e\u0434\u0438\u0440\u0443\u0439 \u043a\u0430\u043d\u0430\u043b \u0432\u044b\u0431\u0440\u0430\u043d\u043d\u044b\u043c \u043c\u0435\u0442\u043e\u0434\u043e\u043c."
        var ratio = bestBy(compression, "ratio")
        var encode = bestBy(compression, "encodeMbPerSec")
        var decode = bestBy(verify, "decodeMbPerSec")
        var parts = []
        if (ratio) parts.push("\u041b\u0443\u0447\u0448\u0435\u0435 \u0441\u0436\u0430\u0442\u0438\u0435: " + ratio.pipelineLabel + " (" + ratio.ratioText + ")")
        if (encode) parts.push("\u0411\u044b\u0441\u0442\u0440\u0435\u0435 \u043a\u043e\u0434\u0438\u0440\u0443\u0435\u0442: " + encode.pipelineLabel + " (" + encode.encodeText + ")")
        if (decode) parts.push("\u0411\u044b\u0441\u0442\u0440\u0435\u0435 \u0434\u0435\u043a\u043e\u0434\u0438\u0440\u0443\u0435\u0442: " + (decode.pipelineLabel || decode.pipelineId) + " (" + decode.decodeText + ")")
        if (root.latestVerifyRow()) parts.push(root.latestVerifyRow().verified ? "\u041f\u0440\u043e\u0432\u0435\u0440\u043a\u0430: \u0434\u0435\u043a\u043e\u0434 \u0441\u043e\u0432\u043f\u0430\u0434\u0430\u0435\u0442 \u0441 JSONL" : "\u041f\u0440\u043e\u0432\u0435\u0440\u043a\u0430: \u0435\u0441\u0442\u044c \u0440\u0430\u0441\u0445\u043e\u0436\u0434\u0435\u043d\u0438\u0435")
        return parts.join("  |  ")
    }

    onActivePageChanged: {
        if (root.activePage === 1) root.selectFirstEncodedForDecode()
    }

    component SectionCard: Rectangle {
        default property alias content: slot.data
        property string title: ""
        property string subtitle: ""
        property int cardHeight: 180
        radius: 8
        color: root.panelColor
        border.color: root.borderColor
        border.width: 1
        Layout.fillWidth: true
        implicitHeight: cardHeight
        Layout.preferredHeight: cardHeight

        RowLayout {
            id: headerRow
            x: 11
            y: 9
            width: parent.width - 22
            height: 22
            visible: title.length > 0 || subtitle.length > 0
            Label { text: title; color: root.textColor; font.pixelSize: 14; font.bold: true }
            Item { Layout.fillWidth: true }
            Label { text: subtitle; color: root.mutedTextColor; font.pixelSize: 10; elide: Text.ElideRight }
        }

        Item {
            id: slot
            x: 11
            y: headerRow.visible ? 39 : 11
            width: parent.width - 22
            height: parent.height - y - 11
        }
    }

    component MetricCard: Rectangle {
        property string title: ""
        property string value: ""
        property string subtext: ""
        property color accent: root.accentColor
        Layout.fillWidth: true
        Layout.preferredHeight: 62
        radius: 8
        color: root.panelSoftColor
        border.color: root.borderColor
        border.width: 1

        Rectangle { width: 3; radius: 2; color: accent; anchors.left: parent.left; anchors.top: parent.top; anchors.bottom: parent.bottom }
        ColumnLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 10
            anchors.topMargin: 8
            anchors.bottomMargin: 7
            spacing: 4
            Label { text: title; color: root.mutedTextColor; font.pixelSize: 10 }
            Label { Layout.fillWidth: true; text: value; color: root.textColor; font.pixelSize: 16; font.bold: true; elide: Text.ElideRight }
            Label { Layout.fillWidth: true; text: subtext; color: root.mutedTextColor; font.pixelSize: 10; elide: Text.ElideRight }
        }
    }

    component DarkButton: Rectangle {
        property string text: ""
        property bool enabledValue: true
        signal clicked()
        radius: 6
        implicitWidth: label.implicitWidth + 16
        implicitHeight: 26
        color: enabledValue ? (mouse.containsMouse ? "#2b303a" : root.panelSoftColor) : root.panelDeepColor
        border.color: enabledValue ? root.accentColor : root.borderColor
        border.width: enabledValue ? 1 : 1
        opacity: enabledValue ? 1.0 : 0.55
        Text { id: label; anchors.centerIn: parent; text: parent.text; color: enabledValue ? root.textColor : root.mutedTextColor; font.pixelSize: 11; font.bold: true }
        MouseArea { id: mouse; anchors.fill: parent; hoverEnabled: true; enabled: parent.enabledValue; onClicked: parent.clicked() }
    }

    component SegButton: Rectangle {
        property string text: ""
        property bool selected: false
        signal clicked()
        radius: 6
        implicitWidth: label.implicitWidth + 18
        implicitHeight: 28
        color: selected ? Qt.rgba(root.accentColor.r, root.accentColor.g, root.accentColor.b, 0.18) : root.panelDeepColor
        border.color: selected ? root.accentColor : root.borderColor
        Text { id: label; anchors.centerIn: parent; text: parent.text; color: selected ? root.textColor : root.mutedTextColor; font.pixelSize: 11; font.bold: true }
        MouseArea { anchors.fill: parent; onClicked: parent.clicked() }
    }

    component DarkCombo: ComboBox {
        id: combo
        property string caption: ""
        property bool requireEncodedArtifact: false
        Layout.fillWidth: true
        Layout.preferredHeight: 42
        spacing: 0
        font.pixelSize: 12
        contentItem: Column {
            leftPadding: 10
            rightPadding: 28
            topPadding: 5
            spacing: 2
            Text { text: combo.caption; color: root.mutedTextColor; font.pixelSize: 10; elide: Text.ElideRight; width: combo.width - 40 }
            Text { text: combo.displayText.length > 0 ? combo.displayText : "не выбрано"; color: root.textColor; font.pixelSize: 12; font.bold: true; elide: Text.ElideRight; width: combo.width - 40 }
        }
        indicator: Text { x: combo.width - 22; y: 15; text: "v"; color: root.mutedTextColor; font.pixelSize: 12 }
        background: Rectangle { radius: 7; color: root.panelDeepColor; border.color: combo.hovered ? root.accentColor : root.borderColor; border.width: 1 }
        popup: Popup {
            y: combo.height + 4
            width: combo.width
            implicitHeight: Math.min(contentItem.implicitHeight, 260)
            padding: 1
            background: Rectangle { color: root.panelColor; border.color: root.borderColor; radius: 7 }
            contentItem: ListView {
                clip: true
                implicitHeight: contentHeight
                model: combo.popup.visible ? combo.delegateModel : null
                currentIndex: combo.highlightedIndex
            }
        }
        delegate: ItemDelegate {
            required property var modelData
            width: combo.width
            height: 30
            enabled: !combo.requireEncodedArtifact || root.compressionVm.hasEncodedArtifact(modelData.id)
            contentItem: Text { leftPadding: 10; text: modelData.label + (root.compressionVm.hasEncodedArtifact(modelData.id) ? "  [ready]" : (combo.requireEncodedArtifact ? "  - no artifact" : "")); color: parent.enabled ? root.textColor : root.mutedTextColor; font.pixelSize: 12; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
            background: Rectangle { color: highlighted ? Qt.rgba(root.accentColor.r, root.accentColor.g, root.accentColor.b, 0.16) : root.panelColor }
        }
    }

    component EmptyBox: Rectangle {
        property string text: ""
        Layout.fillWidth: true
        Layout.preferredHeight: 72
        radius: 7
        color: root.panelDeepColor
        border.color: root.borderColor
        Label { anchors.centerIn: parent; text: parent.text; color: root.mutedTextColor; font.pixelSize: 12 }
    }

    component VerticalRatingChart: Rectangle {
        id: chartBox
        property var rowsModel: []
        property string emptyText: ""
        property string mode: "speed"
        property color barColor: root.accentColor
        Layout.fillWidth: true
        Layout.preferredHeight: 206
        width: parent ? parent.width : 0
        height: parent ? parent.height : 206
        radius: 7
        color: root.panelDeepColor
        border.color: root.borderColor
        border.width: 1

        function chartSummary() {
            var rows = []
            if (mode === "size") rows.push({ isReference: true, pipelineLabel: "JSONL", ratioText: "1.0x" })
            for (var i = 0; i < rowsModel.length; ++i) rows.push(rowsModel[i])
            if (rows.length < 1) return emptyText
            var out = []
            for (var j = 0; j < rows.length && j < 32; ++j) {
                var row = rows[j]
                var value = mode === "speed" ? row.encodeText : (mode === "decode" ? row.decodeText : (row.isReference ? "source" : row.ratioText))
                out.push((j + 1) + ". " + (row.pipelineLabel || row.pipelineId || "JSONL") + " - " + value)
            }
            if (rows.length > 32) out.push("... +" + (rows.length - 32) + " more")
            return out.join("\n")
        }

        Canvas {
            id: chart
            anchors.fill: parent
            anchors.margins: 12
            antialiasing: true
            onPaint: {
                var ctx = getContext("2d")
                ctx.clearRect(0, 0, width, height)
                var sourceRows = rowsModel || []
                if (sourceRows.length < 1) {
                    ctx.fillStyle = root.mutedTextColor
                    ctx.font = "11px sans-serif"
                    ctx.fillText(emptyText, 18, height / 2)
                    return
                }
                var rows = []
                if (mode === "size") rows.push({ isReference: true, pipelineLabel: "JSONL", ratio: 1.0, ratioText: "1.0x" })
                for (var r = 0; r < sourceRows.length; ++r) rows.push(sourceRows[r])
                var worstSpeed = 0
                for (var i = 0; i < rows.length; ++i) {
                    if (mode === "speed" || mode === "decode") {
                        var speed = mode === "decode" ? rows[i].decodeMbPerSec : rows[i].encodeMbPerSec
                        if (speed > 0) worstSpeed = worstSpeed === 0 ? speed : Math.min(worstSpeed, speed)
                    }
                }
                function metricFor(row) {
                    if (mode === "speed") return worstSpeed <= 0 ? 0 : row.encodeMbPerSec / worstSpeed
                    if (mode === "decode") return worstSpeed <= 0 ? 0 : row.decodeMbPerSec / worstSpeed
                    return row.ratio
                }
                function valueText(row) {
                    if (mode === "speed") return row.encodeText
                    if (mode === "decode") return row.decodeText
                    return row.isReference ? "source" : row.ratioText
                }
                function shortLabel(row, index, compact) {
                    if (compact) return String(index + 1)
                    var label = row.isReference ? "JSONL" : (row.pipelineLabel || row.pipelineId || "method")
                    if (label === "Zstd JSONL blocks v1") return "Zstd"
                    if (label === "Raw JSONL blocks v1") return "Raw"
                    return label.length > 11 ? label.substring(0, 10) + "..." : label
                }
                var compact = rows.length > 12
                var left = 38
                var bottom = height - (compact ? 34 : 52)
                var top = 16
                var chartW = width - left - 16
                var chartH = bottom - top
                var maxValue = 1
                rows.sort(function(a, b) { return metricFor(b) - metricFor(a) })
                for (var j = 0; j < rows.length; ++j) maxValue = Math.max(maxValue, metricFor(rows[j]))
                ctx.strokeStyle = root.borderColor
                ctx.lineWidth = 1
                ctx.beginPath(); ctx.moveTo(left, top); ctx.lineTo(left, bottom); ctx.lineTo(width - 8, bottom); ctx.stroke()
                ctx.fillStyle = root.mutedTextColor
                ctx.font = "9px sans-serif"
                ctx.fillText("0", 12, bottom + 3)
                ctx.fillText(maxValue.toFixed(1) + "x", 8, top + 4)
                var gap = compact ? Math.max(2, Math.min(7, chartW / Math.max(1, rows.length * 7))) : (rows.length > 1 ? 28 : 18)
                var barW = compact ? Math.max(3, (chartW - gap * (rows.length + 1)) / rows.length) : Math.min(38, Math.max(22, (chartW - gap * (rows.length + 1)) / rows.length))
                var startX = left + gap
                for (var k = 0; k < rows.length; ++k) {
                    var metric = metricFor(rows[k])
                    var h = (metric / maxValue) * (chartH - 8)
                    var x = startX + k * (barW + gap)
                    var y = bottom - h
                    ctx.fillStyle = rows[k].isReference ? root.borderColor : barColor
                    ctx.fillRect(x, y, barW, h)
                    ctx.fillStyle = root.textColor
                    ctx.font = compact ? "8px sans-serif" : "10px sans-serif"
                    ctx.fillText(shortLabel(rows[k], k, compact), x, bottom + 15)
                    if (!compact || barW > 18) {
                        ctx.fillStyle = rows[k].isReference ? root.mutedTextColor : (mode === "size" ? root.greenColor : barColor)
                        ctx.font = "9px sans-serif"
                        ctx.fillText(valueText(rows[k]), x - 4, bottom + (compact ? 28 : 34))
                    }
                }
                if (compact) {
                    ctx.fillStyle = root.mutedTextColor
                    ctx.font = "9px sans-serif"
                    ctx.fillText("compact: hover for full names", left, height - 4)
                }
            }
        }
        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            ToolTip.visible: containsMouse && chartBox.rowsModel.length > 0
            ToolTip.delay: 350
            ToolTip.timeout: 12000
            ToolTip.text: chartBox.chartSummary()
        }
        onRowsModelChanged: chart.requestPaint()
        onVisibleChanged: chart.requestPaint()
        onWidthChanged: chart.requestPaint()
    }

    Connections {
        target: root.compressionVm
        function onSessionsChanged() { root.syncSelections() }
        function onSelectedSessionChanged() { root.syncSelections() }
        function onSelectedChannelChanged() { if (root.activePage === 1) root.selectFirstEncodedForDecode(); root.syncSelections() }
        function onSelectedPipelineChanged() { root.syncSelections() }
        function onArtifactAvailabilityChanged() { root.syncSelections() }
    }

    Dialog {
        id: filePreviewDialog
        modal: true
        padding: 0
        topPadding: 0
        bottomPadding: 0
        leftPadding: 0
        rightPadding: 0
        width: Math.min(root.width - 80, 980)
        height: Math.min(root.height - 80, 720)
        x: Math.max(20, (root.width - width) / 2)
        y: Math.max(20, (root.height - height) / 2)
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
        background: Rectangle { color: root.panelColor; border.color: root.borderColor; radius: 8 }
        header: Rectangle {
            height: 42
            color: root.panelDeepColor
            border.color: root.borderColor
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                spacing: 8
                Label { Layout.fillWidth: true; text: root.previewTitle; color: root.textColor; font.pixelSize: 13; font.bold: true; elide: Text.ElideRight }
                Rectangle {
                    Layout.preferredWidth: 28
                    Layout.preferredHeight: 28
                    radius: 6
                    color: closeMouse.containsMouse ? root.panelSoftColor : root.panelDeepColor
                    border.color: closeMouse.containsMouse ? root.accentColor : root.borderColor
                    Text { anchors.centerIn: parent; text: "x"; color: root.textColor; font.pixelSize: 14; font.bold: true }
                    MouseArea { id: closeMouse; anchors.fill: parent; hoverEnabled: true; onClicked: filePreviewDialog.close() }
                }
            }
        }
        footer: null
        contentItem: Rectangle {
            color: root.panelColor
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8
                Label { Layout.fillWidth: true; text: root.previewData.path || ""; color: root.mutedTextColor; font.pixelSize: 11; elide: Text.ElideMiddle }
                Label { Layout.fillWidth: true; text: root.previewData.summaryText || root.previewData.error || ""; color: root.previewData.ok ? root.textColor : root.badColor; font.pixelSize: 12; wrapMode: Text.WordWrap }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    visible: root.previewData.metadataRows && root.previewData.metadataRows.length > 0
                    Repeater {
                        model: root.previewData.metadataRows || []
                        delegate: Rectangle {
                            required property var modelData
                            height: 26
                            width: Math.min(260, metaLabel.implicitWidth + 22)
                            radius: 6
                            color: root.panelDeepColor
                            border.color: root.borderColor
                            Label { id: metaLabel; anchors.centerIn: parent; text: modelData.label + ": " + modelData.value; color: root.mutedTextColor; font.pixelSize: 10; elide: Text.ElideRight; width: parent.width - 12 }
                        }
                    }
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    background: Rectangle { color: root.panelDeepColor; border.color: root.borderColor; radius: 6 }
                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                        contentItem: Rectangle { implicitWidth: 6; radius: 3; color: parent.active ? root.accentColor : root.borderColor }
                        background: Rectangle { color: root.panelDeepColor }
                    }
                    ScrollBar.horizontal: ScrollBar {
                        policy: ScrollBar.AsNeeded
                        contentItem: Rectangle { implicitHeight: 6; radius: 3; color: parent.active ? root.accentColor : root.borderColor }
                        background: Rectangle { color: root.panelDeepColor }
                    }
                    TextArea {
                        text: root.previewData.contentText || ""
                        readOnly: true
                        selectByMouse: true
                        wrapMode: TextEdit.NoWrap
                        color: root.textColor
                        selectedTextColor: root.panelDeepColor
                        selectionColor: root.accentColor
                        font.family: "monospace"
                        font.pixelSize: 11
                        background: Rectangle { color: root.panelDeepColor }
                    }
                }
                ScrollView {
                    Layout.fillWidth: true
                    Layout.preferredHeight: root.previewData.blockRows && root.previewData.blockRows.length > 0 ? 110 : 0
                    visible: root.previewData.blockRows && root.previewData.blockRows.length > 0
                    clip: true
                    background: Rectangle { color: root.panelDeepColor; border.color: root.borderColor; radius: 6 }
                    ScrollBar.vertical: ScrollBar {
                        policy: ScrollBar.AsNeeded
                        contentItem: Rectangle { implicitWidth: 6; radius: 3; color: parent.active ? root.accentColor : root.borderColor }
                        background: Rectangle { color: root.panelDeepColor }
                    }
                    ColumnLayout {
                        width: parent.width
                        Repeater {
                            model: root.previewData.blockRows || []
                            delegate: Label { required property var modelData; Layout.fillWidth: true; text: modelData.text; color: root.mutedTextColor; font.family: "monospace"; font.pixelSize: 10; elide: Text.ElideRight }
                        }
                    }
                }
            }
        }
    }

    ScrollView {
        anchors.fill: parent
        contentWidth: availableWidth
        clip: true

        ColumnLayout {
            width: root.width - 32
            x: 16
            spacing: 8

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 6
                spacing: 8

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Label { text: "Сжатие данных"; color: root.textColor; font.pixelSize: 20; font.bold: true }
                    Label { text: "Выбери сессию, канал и метод. Результаты сохраняются как .hfc, краткие метрики лежат рядом с файлом."; color: root.mutedTextColor; font.pixelSize: 11; elide: Text.ElideRight; Layout.fillWidth: true }
                }

                RowLayout {
                    spacing: 8
                    SegButton { text: "Кодирование"; selected: root.activePage === 0; onClicked: root.activePage = 0 }
                    SegButton { text: "\u0414\u0435\u043a\u043e\u0434 \u0438 \u043f\u0440\u043e\u0432\u0435\u0440\u043a\u0430"; selected: root.activePage === 1; onClicked: { root.activePage = 1; root.selectFirstEncodedForDecode() } }
                }
            }
                SectionCard {
                title: "Входные данные: " + root.selectedChannelTitle()
                cardHeight: 146
                subtitle: root.compressionVm.emptyStateText
                ColumnLayout {
                    width: parent.width
                    spacing: 8

                    GridLayout {
                        width: parent.width
                        columns: 5
                        columnSpacing: 10
                        rowSpacing: 10

                        DarkCombo {
                            id: sessionBox
                            Layout.columnSpan: 2
                            caption: "Сессия"
                            textRole: "label"
                            valueRole: "id"
                            model: root.compressionVm.sessions
                            onActivated: root.compressionVm.setSelectedSessionId(currentValue)
                            Component.onCompleted: root.syncSelections()
                        }

                        DarkCombo {
                            id: channelBox
                            Layout.preferredWidth: 120
                            caption: "Канал"
                            textRole: "label"
                            valueRole: "id"
                            model: root.compressionVm.channelChoices
                            onActivated: root.compressionVm.setSelectedChannel(currentValue)
                            Component.onCompleted: root.syncSelections()
                        }

                        DarkCombo {
                            id: pipelineBox
                            Layout.preferredWidth: 230
                            caption: "Метод"
                            requireEncodedArtifact: root.activePage === 1
                            textRole: "label"
                            valueRole: "id"
                            model: root.compressionVm.pipelines
                            onActivated: root.compressionVm.setSelectedPipelineId(currentValue)
                            Component.onCompleted: root.syncSelections()
                        }

                        RowLayout {
                            Layout.preferredWidth: 520
                            Layout.preferredHeight: 42
                            spacing: 8
                            DarkButton { text: "Обновить"; enabledValue: true; onClicked: root.compressionVm.reloadSessions() }
                            DarkButton {
                                text: root.activePage === 0 ? (root.compressionVm.running ? "Кодирую..." : "Кодировать") : (root.compressionVm.verifying ? "Декодирую..." : "Раскодировать")
                                enabledValue: root.activePage === 0 ? root.compressionVm.canRun : root.compressionVm.canDecodeVerify
                                onClicked: root.activePage === 0 ? root.compressionVm.runCompression() : root.compressionVm.decodeVerifySelected()
                            }
                            DarkButton {
                                text: root.activePage === 0 ? "К декоду" : "Вернуться"
                                enabledValue: true
                                onClicked: {
                                    root.activePage = root.activePage === 0 ? 1 : 0
                                    if (root.activePage === 1) root.selectFirstEncodedForDecode()
                                }
                            }
                            DarkButton { text: "JSONL"; enabledValue: root.compressionVm.inputFile.length > 0; onClicked: root.openJsonlPreview() }
                            DarkButton { text: "\u0411\u0438\u043d\u0430\u0440\u044c"; enabledValue: root.compressionVm.selectedArtifactAvailable; onClicked: root.openArtifactPreview() }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        Rectangle {
                            Layout.fillWidth: true
                            Layout.preferredHeight: 30
                            radius: 7
                            color: root.panelDeepColor
                            border.color: root.borderColor
                            Label { anchors.fill: parent; anchors.margins: 8; text: root.compressionVm.inputFile.length > 0 ? "JSONL: " + root.compressionVm.inputFile : "\u041d\u0435\u0442 JSONL \u0444\u0430\u0439\u043b\u0430 \u0434\u043b\u044f \u0432\u044b\u0431\u0440\u0430\u043d\u043d\u043e\u0439 \u0441\u0435\u0441\u0441\u0438\u0438"; color: root.mutedTextColor; font.pixelSize: 11; elide: Text.ElideMiddle; verticalAlignment: Text.AlignVCenter }
                        }

                    }
                }
            }


            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                visible: root.activePage === 0

                GridLayout {
                    Layout.fillWidth: true
                    columns: 4
                    columnSpacing: 10
                    rowSpacing: 10
                    MetricCard { title: "Сжатие"; value: root.latestRunRow() ? root.latestRunRow().ratioText : "0.000x"; subtext: "во сколько раз меньше"; accent: root.greenColor }
                    MetricCard { title: "Скорость кодирования"; value: root.latestRunRow() ? root.latestRunRow().encodeText : "0.00 MB/s"; subtext: "MB/s"; accent: root.accentColor }
                    MetricCard { title: "Размер"; value: root.latestRunRow() ? root.latestRunRow().sizeText : "0 bytes -> 0 bytes"; subtext: "JSONL -> .hfc"; accent: root.warnColor }
                    MetricCard { title: "Статус"; value: root.latestRunRow() ? root.shortStatus(root.latestRunRow().status) : root.shortStatus(root.compressionVm.statusText); subtext: root.selectedChannelTitle() + " / " + (root.latestRunRow() ? root.latestRunRow().pipelineLabel : root.compressionVm.selectedPipelineLabel); accent: root.violetColor }
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    SectionCard {
                        Layout.fillWidth: true
                        title: "Скорость кодирования: " + root.selectedChannelTitle()
                        cardHeight: 270
                        subtitle: "худший результат = 1.0x"
                        VerticalRatingChart { rowsModel: root.compressionVm.compressionBars; mode: "speed"; barColor: root.accentColor; emptyText: "Сначала закодируй выбранный канал." }
                    }
                    SectionCard {
                        Layout.fillWidth: true
                        title: "Размер после сжатия: " + root.selectedChannelTitle()
                        cardHeight: 270
                        subtitle: "эталонный JSONL = 1.0x"
                        VerticalRatingChart { rowsModel: root.compressionVm.compressionBars; mode: "size"; barColor: root.greenColor; emptyText: "После кодирования появится рейтинг размера." }
                    }
                }
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8
                visible: root.activePage === 1

                GridLayout {
                    Layout.fillWidth: true
                    columns: 4
                    columnSpacing: 10
                    rowSpacing: 10
                    MetricCard { title: "Проверка"; value: root.latestVerifyRow() ? (root.latestVerifyRow().verified ? "совпадает" : "не совпало") : "не проверено"; subtext: root.latestVerifyRow() ? root.latestVerifyRow().status : "проверка еще не запускалась"; accent: root.latestVerifyRow() && root.latestVerifyRow().verified ? root.greenColor : root.warnColor }
                    MetricCard { title: "Скорость декода"; value: root.latestVerifyRow() ? root.latestVerifyRow().decodeText : "0.00 MB/s"; subtext: "без сохранения файла на диск"; accent: root.violetColor }
                    MetricCard { title: "Декод / эталон"; value: root.latestVerifyRow() ? root.latestVerifyRow().decodedSizeText : "нет данных"; subtext: root.latestVerifyRow() ? "эталон " + root.latestVerifyRow().canonicalSizeText : root.selectedChannelTitle() + ", сравнение в ОЗУ"; accent: root.accentColor }
                    MetricCard { title: "Расхождение"; value: root.latestVerifyRow() ? root.latestVerifyRow().mismatchPercentText : "0.00%"; subtext: root.latestVerifyRow() ? (root.latestVerifyRow().verified ? "байты совпадают" : "есть расхождения") : "проверка еще не запускалась"; accent: root.badColor }
                }
SectionCard {
                    title: "Скорость декодирования: " + root.selectedChannelTitle()
                    cardHeight: 270
                    subtitle: "худший результат = 1.0x"
                    VerticalRatingChart { rowsModel: root.compressionVm.decodeBars; mode: "decode"; barColor: root.violetColor; emptyText: "После проверки появится рейтинг декодирования." }
                }
                SectionCard {
                    title: "Проверка эталона: " + root.selectedChannelTitle()
                    cardHeight: 160
                    subtitle: "скорость декодирования и процент несовпадения"
                    ColumnLayout {
                        width: parent.width
                        spacing: 8
                        EmptyBox { visible: root.compressionVm.verifyRows.length === 0; text: "Проверок пока нет. Запусти декодирование после кодирования .hfc." }
                        Repeater {
                            model: root.compressionVm.decodeBars
                            delegate: Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                Layout.preferredHeight: 46
                                radius: 7
                                color: root.panelDeepColor
                                border.color: modelData.verified ? root.greenColor : root.badColor
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    spacing: 8
                                    Label { text: modelData.streamLabel; color: root.textColor; font.bold: true; Layout.preferredWidth: 86; elide: Text.ElideRight; clip: true }
                                    Label { text: modelData.pipelineLabel || modelData.pipelineId; color: root.mutedTextColor; elide: Text.ElideRight; Layout.fillWidth: true; clip: true }
                                    Label { text: modelData.decodeText; color: root.violetColor; font.bold: true; Layout.preferredWidth: 116; horizontalAlignment: Text.AlignRight; elide: Text.ElideRight; clip: true }
                                    Label { text: modelData.verified ? "совпадает" : ("не совпало: " + modelData.mismatchPercentText); color: modelData.verified ? root.greenColor : root.badColor; font.bold: true; Layout.preferredWidth: 168; horizontalAlignment: Text.AlignHCenter; elide: Text.ElideRight; clip: true }
                                    Label { text: modelData.sizeText; color: root.mutedTextColor; elide: Text.ElideRight; horizontalAlignment: Text.AlignRight; Layout.preferredWidth: 250; clip: true }
                                }
                            }
                        }
                    }
                }
            }

            Item { Layout.fillHeight: true; Layout.preferredHeight: 18 }
        }
    }
}
