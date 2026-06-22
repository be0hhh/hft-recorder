import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ComboBox {
    id: picker

    property string caption: ""
    property string emptyLabel: "Select session"
    property var rows: []
    property color panelColor: "#1b1d23"
    property color panelAltColor: "#22252d"
    property color panelDeepColor: "#15171c"
    property color borderColor: "#343844"
    property color textColor: "#f1f4f8"
    property color mutedTextColor: "#a8afbd"
    property color accentColor: "#24c2cb"
    property int popupWidth: width
    property int popupMaxWidth: 0
    property bool popupAlignRight: false
    property string searchText: ""
    property var filteredRows: []
    property var expandedGroups: ({})
    readonly property bool compactDisplay: caption.length === 0 || height < 36

    signal picked(string id)

    model: rows
    textRole: "label"
    valueRole: "id"
    Layout.fillWidth: true
    Layout.preferredHeight: 42
    font.pixelSize: 12

    function setCurrentId(id) {
        currentIndex = indexOfValue(id)
    }

    function sourceRowAt(index) {
        if (!picker.rows || index < 0 || index >= picker.rows.length)
            return ({})
        var row = picker.rows[index]
        return row ? row : ({})
    }

    function rowRightText(row) {
        return row && row.rightText ? row.rightText : ""
    }

    function rowIsGroup(row) {
        return row && row.isGroup === true
    }

    function rowSelectable(row) {
        return !(row && row.selectable === false)
    }

    function rowGroupId(row) {
        if (!row)
            return ""
        if (row.groupId)
            return String(row.groupId)
        if (row.parentGroupId)
            return String(row.parentGroupId)
        return ""
    }

    function rowMatches(row, label, value, needle) {
        if (needle.length === 0)
            return true
        var haystack = label + " " + value + " " + picker.rowRightText(row)
        if (row && row.searchText)
            haystack += " " + row.searchText
        if (row && row.groupId)
            haystack += " " + row.groupId
        if (row && row.parentGroupId)
            haystack += " " + row.parentGroupId
        return haystack.toLowerCase().indexOf(needle) !== -1
    }

    function makeFilteredRow(index) {
        var row = picker.sourceRowAt(index)
        return {
            "index": index,
            "label": row.label || "",
            "value": row.id || "",
            "rightText": picker.rowRightText(row),
            "isGroup": picker.rowIsGroup(row),
            "selectable": picker.rowSelectable(row),
            "groupId": picker.rowGroupId(row),
            "depth": row && row.parentGroupId ? 1 : 0
        }
    }

    function rebuildFilter() {
        var needle = searchText.trim().toLowerCase()
        var out = []
        for (var groupIndex = 0; groupIndex < picker.count; ++groupIndex) {
            var groupSource = picker.sourceRowAt(groupIndex)
            var groupRow = picker.makeFilteredRow(groupIndex)
            if (groupRow.isGroup) {
                var groupId = groupRow.groupId
                var groupMatches = picker.rowMatches(groupSource, groupRow.label, groupRow.value, needle)
                var childRows = []
                var childMatches = false
                for (var childIndex = 0; childIndex < picker.count; ++childIndex) {
                    var childSource = picker.sourceRowAt(childIndex)
                    if (picker.rowIsGroup(childSource) || picker.rowGroupId(childSource) !== groupId)
                        continue
                    var childRow = picker.makeFilteredRow(childIndex)
                    if (picker.rowMatches(childSource, childRow.label, childRow.value, needle))
                        childMatches = true
                    childRows.push(childRow)
                }
                if (needle.length === 0 || groupMatches || childMatches) {
                    out.push(groupRow)
                    if (picker.expandedGroups[groupId] === true) {
                        for (var c = 0; c < childRows.length; ++c)
                            out.push(childRows[c])
                    }
                }
            } else if (picker.rowGroupId(groupSource) === "" && picker.rowMatches(groupSource, groupRow.label, groupRow.value, needle)) {
                out.push(groupRow)
            }
        }
        filteredRows = out
    }

    function selectFilteredRow(row) {
        if (!row || row.index < 0)
            return
        if (row.isGroup) {
            var nextExpanded = ({})
            for (var key in picker.expandedGroups)
                nextExpanded[key] = picker.expandedGroups[key]
            nextExpanded[row.groupId] = nextExpanded[row.groupId] !== true
            picker.expandedGroups = nextExpanded
            picker.rebuildFilter()
            return
        }
        if (row.selectable === false)
            return
        picker.currentIndex = row.index
        picker.popup.close()
        picker.picked(row.value)
    }

    function currentRightText() {
        var row = picker.sourceRowAt(picker.currentIndex)
        return picker.rowRightText(row)
    }

    onSearchTextChanged: rebuildFilter()
    onCountChanged: rebuildFilter()
    onRowsChanged: {
        expandedGroups = ({})
        rebuildFilter()
    }

    contentItem: Item {
        Text {
            visible: !picker.compactDisplay
            x: 10
            y: 5
            width: picker.width - 40
            text: picker.caption
            color: picker.mutedTextColor
            font.pixelSize: 10
            elide: Text.ElideRight
        }
        Text {
            x: 10
            y: picker.compactDisplay ? Math.round((picker.height - implicitHeight) / 2) : 21
            width: picker.width - 40 - (selectedRightText.visible ? selectedRightText.width : 0)
            text: picker.currentIndex >= 0 && picker.displayText.length > 0 ? picker.displayText : picker.emptyLabel
            color: picker.currentIndex >= 0 ? picker.textColor : picker.mutedTextColor
            font.pixelSize: 12
            font.bold: true
            elide: Text.ElideRight
        }
        Text {
            id: selectedRightText
            x: picker.width - width - 30
            y: picker.compactDisplay ? Math.round((picker.height - implicitHeight) / 2) : 21
            width: visible ? Math.min(190, implicitWidth + 8) : 0
            text: picker.currentRightText()
            visible: text.length > 0
            color: picker.mutedTextColor
            font.pixelSize: 12
            font.bold: true
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideRight
        }
    }

    indicator: Text {
        x: picker.width - 22
        y: Math.round((picker.height - implicitHeight) / 2)
        text: "v"
        color: picker.mutedTextColor
        font.pixelSize: 12
    }

    background: Rectangle {
        radius: 7
        color: picker.panelDeepColor
        border.color: picker.hovered ? picker.accentColor : picker.borderColor
        border.width: 1
    }

    popup: Popup {
        x: picker.popupAlignRight ? Math.min(0, picker.width - width) : 0
        y: picker.height + 4
        width: picker.popupMaxWidth > 0 ? Math.min(picker.popupMaxWidth, Math.max(picker.width, picker.popupWidth)) : Math.max(picker.width, picker.popupWidth)
        implicitHeight: Math.min(contentItem.implicitHeight, 400)
        padding: 1
        closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside | Popup.CloseOnReleaseOutside
        onOpened: {
            picker.searchText = ""
            picker.expandedGroups = ({})
            picker.rebuildFilter()
            searchField.forceActiveFocus()
        }
        background: Rectangle { color: picker.panelColor; border.color: picker.borderColor; radius: 7 }
        contentItem: Column {
            width: picker.popup.width
            spacing: 4

            Rectangle {
                width: parent.width - 8
                x: 4
                height: 30
                radius: 5
                color: picker.panelDeepColor
                border.color: searchField.activeFocus ? picker.accentColor : picker.borderColor
                border.width: 1

                Text {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    text: "Search"
                    visible: searchField.text.length === 0
                    color: picker.mutedTextColor
                    font.pixelSize: 12
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                }
                TextInput {
                    id: searchField
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    text: picker.searchText
                    color: picker.textColor
                    selectionColor: picker.accentColor
                    selectedTextColor: picker.panelDeepColor
                    font.pixelSize: 12
                    selectByMouse: true
                    clip: true
                    verticalAlignment: TextInput.AlignVCenter
                    onTextChanged: picker.searchText = text
                    Keys.onPressed: function(event) {
                        if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && picker.filteredRows.length > 0) {
                            picker.selectFilteredRow(picker.filteredRows[0])
                            event.accepted = true
                        } else if (event.key === Qt.Key_Escape) {
                            if (picker.searchText.length > 0) {
                                picker.searchText = ""
                                searchField.text = ""
                            } else {
                                picker.popup.close()
                            }
                            event.accepted = true
                        }
                    }
                }
            }

            ListView {
                width: parent.width
                height: Math.min(contentHeight, 330)
                clip: true
                model: picker.popup.visible ? picker.filteredRows : []
                delegate: picker.delegate
            }

            Text {
                width: parent.width
                height: 30
                visible: picker.filteredRows.length === 0
                text: "No matches"
                color: picker.mutedTextColor
                font.pixelSize: 12
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    delegate: ItemDelegate {
        required property var modelData
        width: picker.popup.width
        height: modelData.isGroup ? 34 : 32
        contentItem: RowLayout {
            spacing: 8
            Text {
                visible: modelData.isGroup
                Layout.preferredWidth: 16
                leftPadding: 8
                text: picker.expandedGroups[modelData.groupId] === true ? "v" : ">"
                color: picker.mutedTextColor
                font.pixelSize: 11
                font.bold: true
                verticalAlignment: Text.AlignVCenter
            }
            Text {
                Layout.fillWidth: true
                leftPadding: modelData.isGroup ? 0 : 26
                text: modelData.label || modelData.value || ""
                color: picker.textColor
                font.pixelSize: modelData.isGroup ? 12 : 11
                font.bold: modelData.isGroup
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
            Text {
                Layout.preferredWidth: visible ? Math.max(64, implicitWidth + 10) : 0
                rightPadding: 10
                text: modelData.rightText || ""
                visible: text.length > 0
                color: picker.mutedTextColor
                font.pixelSize: modelData.isGroup ? 12 : 11
                font.bold: true
                horizontalAlignment: Text.AlignRight
                verticalAlignment: Text.AlignVCenter
            }
        }
        background: Rectangle {
            color: highlighted ? Qt.rgba(picker.accentColor.r, picker.accentColor.g, picker.accentColor.b, 0.16)
                               : (modelData.isGroup ? picker.panelAltColor : picker.panelColor)
        }
        onClicked: picker.selectFilteredRow(modelData)
    }
}
