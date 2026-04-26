import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts

Pane {
    id: root
    required property var compressionVm
    required property bool tabActive

    property color windowColor: "#161616"
    property color panelColor: "#242427"
    property color panelAltColor: "#303034"
    property color borderColor: "#3c3d3f"
    property color textColor: "#f5f5f5"
    property color mutedTextColor: "#b6b6b6"
    property color accentColor: "#24c2cb"

    background: Rectangle { color: root.windowColor }

    FileDialog {
        id: inputDialog
        title: "Select corpus file"
        nameFilters: ["Canonical JSONL (*.jsonl)"]
        onAccepted: root.compressionVm.setInputFileUrl(selectedFile)
    }

    FolderDialog {
        id: outputDialog
        title: "Select output root"
        onAccepted: root.compressionVm.setOutputRootUrl(selectedFolder)
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            spacing: 12

            Label {
                text: "Compression"
                font.pixelSize: 24
                font.bold: true
                color: root.textColor
            }

            Rectangle {
                Layout.preferredWidth: 120
                Layout.preferredHeight: 28
                radius: 4
                color: root.panelAltColor
                border.color: root.compressionVm.canRun ? root.accentColor : root.borderColor
                border.width: 1

                Text {
                    anchors.centerIn: parent
                    text: root.compressionVm.inferredStream
                    color: root.compressionVm.canRun ? root.textColor : root.mutedTextColor
                    font.pixelSize: 12
                }
            }

            Item { Layout.fillWidth: true }

            Button {
                text: "Run pipeline"
                enabled: root.compressionVm.canRun
                onClicked: root.compressionVm.runCompression()
            }
        }

        GridLayout {
            Layout.fillWidth: true
            columns: 3
            columnSpacing: 10
            rowSpacing: 10

            Label { text: "Input"; color: root.mutedTextColor; Layout.alignment: Qt.AlignVCenter }
            TextField {
                Layout.fillWidth: true
                text: root.compressionVm.inputFile
                color: root.textColor
                placeholderText: "trades.jsonl, bookticker.jsonl, or depth.jsonl"
                onEditingFinished: root.compressionVm.setInputFile(text)
            }
            Button { text: "Browse"; onClicked: inputDialog.open() }

            Label { text: "Output"; color: root.mutedTextColor; Layout.alignment: Qt.AlignVCenter }
            TextField {
                Layout.fillWidth: true
                text: root.compressionVm.outputRoot
                color: root.textColor
                onEditingFinished: root.compressionVm.setOutputRoot(text)
            }
            Button { text: "Folder"; onClicked: outputDialog.open() }

            Label { text: "Pipeline"; color: root.mutedTextColor; Layout.alignment: Qt.AlignVCenter }
            ComboBox {
                id: pipelineBox
                Layout.fillWidth: true
                textRole: "label"
                valueRole: "id"
                model: root.compressionVm.pipelines
                Component.onCompleted: currentIndex = indexOfValue(root.compressionVm.selectedPipelineId)
                onActivated: root.compressionVm.setSelectedPipelineId(currentValue)

                delegate: ItemDelegate {
                    required property var modelData
                    width: pipelineBox.width
                    text: modelData.label + "  [" + modelData.availability + "]"
                    opacity: modelData.available ? 1.0 : 0.65
                }
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 30
                radius: 4
                color: root.panelAltColor
                border.color: root.compressionVm.selectedPipelineAvailable ? root.accentColor : root.borderColor
                border.width: 1

                Text {
                    anchors.fill: parent
                    anchors.leftMargin: 8
                    anchors.rightMargin: 8
                    verticalAlignment: Text.AlignVCenter
                    text: root.compressionVm.selectedPipelineAvailable ? "available" : "planned"
                    color: root.compressionVm.selectedPipelineAvailable ? root.textColor : root.mutedTextColor
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }
        }

        Label {
            Layout.fillWidth: true
            text: root.compressionVm.selectedPipelineSummary
            color: root.mutedTextColor
            font.pixelSize: 12
            elide: Text.ElideRight
        }

        Rectangle { Layout.fillWidth: true; Layout.preferredHeight: 1; color: root.borderColor }

        GridLayout {
            Layout.fillWidth: true
            columns: 4
            columnSpacing: 10
            rowSpacing: 10

            Repeater {
                model: [
                    { label: "Ratio", value: root.compressionVm.ratioText },
                    { label: "Encode", value: root.compressionVm.encodeSpeedText },
                    { label: "Decode", value: root.compressionVm.decodeSpeedText },
                    { label: "Size", value: root.compressionVm.sizeText }
                ]

                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.preferredHeight: 86
                    radius: 6
                    color: root.panelColor
                    border.color: root.borderColor
                    border.width: 1

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 12
                        spacing: 6

                        Label { text: modelData.label; color: root.mutedTextColor; font.pixelSize: 12 }
                        Label {
                            Layout.fillWidth: true
                            text: modelData.value
                            color: root.textColor
                            font.pixelSize: 18
                            font.bold: true
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 138
            radius: 6
            color: root.panelColor
            border.color: root.borderColor
            border.width: 1

            GridLayout {
                anchors.fill: parent
                anchors.margins: 12
                columns: 2
                columnSpacing: 12
                rowSpacing: 8

                Label { text: "Status"; color: root.mutedTextColor }
                Label { Layout.fillWidth: true; text: root.compressionVm.statusText; color: root.textColor; elide: Text.ElideRight }

                Label { text: "Pipeline"; color: root.mutedTextColor }
                Label { Layout.fillWidth: true; text: root.compressionVm.resultPipelineText; color: root.textColor; elide: Text.ElideRight }

                Label { text: "File"; color: root.mutedTextColor }
                Label { Layout.fillWidth: true; text: root.compressionVm.outputFile; color: root.textColor; elide: Text.ElideMiddle }

                Label { text: "Metrics"; color: root.mutedTextColor }
                Label { Layout.fillWidth: true; text: root.compressionVm.metricsFile; color: root.textColor; elide: Text.ElideMiddle }

                Label { text: "Timing"; color: root.mutedTextColor }
                Label { Layout.fillWidth: true; text: root.compressionVm.timingText; color: root.textColor; elide: Text.ElideRight }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
