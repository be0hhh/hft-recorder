import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: card

    required property var captureVm
    required property color panelColor
    required property color panelAltColor
    required property color borderColor
    required property color textColor
    required property color mutedTextColor
    required property color accentBuyColor

    Layout.fillWidth: true
    radius: 12
    color: card.panelColor
    border.color: card.borderColor
    border.width: 1
    implicitHeight: infoColumn.implicitHeight + 24

    ColumnLayout {
        id: infoColumn
        anchors.fill: parent
        anchors.margins: 12
        spacing: 10

        Label { text: "Parent Directory"; font.bold: true; color: card.textColor }

        TextField {
            Layout.fillWidth: true
            text: card.captureVm.outputDirectory
            readOnly: true
            color: card.textColor
            selectedTextColor: card.textColor
            selectionColor: card.accentBuyColor
            background: Rectangle {
                radius: 8
                color: card.panelAltColor
                border.color: card.borderColor
                border.width: 1
            }
        }

        Label { text: "Symbols"; font.bold: true; color: card.textColor }

        TextField {
            Layout.fillWidth: true
            text: card.captureVm.symbolsText
            placeholderText: "RAVE BTC ETH"
            color: card.textColor
            placeholderTextColor: card.mutedTextColor
            selectedTextColor: card.textColor
            selectionColor: card.accentBuyColor
            enabled: !card.captureVm.sessionOpen
            onTextChanged: card.captureVm.setSymbolsText(text)
            background: Rectangle {
                radius: 8
                color: card.panelAltColor
                border.color: card.borderColor
                border.width: 1
            }
        }

        Label {
            text: "Normalized: " + (card.captureVm.normalizedSymbolsText === "" ? "<none>" : card.captureVm.normalizedSymbolsText)
            color: card.mutedTextColor
            wrapMode: Text.WordWrap
        }

        Label { text: "Session ID: " + (card.captureVm.sessionId === "" ? "<not started>" : card.captureVm.sessionId); color: card.textColor }
        Label { text: "Session Path: " + (card.captureVm.sessionPath === "" ? "<not created>" : card.captureVm.sessionPath); color: card.textColor }
        Label { text: "Status: " + card.captureVm.statusText; wrapMode: Text.WordWrap; color: card.mutedTextColor }

        RowLayout {
            Layout.fillWidth: true
            spacing: 18

            Label { text: "Trades: " + card.captureVm.tradesCount; color: card.textColor }
            Label { text: "Liquidations: " + card.captureVm.liquidationsCount; color: card.textColor }
            Label { text: "BookTicker: " + card.captureVm.bookTickerCount; color: card.textColor }
            Label { text: "Depth: " + card.captureVm.depthCount; color: card.textColor }
            Item { Layout.fillWidth: true }
        }
    }
}
