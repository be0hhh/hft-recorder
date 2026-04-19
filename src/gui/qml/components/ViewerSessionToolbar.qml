import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: bar

    required property var sessionsModel
    required property string selectedSessionId
    required property color chromeColor
    required property color panelColor
    required property color panelAltColor
    required property color borderColor
    required property color textColor
    required property color mutedTextColor

    signal sessionActivated(string sessionId)
    signal reloadRequested()
    signal sessionCountChanged()

    Layout.fillWidth: true
    Layout.preferredHeight: 50
    color: bar.chromeColor
    border.color: bar.borderColor
    border.width: 1

    function findSessionIndex(sessionId) {
        return sessionPicker.find(sessionId)
    }

    function setCurrentIndex(index) {
        sessionPicker.currentIndex = index
    }

    function currentIndex() {
        return sessionPicker.currentIndex
    }

    function textAt(index) {
        return sessionPicker.textAt(index)
    }

    function currentText() {
        return sessionPicker.currentText
    }

    function count() {
        return sessionPicker.count
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        spacing: 10

        Label {
            text: "Session:"
            color: bar.textColor
            font.pixelSize: 14
        }

        ComboBox {
            id: sessionPicker
            Layout.fillWidth: true
            model: bar.sessionsModel
            textRole: "sessionId"

            background: Rectangle {
                radius: 7
                color: bar.panelColor
                border.color: bar.borderColor
                border.width: 1
            }

            contentItem: Text {
                leftPadding: 12
                rightPadding: 12
                text: sessionPicker.displayText
                color: bar.textColor
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                font.pixelSize: 13
            }

            indicator: Canvas {
                x: sessionPicker.width - width - 10
                y: (sessionPicker.height - height) / 2
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
                required property string sessionId
                width: sessionPicker.width
                text: sessionId
                highlighted: sessionPicker.highlightedIndex === index
                background: Rectangle {
                    color: delegateControl.highlighted ? bar.panelAltColor : bar.panelColor
                }
                contentItem: Text {
                    text: delegateControl.text
                    color: bar.textColor
                    verticalAlignment: Text.AlignVCenter
                    elide: Text.ElideRight
                    font.pixelSize: 13
                }
            }

            popup: Popup {
                y: sessionPicker.height + 4
                width: sessionPicker.width
                padding: 4
                background: Rectangle {
                    radius: 7
                    color: bar.panelColor
                    border.color: bar.borderColor
                    border.width: 1
                }
                contentItem: ListView {
                    clip: true
                    implicitHeight: contentHeight
                    model: sessionPicker.popup.visible ? sessionPicker.delegateModel : null
                    currentIndex: sessionPicker.highlightedIndex
                }
            }

            onActivated: {
                if (currentIndex < 0)
                    return
                bar.sessionActivated(currentText)
            }

            onCountChanged: bar.sessionCountChanged()
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
