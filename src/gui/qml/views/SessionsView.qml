import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HftRecorder 1.0

Pane {
    SessionListModel {
        id: sessionsModel
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        RowLayout {
            Layout.fillWidth: true

            Label {
                text: "Recorded Sessions"
                font.pixelSize: 24
                font.bold: true
            }

            Item { Layout.fillWidth: true }

            Button {
                text: "Reload"
                onClicked: sessionsModel.reload()
            }
        }

        Label {
            text: "Current lightweight browser scans ./recordings for created session folders."
            color: "#666666"
            wrapMode: Text.WordWrap
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 12
            color: "#f5f7fb"
            border.color: "#d8deea"
            border.width: 1

            ListView {
                id: sessionsList
                anchors.fill: parent
                anchors.margins: 8
                clip: true
                model: sessionsModel
                spacing: 8

                delegate: Rectangle {
                    required property string sessionId

                    width: ListView.view.width
                    height: 56
                    radius: 10
                    color: "#ffffff"
                    border.color: "#d8deea"
                    border.width: 1

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 12

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4

                            Label {
                                text: sessionId
                                font.bold: true
                            }

                            Label {
                                text: "Session folder"
                                color: "#777777"
                            }
                        }
                    }
                }

                Label {
                    anchors.centerIn: parent
                    visible: sessionsList.count === 0
                    text: "No session folders found in ./recordings yet"
                    color: "#666666"
                }
            }
        }
    }
}
