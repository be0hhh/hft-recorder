import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HftRecorder 1.0

Pane {
    id: root
    property color windowColor: "#161616"
    property color panelColor: "#2c2c2f"
    property color panelAltColor: "#3c3c3c"
    property color borderColor: "#3c3d3f"
    property color textColor: "#f5f5f5"
    property color mutedTextColor: "#b6b6b6"
    property color accentBuyColor: "#24c2cb"

    background: Rectangle { color: root.windowColor }

    component DarkButton: Button {
        id: control
        background: Rectangle {
            radius: 8
            color: root.panelColor
            border.color: root.borderColor
            border.width: 1
        }
        contentItem: Text {
            text: control.text
            color: root.textColor
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
        }
    }

    SessionListModel { id: sessionsModel }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        RowLayout {
            Layout.fillWidth: true

            Label {
                text: "Recorded Sessions"
                font.pixelSize: 24
                font.bold: true
                color: root.textColor
            }

            Item { Layout.fillWidth: true }

            DarkButton {
                text: "Reload"
                onClicked: sessionsModel.reload()
            }
        }

        Label {
            text: "Current lightweight browser scans ./recordings for created session folders."
            color: root.mutedTextColor
            wrapMode: Text.WordWrap
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: 12
            color: root.panelColor
            border.color: root.borderColor
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
                    color: root.panelAltColor
                    border.color: root.accentBuyColor
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
                                color: root.textColor
                            }

                            Label {
                                text: "Session folder"
                                color: root.mutedTextColor
                            }
                        }
                    }
                }

                Label {
                    anchors.centerIn: parent
                    visible: sessionsList.count === 0
                    text: "No session folders found in ./recordings yet"
                    color: root.mutedTextColor
                }
            }
        }
    }
}
