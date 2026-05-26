import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: bar

    required property var sourcesModel
    required property string selectedSourceId
    required property color chromeColor
    required property color panelColor
    required property color panelAltColor
    required property color borderColor
    required property color textColor
    required property color mutedTextColor

    signal sourceActivated(string sourceId)
    signal reloadRequested()
    signal sourceCountChanged()

    Layout.fillWidth: true
    Layout.preferredHeight: 50
    color: bar.chromeColor
    border.color: bar.borderColor
    border.width: 1

    function findSourceIndex(sourceId) { return bar.sourcesModel.indexOfSource(sourceId) }
    function setCurrentIndex(index) { sourcePicker.currentIndex = index }
    function currentIndex() { return sourcePicker.currentIndex }
    function currentSourceId() { return sourcePicker.currentValue }
    function count() { return sourcePicker.count }
    function selectedSourceSummary() { return bar.sourcesModel.sourceSummary(sourcePicker.currentValue) }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        spacing: 10

        Label { text: "Source:"; color: bar.textColor; font.pixelSize: 14 }

        ComboBox {
            id: sourcePicker
            Layout.fillWidth: true
            model: bar.sourcesModel
            textRole: "label"
            valueRole: "id"
            property string searchText: ""
            property var filteredRows: []

            function rebuildFilter() {
                var needle = sourcePicker.searchText.trim().toLowerCase()
                var rows = []
                for (var i = 0; i < sourcePicker.count; ++i) {
                    var label = bar.sourcesModel.labelAt(i)
                    var id = bar.sourcesModel.sourceIdAt(i)
                    var group = bar.sourcesModel.groupAt(i)
                    var summary = bar.sourcesModel.sourceSummary(id)
                    var haystack = (label + " " + id + " " + group + " " + summary).toLowerCase()
                    if (needle.length === 0 || haystack.indexOf(needle) !== -1)
                        rows.push({ "index": i, "label": label, "id": id, "group": group, "groupTitle": group, "rightText": summary })
                }
                sourcePicker.filteredRows = rows
            }

            function selectFilteredRow(row) {
                if (!row || row.index < 0)
                    return
                sourcePicker.currentIndex = row.index
                sourcePicker.popup.close()
                bar.sourceActivated(row.id)
            }

            onSearchTextChanged: rebuildFilter()
            onCountChanged: {
                rebuildFilter()
                bar.sourceCountChanged()
            }

            background: Rectangle { radius: 7; color: bar.panelColor; border.color: bar.borderColor; border.width: 1 }

            contentItem: RowLayout {
                spacing: 8
                Text {
                    Layout.fillWidth: true
                    leftPadding: 12
                    text: sourcePicker.displayText
                    color: bar.textColor
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    font.pixelSize: 13
                }
                Text {
                    Layout.preferredWidth: visible ? Math.max(210, implicitWidth + 8) : 0
                    rightPadding: 28
                    text: bar.selectedSourceSummary()
                    visible: text.length > 0
                    color: bar.mutedTextColor
                    font.pixelSize: 12
                    font.bold: true
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignVCenter
                }
            }

            indicator: Canvas {
                x: sourcePicker.width - width - 10
                y: (sourcePicker.height - height) / 2
                width: 12
                height: 8
                contextType: "2d"
                onPaint: {
                    context.reset()
                    context.fillStyle = bar.mutedTextColor
                    context.beginPath()
                    context.moveTo(0, 0)
                    context.lineTo(width, 0)
                    context.lineTo(width / 2, height)
                    context.closePath()
                    context.fill()
                }
            }

            delegate: ItemDelegate {
                id: delegateControl
                required property int index
                required property string label
                required property string group
                required property string groupTitle
                width: sourcePicker.width
                highlighted: sourcePicker.highlightedIndex === index
                padding: 0
                background: Rectangle { color: delegateControl.highlighted ? bar.panelAltColor : bar.panelColor }

                contentItem: Column {
                    width: delegateControl.width
                    spacing: 0

                    Rectangle {
                        width: parent.width
                        height: groupLabel.visible ? 22 : 0
                        color: bar.chromeColor
                        visible: delegateControl.index === 0
                                 || delegateControl.group !== bar.sourcesModel.groupAt(delegateControl.index - 1)

                        Label {
                            id: groupLabel
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.left: parent.left
                            anchors.leftMargin: 12
                            text: delegateControl.groupTitle
                            color: bar.mutedTextColor
                            font.pixelSize: 11
                            font.bold: true
                            visible: parent.visible
                        }
                    }

                    RowLayout {
                        width: parent.width
                        spacing: 8
                        Text {
                            Layout.fillWidth: true
                            leftPadding: 12
                            topPadding: 8
                            bottomPadding: 8
                            text: delegateControl.label
                            color: bar.textColor
                            verticalAlignment: Text.AlignVCenter
                            elide: Text.ElideRight
                            font.pixelSize: 13
                        }
                        Text {
                            Layout.preferredWidth: visible ? Math.max(210, implicitWidth + 8) : 0
                            rightPadding: 12
                            text: bar.sourcesModel.sourceSummary(bar.sourcesModel.sourceIdAt(delegateControl.index))
                            visible: text.length > 0
                            color: bar.mutedTextColor
                            font.pixelSize: 12
                            font.bold: true
                            horizontalAlignment: Text.AlignRight
                            verticalAlignment: Text.AlignVCenter
                        }
                    }
                }
            }

            popup: Popup {
                y: sourcePicker.height + 4
                width: Math.max(sourcePicker.width, 760)
                padding: 4
                onOpened: {
                    sourcePicker.searchText = ""
                    sourcePicker.rebuildFilter()
                    sourceSearchField.forceActiveFocus()
                }
                background: Rectangle { radius: 7; color: bar.panelColor; border.color: bar.borderColor; border.width: 1 }
                contentItem: Column {
                    width: sourcePicker.popup.width
                    spacing: 4

                    Component {
                        id: filteredSourceDelegate
                        ItemDelegate {
                            required property var modelData
                            width: sourcePicker.popup.width - 8
                            highlighted: sourcePicker.highlightedIndex === modelData.index
                            padding: 0
                            background: Rectangle { color: highlighted ? bar.panelAltColor : bar.panelColor }
                            onClicked: sourcePicker.selectFilteredRow(modelData)

                            contentItem: Column {
                                width: parent.width
                                spacing: 0

                                Rectangle {
                                    width: parent.width
                                    height: groupLabel.visible ? 22 : 0
                                    color: bar.chromeColor
                                    visible: modelData.index === 0 || modelData.group !== bar.sourcesModel.groupAt(modelData.index - 1)

                                    Label {
                                        id: groupLabel
                                        anchors.verticalCenter: parent.verticalCenter
                                        anchors.left: parent.left
                                        anchors.leftMargin: 12
                                        text: modelData.groupTitle
                                        color: bar.mutedTextColor
                                        font.pixelSize: 11
                                        font.bold: true
                                        visible: parent.visible
                                    }
                                }

                                RowLayout {
                                    width: parent.width
                                    spacing: 8
                                    Text {
                                        Layout.fillWidth: true
                                        leftPadding: 12
                                        topPadding: 8
                                        bottomPadding: 8
                                        text: modelData.label
                                        color: bar.textColor
                                        verticalAlignment: Text.AlignVCenter
                                        elide: Text.ElideRight
                                        font.pixelSize: 13
                                    }
                                    Text {
                                        Layout.preferredWidth: visible ? Math.max(210, implicitWidth + 8) : 0
                                        rightPadding: 12
                                        text: modelData.rightText || ""
                                        visible: text.length > 0
                                        color: bar.mutedTextColor
                                        font.pixelSize: 13
                                        font.bold: true
                                        horizontalAlignment: Text.AlignRight
                                        verticalAlignment: Text.AlignVCenter
                                    }
                                }
                            }
                        }
                    }

                    Rectangle {
                        width: parent.width
                        height: 30
                        radius: 5
                        color: bar.panelAltColor
                        border.color: sourceSearchField.activeFocus ? bar.borderColor : bar.panelColor
                        border.width: 1

                        Text { anchors.fill: parent; anchors.leftMargin: 8; anchors.rightMargin: 8; text: "Search"; visible: sourceSearchField.text.length === 0; color: bar.mutedTextColor; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                        TextInput {
                            id: sourceSearchField
                            anchors.fill: parent
                            anchors.leftMargin: 8
                            anchors.rightMargin: 8
                            text: sourcePicker.searchText
                            color: bar.textColor
                            selectionColor: bar.borderColor
                            selectedTextColor: bar.textColor
                            font.pixelSize: 12
                            selectByMouse: true
                            clip: true
                            verticalAlignment: TextInput.AlignVCenter
                            onTextChanged: sourcePicker.searchText = text
                            Keys.onPressed: function(event) {
                                if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter) && sourcePicker.filteredRows.length > 0) {
                                    sourcePicker.selectFilteredRow(sourcePicker.filteredRows[0])
                                    event.accepted = true
                                } else if (event.key === Qt.Key_Escape) {
                                    if (sourcePicker.searchText.length > 0) {
                                        sourcePicker.searchText = ""
                                        sourceSearchField.text = ""
                                    } else {
                                        sourcePicker.popup.close()
                                    }
                                    event.accepted = true
                                }
                            }
                        }
                    }

                    ListView {
                        id: sourceResultList
                        width: parent.width
                        height: Math.min(contentHeight, 360)
                        clip: true
                        model: sourcePicker.popup.visible ? sourcePicker.filteredRows : []
                        currentIndex: 0
                        delegate: filteredSourceDelegate
                    }

                    Text {
                        id: sourceEmptyText
                        width: parent.width
                        height: 30
                        visible: sourcePicker.filteredRows.length === 0
                        text: "No matches"
                        color: bar.mutedTextColor
                        font.pixelSize: 12
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                }
            }

            onActivated: {
                if (currentIndex < 0) return
                bar.sourceActivated(currentValue)
            }

        }

        ViewerChromeButton {
            text: "Reload"
            panelColor: bar.panelColor
            panelAltColor: bar.panelAltColor
            borderColor: bar.borderColor
            textColor: bar.textColor
            onClicked: bar.reloadRequested()
        }
    }
}
