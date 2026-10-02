import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ComboBox {
    id: backtestCombo
    required property var rows
    required property Item layoutRoot
    required property color textColor
    required property color mutedTextColor
    required property color panelColor
    required property color panelAltColor
    required property color panelDeepColor
    required property color borderColor
    required property color accentBuyColor
    signal rowChosen(int index)
    signal refreshRequested()
    model: rows
    textRole: "label"
    valueRole: "path"
    function backtestPopupPreferredWidth() {
        var widest = 420
        for (var i = 0; i < backtestCombo.rows.length; ++i) {
            var row = backtestCombo.rows[i] || {}
            var label = String(row.label || "")
            var right = String(row.rightText || row.pnlText || (row.selectable === false ? "sweep" : ""))
            var estimate = 48 + Math.ceil(label.length * 7.4) + (right.length > 0 ? Math.ceil(right.length * 8.2) + 18 : 0)
            widest = Math.max(widest, estimate)
        }
        var controlWidth = backtestCombo && backtestCombo.width > 0 ? backtestCombo.width : 220
        return Math.min(Math.max(controlWidth, widest), Math.max(controlWidth, backtestCombo.layoutRoot.width - 32))
    }

    function backtestPopupX(popupWidth) {
        if (!backtestCombo)
            return 0
        var comboRootX = backtestCombo.mapToItem(backtestCombo.layoutRoot, 0, 0).x
        var alignRightX = Math.min(0, backtestCombo.width - popupWidth)
        return Math.max(16 - comboRootX, alignRightX)
    }

    property string searchText: ""
    property var filteredRows: []
    function rebuildFilter() {
        var needle = backtestCombo.searchText.trim().toLowerCase()
        var rows = []
        for (var i = 0; i < backtestCombo.rows.length; ++i) {
            var row = backtestCombo.rows[i]
            var haystack = (row.label + " " + row.path).toLowerCase()
            if (needle.length === 0 || haystack.indexOf(needle) !== -1)
                rows.push({ "index": i, "label": row.label, "path": row.path, "pnlText": row.rightText || row.pnlText || (row.selectable === false ? "sweep" : ""), "selectable": row.selectable !== false })
        }
        backtestCombo.filteredRows = rows
    }
    function selectFilteredRow(row) {
        if (!row || row.index < 0)
            return
        backtestCombo.currentIndex = row.index
        backtestCombo.popup.close()
        backtestCombo.rowChosen(row.index)
    }
    onSearchTextChanged: rebuildFilter()
    onModelChanged: rebuildFilter()
    onActivated: function(index) { backtestCombo.rowChosen(index) }
    contentItem: Text {
        text: backtestCombo.displayText === "" ? "Backtest" : backtestCombo.displayText
        color: backtestCombo.enabled ? backtestCombo.textColor : backtestCombo.mutedTextColor
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        leftPadding: 10
        rightPadding: 28
    }
    background: Rectangle {
        radius: 7
        color: backtestCombo.down ? backtestCombo.panelAltColor : backtestCombo.panelColor
        border.color: backtestCombo.activeFocus ? backtestCombo.accentBuyColor : backtestCombo.borderColor
        border.width: 1
    }
    delegate: Component {
        ItemDelegate {
            width: backtestCombo.popup.width
            text: modelData.label
            highlighted: backtestCombo.highlightedIndex === index
            contentItem: RowLayout {
                spacing: 8
                Text {
                    Layout.fillWidth: true
                    text: modelData.label
                    color: modelData.index === 0 || modelData.selectable === false ? backtestCombo.mutedTextColor : backtestCombo.textColor
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }
                Text {
                    Layout.preferredWidth: visible ? Math.min(190, Math.max(96, implicitWidth + 14)) : 0
                    text: modelData.pnlText || ""
                    visible: text.length > 0
                    color: text.charAt(0) === "-" ? "#ef6f6c" : backtestCombo.accentBuyColor
                    font.pixelSize: 12
                    font.bold: true
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignVCenter
                }
            }
            background: Rectangle { color: highlighted ? backtestCombo.panelAltColor : backtestCombo.panelColor }
            onClicked: backtestCombo.selectFilteredRow(modelData)
        }
    }
    popup: Popup {
        y: backtestCombo.height + 2
        x: backtestCombo.backtestPopupX(width)
        width: backtestCombo.backtestPopupPreferredWidth()
        implicitHeight: Math.min(contentItem.implicitHeight, 360)
        padding: 1
        onOpened: {
            backtestCombo.refreshRequested()
            backtestCombo.searchText = ""
            backtestCombo.rebuildFilter()
            backtestSearchField.forceActiveFocus()
        }
        contentItem: Column {
            width: backtestCombo.popup.width
            spacing: 4

            Rectangle {
                width: parent.width - 8
                x: 4
                height: 30
                radius: 5
                color: backtestCombo.panelDeepColor
                border.color: backtestSearchField.activeFocus ? backtestCombo.accentBuyColor : backtestCombo.borderColor
                border.width: 1

                Text { anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; text: "Search"; visible: backtestSearchField.text.length === 0; color: backtestCombo.mutedTextColor; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                TextInput {
                    id: backtestSearchField
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    text: backtestCombo.searchText
                    color: backtestCombo.textColor
                    selectionColor: backtestCombo.accentBuyColor
                    selectedTextColor: backtestCombo.panelDeepColor
                    font.pixelSize: 12
                    selectByMouse: true
                    clip: true
                    verticalAlignment: TextInput.AlignVCenter
                    onTextChanged: backtestCombo.searchText = text
                    Keys.onPressed: function(event) {
                        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && backtestCombo.filteredRows.length > 0) {
                            backtestCombo.selectFilteredRow(backtestCombo.filteredRows[0])
                            event.accepted = true
                        } else if (event.key === Qt.Key_Escape) {
                            if (backtestCombo.searchText.length > 0) {
                                backtestCombo.searchText = ""
                                backtestSearchField.text = ""
                            } else {
                                backtestCombo.popup.close()
                            }
                            event.accepted = true
                        }
                    }
                }
            }

            ListView {
                id: backtestResultList
                width: parent.width
                height: Math.min(contentHeight, 290)
                clip: true
                model: backtestCombo.popup.visible ? backtestCombo.filteredRows : []
                currentIndex: 0
                delegate: backtestCombo.delegate
            }

            Text {
                id: backtestEmptyText
                width: parent.width
                height: 30
                visible: backtestCombo.filteredRows.length === 0
                text: "No matches"
                color: backtestCombo.mutedTextColor
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
        background: Rectangle {
            color: backtestCombo.panelColor
            border.color: backtestCombo.borderColor
            radius: 7
        }
    }
}
