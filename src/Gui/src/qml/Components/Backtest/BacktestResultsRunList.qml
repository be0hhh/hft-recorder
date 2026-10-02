import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HftRecorder 1.0

Rectangle {
    id: runList
    required property var viewRoot

    SplitView.preferredWidth: 360
    SplitView.minimumWidth: 260
    color: viewRoot.panelColor
    border.color: viewRoot.borderColor
    radius: 6

    ListView {
        anchors.fill: parent
        anchors.margins: 8
        clip: true
        model: viewRoot.backtestVm.runs
        delegate: Rectangle {
            required property var modelData
            width: ListView.view.width
            height: 64
            radius: 5
            color: modelData.runId === viewRoot.backtestVm.selectedRunId ? Qt.rgba(viewRoot.accentColor.r, viewRoot.accentColor.g, viewRoot.accentColor.b, 0.16) : viewRoot.panelDeepColor
            border.color: modelData.runId === viewRoot.backtestVm.selectedRunId ? viewRoot.accentColor : viewRoot.borderColor
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 8
                anchors.rightMargin: 8
                spacing: 8
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Label { text: modelData.label; color: viewRoot.textColor; font.pixelSize: 12; font.bold: true; elide: Text.ElideRight; Layout.fillWidth: true }
                    Label { text: modelData.status + " / " + modelData.modifiedText; color: viewRoot.mutedTextColor; font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true }
                    Label { text: modelData.configText || modelData.runId; color: viewRoot.mutedTextColor; font.pixelSize: 10; elide: Text.ElideRight; Layout.fillWidth: true }
                }
                Label {
                    Layout.preferredWidth: 58
                    text: modelData.pnlText || ""
                    visible: text.length > 0
                    color: modelData.pnlNegative ? viewRoot.badColor : viewRoot.accentColor
                    font.pixelSize: 12
                    font.bold: true
                    horizontalAlignment: Text.AlignRight
                    verticalAlignment: Text.AlignVCenter
                }
            }
            MouseArea { anchors.fill: parent; onClicked: viewRoot.backtestVm.selectRun(modelData.runId) }
        }
    }
}
