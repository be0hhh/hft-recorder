import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: card

    required property var captureVm
    required property string channelKey
    required property string titleText
    required property string emptyText
    required property var availableAliases
    required property string requestPreview
    required property string weightSummary
    required property bool running
    required property string actionText
    required property color panelColor
    required property color panelAltColor
    required property color borderColor
    required property color textColor
    required property color mutedTextColor
    required property color accentRequiredColor
    required property color accentOptionalColor
    required property color accentSellColor
    required property color actionAccentColor
    required property color actionTextColor
    property bool actionVisible: true

    signal actionTriggered()

    Layout.fillWidth: true
    radius: 10
    color: card.panelAltColor
    border.color: card.borderColor
    border.width: 1
    implicitHeight: contentColumn.implicitHeight + 20

    ColumnLayout {
        id: contentColumn
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        RowLayout {
            Layout.fillWidth: true

            Label { text: card.titleText; font.bold: true; color: card.textColor }
            Item { Layout.fillWidth: true }
            CaptureWeightBadge {
                Layout.alignment: Qt.AlignRight
                badgeText: card.weightSummary
                panelColor: card.panelColor
                borderColor: card.borderColor
                textColor: card.textColor
                accentRequiredColor: card.accentRequiredColor
            }
        }

        Label {
            Layout.fillWidth: true
            visible: card.availableAliases.length < 1
            text: card.emptyText
            color: card.mutedTextColor
        }

        Flow {
            Layout.fillWidth: true
            spacing: 8

            Repeater {
                model: card.availableAliases.length

                delegate: CaptureAliasChip {
                    required property int index
                    property string aliasValue: card.availableAliases[index]

                    text: card.captureVm.aliasDisplayText(card.channelKey, aliasValue)
                    active: card.captureVm.isAliasSelected(card.channelKey, aliasValue)
                    required: card.captureVm.isRequiredAlias(card.channelKey, aliasValue)
                    panelColor: card.panelColor
                    borderColor: card.borderColor
                    textColor: card.textColor
                    accentRequiredColor: card.accentRequiredColor
                    accentOptionalColor: card.accentOptionalColor
                    onClicked: card.captureVm.toggleAlias(card.channelKey, aliasValue)
                }
            }
        }

        TextArea {
            Layout.fillWidth: true
            Layout.preferredHeight: 72
            readOnly: true
            wrapMode: TextEdit.WrapAnywhere
            text: card.requestPreview
            color: card.textColor
            selectByMouse: true
            background: Rectangle {
                radius: 8
                color: card.panelColor
                border.color: card.borderColor
                border.width: 1
            }
        }

        CaptureAccentActionButton {
            visible: card.actionVisible
            text: card.actionText
            accentColor: card.actionAccentColor
            actionTextColor: card.actionTextColor
            mutedTextColor: card.mutedTextColor
            onClicked: card.actionTriggered()
        }
    }
}
