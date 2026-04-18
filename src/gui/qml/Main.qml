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

    AppViewModel {
        id: appVm
    }

    header: ToolBar {
        contentHeight: 48

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 16
            anchors.rightMargin: 16
            spacing: 16

            Label {
                text: "hft-recorder"
                font.bold: true
                font.pixelSize: 20
            }

            Label {
                Layout.fillWidth: true
                text: appVm.statusText
                color: "#666666"
            }
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        TabBar {
            id: tabBar
            Layout.fillWidth: true

            TabButton { text: "Capture" }
            TabButton { text: "Sessions" }
            TabButton { text: "Viewer" }
            TabButton { text: "Validation" }
            TabButton { text: "Lab" }
            TabButton { text: "Dashboard" }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: tabBar.currentIndex

            CaptureView { Layout.fillWidth: true; Layout.fillHeight: true }
            SessionsView { Layout.fillWidth: true; Layout.fillHeight: true }
            ViewerView { Layout.fillWidth: true; Layout.fillHeight: true }
            ValidationView { Layout.fillWidth: true; Layout.fillHeight: true }
            LabView { Layout.fillWidth: true; Layout.fillHeight: true }
            DashboardView { Layout.fillWidth: true; Layout.fillHeight: true }
        }
    }
}
