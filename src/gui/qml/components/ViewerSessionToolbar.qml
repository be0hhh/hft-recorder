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

            background: Rectangle { radius: 7; color: bar.panelColor; border.color: bar.borderColor; border.width: 1 }

            contentItem: Text {
                leftPadding: 12
                rightPadding: 12
                text: sourcePicker.displayText
                color: bar.textColor
                verticalAlignment: Text.AlignVCenter
                elide: Text.ElideRight
                font.pixelSize: 13
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

                    Text {
                        width: parent.width
                        leftPadding: 12
                        rightPadding: 12
                        topPadding: 8
                        bottomPadding: 8
                        text: delegateControl.label
                        color: bar.textColor
                        verticalAlignment: Text.AlignVCenter
                        elide: Text.ElideRight
                        font.pixelSize: 13
                    }
                }
            }

            popup: Popup {
                y: sourcePicker.height + 4
                width: sourcePicker.width
                padding: 4
                background: Rectangle { radius: 7; color: bar.panelColor; border.color: bar.borderColor; border.width: 1 }
                contentItem: ListView {
                    clip: true
                    implicitHeight: Math.min(contentHeight, 360)
                    model: sourcePicker.popup.visible ? sourcePicker.delegateModel : null
                    currentIndex: sourcePicker.highlightedIndex
                }
            }

            onActivated: {
                if (currentIndex < 0) return
                bar.sourceActivated(currentValue)
            }

            onCountChanged: bar.sourceCountChanged()
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

