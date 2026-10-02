import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: strip

    required property var backtestVm
    property color panelColor: "#22252d"
    property color panelDeepColor: "#15171c"
    property color borderColor: "#343844"
    property color textColor: "#f1f4f8"
    property color mutedTextColor: "#a8afbd"
    property color accentColor: "#24c2cb"
    property color goodColor: "#82d46b"

    property int refreshRevision: 0

    function touch() {
        refreshRevision += 1
    }

    function safeValue(name, fallback) {
        var revision = refreshRevision
        if (revision < 0 || !backtestVm)
            return fallback
        try {
            var value = backtestVm[name]
            if (value === undefined || value === null)
                return fallback
            return value
        } catch (error) {
            return fallback
        }
    }

    function safeList(nameList) {
        for (var i = 0; i < nameList.length; ++i) {
            var value = safeValue(nameList[i], [])
            if (value && value.length !== undefined)
                return value
        }
        return []
    }

    function canCall(name) {
        if (!backtestVm)
            return false
        try {
            return typeof backtestVm[name] === "function"
        } catch (error) {
            return false
        }
    }

    function rowValue(row, fallback) {
        if (!row)
            return fallback
        if (row.id !== undefined) return row.id
        if (row.value !== undefined) return row.value
        if (row.key !== undefined) return row.key
        if (row.index !== undefined) return row.index
        return fallback
    }

    function rowLabel(row, fallback) {
        if (!row)
            return fallback
        if (row.label !== undefined && String(row.label).length > 0) return row.label
        if (row.text !== undefined && String(row.text).length > 0) return row.text
        if (row.name !== undefined && String(row.name).length > 0) return row.name
        var value = rowValue(row, fallback)
        return String(value)
    }

    function valuesEqual(lhs, rhs) {
        if (String(lhs) === String(rhs))
            return true
        var leftNumber = Number(lhs)
        var rightNumber = Number(rhs)
        return isFinite(leftNumber) && isFinite(rightNumber) && leftNumber === rightNumber
    }

    function limitedRows(rows, limit) {
        var out = []
        var count = Math.min(limit, rows.length)
        for (var i = 0; i < count; ++i)
            out.push(rows[i])
        return out
    }

    function choiceRow(rows, selectedValue) {
        for (var i = 0; i < rows.length; ++i) {
            if (valuesEqual(rowValue(rows[i], i), selectedValue))
                return rows[i]
        }
        return null
    }

    function selectedChoiceLabel(rows, selectedValue, fallback) {
        var row = choiceRow(rows, selectedValue)
        return row ? rowLabel(row, fallback) : fallback
    }

    function universeLabel() {
        var id = safeValue("batchUniverseId", "")
        return selectedChoiceLabel(safeList(["batchUniverseChoices"]), id, id.length > 0 ? id : "none")
    }

    function strategyLabel() {
        var id = safeValue("selectedStrategy", "")
        return selectedChoiceLabel(safeList(["strategyChoices"]), id, id.length > 0 ? id : "none")
    }

    function primaryLegChoices() {
        var explicitChoices = safeList(["selectedPrimaryLegChoices", "primaryLegChoices", "selectedPrimaryLegIndexChoices"])
        if (explicitChoices.length > 0)
            return explicitChoices
        var legs = safeList(["selectedSessionLegs"])
        var out = []
        for (var i = 0; i < legs.length; ++i) {
            var leg = legs[i]
            var index = leg.index !== undefined && leg.index !== null ? leg.index : i
            out.push({
                "id": index,
                "value": index,
                "label": leg.label || ("leg " + String(index)),
                "rightText": leg.tradable === false ? "no-trade" : ""
            })
        }
        return out
    }

    function tradeModeChoices() {
        return safeList(["selectedTradeModeChoices", "tradeModeChoices"])
    }

    function selectedPrimaryLegIndex() {
        return safeValue("selectedPrimaryLegIndex", "pending")
    }

    function selectedTradeMode() {
        return safeValue("selectedTradeMode", "pending")
    }

    function primaryFallbackLabel() {
        var value = selectedPrimaryLegIndex()
        if (value === "pending")
            return "pending"
        var numeric = Number(value)
        if (isFinite(numeric) && numeric < 0)
            return "auto"
        return "leg " + String(value)
    }

    function tradeFallbackLabel() {
        var value = selectedTradeMode()
        return value === "pending" || String(value).length === 0 ? "pending" : String(value)
    }

    function parameterSummary() {
        var rows = safeList(["strategyParameters"])
        if (rows.length === 0)
            return "Params: defaults"
        var parts = []
        for (var i = 0; i < rows.length && i < 4; ++i) {
            var row = rows[i]
            var label = row.label || row.key || ("p" + String(i + 1))
            var value = row.value !== undefined && row.value !== null ? row.value : ""
            var minValue = row.min !== undefined && row.min !== null ? row.min : ""
            var maxValue = row.max !== undefined && row.max !== null ? row.max : ""
            var stepValue = row.step !== undefined && row.step !== null ? row.step : ""
            if (row.isChoice === true) {
                parts.push(label + "=" + String(value))
            } else if (row.mode === "fixed") {
                parts.push(label + "=" + String(value))
            } else {
                parts.push(label + "=" + String(minValue) + ".." + String(maxValue) + "/" + String(stepValue))
            }
        }
        if (rows.length > 4)
            parts.push("+" + String(rows.length - 4))
        return "Params: " + parts.join(" | ")
    }

    function choosePrimary(value) {
        if (!canCall("setSelectedPrimaryLegIndex"))
            return
        var numeric = Number(value)
        if (!isFinite(numeric))
            return
        backtestVm.setSelectedPrimaryLegIndex(numeric)
        touch()
    }

    function chooseTradeMode(value) {
        if (!canCall("setSelectedTradeMode"))
            return
        backtestVm.setSelectedTradeMode(String(value))
        touch()
    }

    Layout.preferredHeight: 38
    color: panelDeepColor
    border.color: borderColor
    border.width: 1
    radius: 6
    clip: true

    Connections {
        target: strip.backtestVm
        ignoreUnknownSignals: true
        function onBatchConfigChanged() { strip.touch() }
        function onSelectedStrategyChanged() { strip.touch() }
        function onStrategyParametersChanged() { strip.touch() }
        function onMultiSessionChanged() { strip.touch() }
        function onSelectedPrimaryLegIndexChanged() { strip.touch() }
        function onSelectedPrimaryLegChanged() { strip.touch() }
        function onPrimaryLegChanged() { strip.touch() }
        function onPrimaryLegChoicesChanged() { strip.touch() }
        function onSelectedPrimaryLegChoicesChanged() { strip.touch() }
        function onSelectedTradeModeChanged() { strip.touch() }
        function onTradeModeChanged() { strip.touch() }
        function onTradeModeChoicesChanged() { strip.touch() }
        function onSelectedTradeModeChoicesChanged() { strip.touch() }
    }

    component ContextChip: Rectangle {
        property string title: ""
        property string value: ""
        property bool selected: false
        property bool enabledValue: false
        property color accent: strip.accentColor
        signal clicked()

        width: Math.min(210, Math.max(92, titleText.implicitWidth + valueText.implicitWidth + 28))
        height: 24
        radius: 5
        color: selected ? Qt.rgba(accent.r, accent.g, accent.b, 0.18) : strip.panelColor
        border.color: selected ? accent : strip.borderColor
        border.width: 1
        opacity: enabledValue || selected ? 1.0 : 0.74

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 8
            anchors.rightMargin: 8
            spacing: 5
            Text {
                id: titleText
                text: title
                color: strip.mutedTextColor
                font.pixelSize: 10
                elide: Text.ElideRight
            }
            Text {
                id: valueText
                Layout.fillWidth: true
                text: value
                color: selected ? strip.textColor : strip.mutedTextColor
                font.pixelSize: 11
                font.bold: selected
                elide: Text.ElideRight
            }
        }

        MouseArea {
            anchors.fill: parent
            hoverEnabled: true
            enabled: parent.enabledValue
            onClicked: parent.clicked()
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 8

        Label {
            text: "Context"
            color: strip.textColor
            font.pixelSize: 11
            font.bold: true
            Layout.preferredWidth: 54
        }

        Flickable {
            Layout.fillWidth: true
            Layout.preferredHeight: 26
            contentWidth: Math.max(width, contextRow.implicitWidth)
            contentHeight: contextRow.implicitHeight
            boundsBehavior: Flickable.StopAtBounds
            clip: true

            Row {
                id: contextRow
                height: 26
                spacing: 6

                ContextChip {
                    title: "Universe"
                    value: strip.universeLabel()
                    selected: true
                    accent: strip.goodColor
                }

                ContextChip {
                    title: "Strategy"
                    value: strip.strategyLabel()
                    selected: true
                }

                Repeater {
                    model: strip.primaryLegChoices().length > 0 ? strip.limitedRows(strip.primaryLegChoices(), 5) : []
                    delegate: ContextChip {
                        required property var modelData
                        required property int index
                        property var choiceValue: strip.rowValue(modelData, index)
                        title: "Primary"
                        value: strip.rowLabel(modelData, "leg " + String(choiceValue))
                        selected: strip.valuesEqual(choiceValue, strip.selectedPrimaryLegIndex())
                        enabledValue: !selected && strip.canCall("setSelectedPrimaryLegIndex")
                        accent: strip.goodColor
                        onClicked: strip.choosePrimary(choiceValue)
                    }
                }

                ContextChip {
                    visible: strip.primaryLegChoices().length === 0
                    title: "Primary"
                    value: strip.primaryFallbackLabel()
                    selected: true
                    accent: strip.goodColor
                }

                ContextChip {
                    visible: strip.primaryLegChoices().length > 5
                    title: "Primary"
                    value: "+" + String(strip.primaryLegChoices().length - 5)
                }

                Repeater {
                    model: strip.tradeModeChoices().length > 0 ? strip.limitedRows(strip.tradeModeChoices(), 4) : []
                    delegate: ContextChip {
                        required property var modelData
                        required property int index
                        property var choiceValue: strip.rowValue(modelData, index)
                        title: "Mode"
                        value: strip.rowLabel(modelData, String(choiceValue))
                        selected: strip.valuesEqual(choiceValue, strip.selectedTradeMode())
                        enabledValue: !selected && strip.canCall("setSelectedTradeMode")
                        onClicked: strip.chooseTradeMode(choiceValue)
                    }
                }

                ContextChip {
                    visible: strip.tradeModeChoices().length === 0
                    title: "Mode"
                    value: strip.tradeFallbackLabel()
                    selected: true
                }

                ContextChip {
                    visible: strip.tradeModeChoices().length > 4
                    title: "Mode"
                    value: "+" + String(strip.tradeModeChoices().length - 4)
                }
            }
        }

        Label {
            text: strip.parameterSummary()
            color: strip.mutedTextColor
            font.pixelSize: 11
            elide: Text.ElideRight
            Layout.preferredWidth: 360
        }
    }
}
