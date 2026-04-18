import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HftRecorder 1.0

ApplicationWindow {
    id: root
    width: 1600
    height: 980
    visible: true
    title: "hft-recorder"
    color: "#161616"

    property color panelColor: "#2c2c2f"
    property color panelAltColor: "#3c3c3c"
    property color borderColor: "#3c3d3f"
    property color textColor: "#f5f5f5"
    property color mutedTextColor: "#b6b6b6"
    property color accentBuyColor: "#24c2cb"

    component DarkTabButton: TabButton {
        id: control
        background: Rectangle {
            color: control.checked ? root.panelAltColor : root.panelColor
            border.color: control.checked ? root.accentBuyColor : root.borderColor
            border.width: 1
        }
        contentItem: Text {
            text: control.text
            color: control.checked ? root.textColor : root.mutedTextColor
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    header: ToolBar {
        contentHeight: 48
        background: Rectangle {
            color: root.panelColor
            border.color: root.borderColor
            border.width: 1
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 16

            Label {
                text: "hft-recorder"
                font.bold: true
                font.pixelSize: 20
                color: root.textColor
            }

            Item { Layout.fillWidth: true }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TabBar {
            id: tabBar
            Layout.fillWidth: true
            background: Rectangle {
                color: root.panelColor
                border.color: root.borderColor
                border.width: 1
            }

            DarkTabButton { text: "Capture" }
            DarkTabButton { text: "Viewer" }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabBar.currentIndex

            CaptureView { Layout.fillWidth: true; Layout.fillHeight: true }
            ViewerView { Layout.fillWidth: true; Layout.fillHeight: true }
        }
    }
}
