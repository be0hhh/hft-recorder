import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ComboBox {
    id: combo

    property string caption: ""
    property color panelColor: "#1b1d23"
    property color panelAltColor: "#22252d"
    property color panelDeepColor: "#15171c"
    property color borderColor: "#343844"
    property color textColor: "#f1f4f8"
    property color mutedTextColor: "#a8afbd"
    property color accentColor: "#24c2cb"
    property color badColor: "#ef6f6c"
    property int popupWidth: width
    property int popupMaxWidth: 0
    property bool popupAlignRight: false
    property bool colorizeRightText: false
    property string searchText: ""
    property var filteredRows: []
    readonly property bool compactDisplay: caption.length === 0 || height < 36

    function rebuildFilter() {
        var needle = searchText.trim().toLowerCase()
        var rows = []
        for (var i = 0; i < combo.count; ++i) {
            var label = combo.textAt(i)
            var value = combo.valueAt(i)
            var rightText = ""
            if (combo.model && combo.model[i] && combo.model[i].rightText)
                rightText = combo.model[i].rightText
            var haystack = (label + " " + value).toLowerCase()
            if (needle.length === 0 || haystack.indexOf(needle) !== -1)
                rows.push({ "index": i, "label": label, "value": value, "rightText": rightText })
        }
        filteredRows = rows
    }

    function selectFilteredRow(row) {
        if (!row || row.index < 0)
            return
        combo.currentIndex = row.index
        combo.popup.close()
        combo.activated(row.index)
    }

    function currentRightText() {
        if (combo.currentIndex < 0 || !combo.model || !combo.model[combo.currentIndex])
            return ""
        return combo.model[combo.currentIndex].rightText || ""
    }

    Layout.fillWidth: true
    Layout.preferredHeight: 42
    font.pixelSize: 12

    onSearchTextChanged: rebuildFilter()
    onCountChanged: rebuildFilter()
    onModelChanged: rebuildFilter()

    contentItem: Item {
        Text {
            visible: !combo.compactDisplay
            x: 10
            y: 5
            width: combo.width - 40
            text: combo.caption
            color: combo.mutedTextColor
            font.pixelSize: 10
            elide: Text.ElideRight
        }
        Text {
            x: 10
            y: combo.compactDisplay ? Math.round((combo.height - implicitHeight) / 2) : 21
            width: combo.width - 40 - (selectedRightText.visible ? selectedRightText.width : 0)
            text: combo.displayText.length > 0 ? combo.displayText : "not selected"
            color: combo.textColor
            font.pixelSize: 12
            font.bold: true
            elide: Text.ElideRight
        }
        Text {
            id: selectedRightText
            x: combo.width - width - 30
            y: combo.compactDisplay ? Math.round((combo.height - implicitHeight) / 2) : 21
            width: visible ? Math.min(170, implicitWidth + 8) : 0
            text: combo.currentRightText()
            visible: text.length > 0
            color: combo.mutedTextColor
            font.pixelSize: 12
            font.bold: true
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideRight
        }
    }

    indicator: Text { x: combo.width - 22; y: Math.round((combo.height - implicitHeight) / 2); text: "v"; color: combo.mutedTextColor; font.pixelSize: 12 }
    background: Rectangle { radius: 7; color: combo.panelDeepColor; border.color: combo.hovered ? combo.accentColor : combo.borderColor; border.width: 1 }

    popup: Popup {
        x: combo.popupAlignRight ? Math.min(0, combo.width - width) : 0
        y: combo.height + 4
        width: combo.popupMaxWidth > 0 ? Math.min(combo.popupMaxWidth, Math.max(combo.width, combo.popupWidth)) : Math.max(combo.width, combo.popupWidth)
        implicitHeight: Math.min(contentItem.implicitHeight, 400)
        padding: 1
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside | Popup.CloseOnReleaseOutside
        onOpened: {
            combo.searchText = ""
            combo.rebuildFilter()
            searchField.forceActiveFocus()
        }
        background: Rectangle { color: combo.panelColor; border.color: combo.borderColor; radius: 7 }
        contentItem: Column {
            width: combo.popup.width
            spacing: 4

            Rectangle {
                width: parent.width - 8
                x: 4
                height: 30
                radius: 5
                color: combo.panelDeepColor
                border.color: searchField.activeFocus ? combo.accentColor : combo.borderColor
                border.width: 1

                Text { anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; text: "Search"; visible: searchField.text.length === 0; color: combo.mutedTextColor; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                TextInput {
                    id: searchField
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    text: combo.searchText
                    color: combo.textColor
                    selectionColor: combo.accentColor
                    selectedTextColor: combo.panelDeepColor
                    font.pixelSize: 12
                    selectByMouse: true
                    clip: true
                    verticalAlignment: TextInput.AlignVCenter
                    onTextChanged: combo.searchText = text
                    Keys.onPressed: function(event) {
                        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && combo.filteredRows.length > 0) {
                            combo.selectFilteredRow(combo.filteredRows[0])
                            event.accepted = true
                        } else if (event.key === Qt.Key_Escape) {
                            if (combo.searchText.length > 0) {
                                combo.searchText = ""
                                searchField.text = ""
                            } else {
                                combo.popup.close()
                            }
                            event.accepted = true
                        }
                    }
                }
            }

            ListView {
                id: resultList
                width: parent.width
                height: Math.min(contentHeight, 330)
                clip: true
                model: combo.popup.visible ? combo.filteredRows : []
                currentIndex: 0
                delegate: combo.delegate
            }

            Text {
                id: emptyText
                width: parent.width
                height: 30
                visible: combo.filteredRows.length === 0
                text: "No matches"
                color: combo.mutedTextColor
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    delegate: ItemDelegate {
        required property var modelData
        width: combo.popup.width
        height: 32
        contentItem: RowLayout {
            spacing: 8
            Text {
                Layout.fillWidth: true
                leftPadding: 10
                text: modelData.label || modelData.value || modelData.id || ""
                color: combo.textColor
                font.pixelSize: 12
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
            Text {
                Layout.preferredWidth: visible ? Math.max(64, implicitWidth + 10) : 0
                rightPadding: 10
                text: modelData.rightText || ""
                visible: text.length > 0
                color: combo.colorizeRightText ? (text.charAt(0) === "-" ? combo.badColor : combo.accentColor) : combo.mutedTextColor
                font.pixelSize: 12
                font.bold: true
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
            }
        }
        background: Rectangle { color: highlighted ? Qt.rgba(combo.accentColor.r, combo.accentColor.g, combo.accentColor.b, 0.16) : combo.panelColor }
        onClicked: combo.selectFilteredRow(modelData)
    }
}
