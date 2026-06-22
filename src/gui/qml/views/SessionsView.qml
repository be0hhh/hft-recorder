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
            text: "Grouped recordings from ./recordings"
            color: root.mutedTextColor
            wrapMode: Text.WordWrap
        }

        TextField {
            id: searchField
            Layout.fillWidth: true
            placeholderText: "Search date, time, symbol, exchange, session"
            text: sessionsModel.searchText
            color: root.textColor
            placeholderTextColor: root.mutedTextColor
            selectionColor: root.accentBuyColor
            selectedTextColor: root.windowColor
            onTextChanged: sessionsModel.searchText = text
            background: Rectangle {
                radius: 8
                color: root.panelColor
                border.color: searchField.activeFocus ? root.accentBuyColor : root.borderColor
                border.width: 1
            }
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
                    required property string label
                    required property string sessionSummary
                    required property bool isGroup
                    required property int indent

                    width: ListView.view.width
                    height: isGroup ? 58 : 48
                    radius: 8
                    color: isGroup ? root.panelAltColor : root.panelColor
                    border.color: isGroup ? root.accentBuyColor : root.borderColor
                    border.width: 1

                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12 + indent * 18
                        anchors.rightMargin: 12
                        anchors.topMargin: 8
                        anchors.bottomMargin: 8

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Label {
                                Layout.fillWidth: true
                                text: label.length > 0 ? label : sessionId
                                font.bold: isGroup
                                color: root.textColor
                                elide: Text.ElideRight
                            }

                            Label {
                                Layout.preferredWidth: Math.max(150, implicitWidth + 8)
                                text: sessionSummary
                                color: root.mutedTextColor
                                font.bold: true
                                horizontalAlignment: Text.AlignRight
                                elide: Text.ElideRight
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
