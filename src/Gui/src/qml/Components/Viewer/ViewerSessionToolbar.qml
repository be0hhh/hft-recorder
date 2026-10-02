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

    property var sourceRows: []

    signal sourceActivated(string sourceId)
    signal reloadRequested()
    signal sourceCountChanged()

    Layout.fillWidth: true
    Layout.preferredHeight: 50
    color: bar.chromeColor
    border.color: bar.borderColor
    border.width: 1

    function reloadSourceRows() {
        sourceRows = bar.sourcesModel ? bar.sourcesModel.sourceRows() : []
        Qt.callLater(function() { sourcePicker.setCurrentId(bar.selectedSourceId) })
        sourceCountChanged()
    }

    function setCurrentIndex(index) {
        if (!bar.sourcesModel || index < 0) {
            sourcePicker.currentIndex = -1
            return
        }
        sourcePicker.setCurrentId(bar.sourcesModel.sourceIdAt(index))
    }
    function currentIndex() { return sourcePicker.currentIndex }
    function currentSourceId() { return sourcePicker.currentValue }
    function count() { return bar.sourcesModel ? bar.sourcesModel.rowCount() : 0 }

    Component.onCompleted: reloadSourceRows()
    onSourcesModelChanged: reloadSourceRows()
    onSelectedSourceIdChanged: sourcePicker.setCurrentId(bar.selectedSourceId)

    Connections {
        target: bar.sourcesModel ? bar.sourcesModel : null
        function onModelReset() { bar.reloadSourceRows() }
        function onRowsInserted() { bar.reloadSourceRows() }
        function onRowsRemoved() { bar.reloadSourceRows() }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        spacing: 10

        Label { text: "Source:"; color: bar.textColor; font.pixelSize: 14 }

        SessionPickerCombo {
            id: sourcePicker
            Layout.fillWidth: true
            caption: "Source"
            emptyLabel: "Select session"
            rows: bar.sourceRows
            panelColor: bar.panelColor
            panelAltColor: bar.panelAltColor
            panelDeepColor: bar.panelColor
            borderColor: bar.borderColor
            textColor: bar.textColor
            mutedTextColor: bar.mutedTextColor
            accentColor: "#24c2cb"
            popupWidth: 760
            onPicked: function(id) { bar.sourceActivated(id) }
            Component.onCompleted: setCurrentId(bar.selectedSourceId)
        }

        Button {
            text: "Reload"
            onClicked: {
                bar.reloadRequested()
                Qt.callLater(bar.reloadSourceRows)
            }
        }
    }
}
