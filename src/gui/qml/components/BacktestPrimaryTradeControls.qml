import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: controls

    required property var backtestVm
    property color panelColor: "#1b1d23"
    property color panelDeepColor: "#15171c"
    property color borderColor: "#343844"
    property color textColor: "#f1f4f8"
    property color mutedTextColor: "#a8afbd"
    property color accentColor: "#24c2cb"

    spacing: 8

    function enabledLegRows() {
        var rows = []
        var legs = controls.backtestVm.selectedSessionLegs || []
        for (var i = 0; i < legs.length; ++i) {
            if (legs[i] && legs[i].enabled !== false)
                rows.push(legs[i])
        }
        return rows
    }

    function syncSelections() {
        var primaryIndex = Number(controls.backtestVm.selectedPrimaryLegIndex || 0)
        var visualIndex = primaryBox.indexOfValue(primaryIndex)
        primaryBox.currentIndex = visualIndex >= 0 ? visualIndex : (primaryBox.count > 0 ? 0 : -1)
        tradeModeBox.currentIndex = Math.max(0, tradeModeBox.indexOfValue(controls.backtestVm.selectedTradeMode))
    }

    RecorderComboBox {
        id: primaryBox
        Layout.preferredWidth: 280
        Layout.maximumWidth: 320
        caption: "Primary"
        textRole: "label"
        valueRole: "index"
        model: controls.enabledLegRows()
        popupWidth: 420
        enabled: count > 1
        opacity: enabled ? 1.0 : 0.58
        panelColor: controls.panelColor
        panelDeepColor: controls.panelDeepColor
        borderColor: controls.borderColor
        textColor: controls.textColor
        mutedTextColor: controls.mutedTextColor
        accentColor: controls.accentColor
        onActivated: {
            var value = Number(currentValue)
            if (isFinite(value))
                controls.backtestVm.setSelectedPrimaryLegIndex(value)
        }
    }

    RecorderComboBox {
        id: tradeModeBox
        Layout.preferredWidth: 128
        caption: "Trade"
        textRole: "label"
        valueRole: "id"
        model: controls.backtestVm.tradeModeChoices
        popupWidth: 140
        panelColor: controls.panelColor
        panelDeepColor: controls.panelDeepColor
        borderColor: controls.borderColor
        textColor: controls.textColor
        mutedTextColor: controls.mutedTextColor
        accentColor: controls.accentColor
        onActivated: controls.backtestVm.setSelectedTradeMode(currentValue)
    }

    Item { Layout.fillWidth: true }

    Component.onCompleted: syncSelections()

    Connections {
        target: controls.backtestVm
        function onMultiSessionChanged() { controls.syncSelections() }
        function onLegSelectionChanged() { controls.syncSelections() }
        function onPrimaryLegChanged() { controls.syncSelections() }
        function onTradeModeChanged() { controls.syncSelections() }
    }
}
