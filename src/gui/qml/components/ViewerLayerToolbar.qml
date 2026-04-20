import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: bar

    required property var appVm
    required property var chart
    required property var interaction
    required property bool showTradesLayer
    required property bool showOrderbookLayer
    required property bool showBookTickerLayer
    required property bool effectiveBookTickerLayer
    required property color chromeColor
    required property color panelColor
    required property color panelAltColor
    required property color borderColor
    required property color textColor
    required property color mutedTextColor
    required property color accentBuyColor

    signal toggleTrades()
    signal toggleOrderbook()
    signal toggleBookTicker()

    Layout.fillWidth: true
    Layout.preferredHeight: 48
    color: bar.chromeColor
    border.color: bar.borderColor
    border.width: 1

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        spacing: 8

        ViewerChannelButton {
            text: "Trades"
            active: bar.showTradesLayer
            panelColor: bar.panelColor
            panelAltColor: bar.panelAltColor
            borderColor: bar.borderColor
            textColor: bar.textColor
            mutedTextColor: bar.mutedTextColor
            accentBuyColor: bar.accentBuyColor
            onClicked: bar.toggleTrades()
        }

        ViewerChannelButton {
            text: "Orderbook"
            active: bar.showOrderbookLayer
            enabled: bar.chart.hasOrderbook
            panelColor: bar.panelColor
            panelAltColor: bar.panelAltColor
            borderColor: bar.borderColor
            textColor: bar.textColor
            mutedTextColor: bar.mutedTextColor
            accentBuyColor: bar.accentBuyColor
            onClicked: bar.toggleOrderbook()
        }

        ViewerChannelButton {
            text: "BookTicker"
            active: bar.effectiveBookTickerLayer
            enabled: bar.chart.hasBookTicker
            panelColor: bar.panelColor
            panelAltColor: bar.panelAltColor
            borderColor: bar.borderColor
            textColor: bar.textColor
            mutedTextColor: bar.mutedTextColor
            accentBuyColor: bar.accentBuyColor
            onClicked: bar.toggleBookTicker()
        }

        Item { Layout.fillWidth: true }

        Label {
            text: "Trades Size"
            color: bar.mutedTextColor
            font.pixelSize: 12
        }

        Slider {
            Layout.preferredWidth: 120
            from: 0.0
            to: 1.0
            value: bar.appVm.tradeAmountScale
            onMoved: bar.appVm.tradeAmountScale = value
        }

        Label {
            text: Math.round(bar.appVm.tradeAmountScale * 100) + "%"
            color: bar.textColor
            font.pixelSize: 12
            Layout.preferredWidth: 34
            horizontalAlignment: Text.AlignRight
        }

        Label {
            text: "Full Bright @"
            color: bar.mutedTextColor
            font.pixelSize: 12
        }

        Slider {
            Layout.preferredWidth: 120
            from: 0.0
            to: 1.0
            value: bar.interaction.usdValueToSlider(bar.appVm.bookBrightnessUsdRef)
            onMoved: bar.appVm.bookBrightnessUsdRef = bar.interaction.usdSliderToValue(value)
        }

        Label {
            text: bar.interaction.formatUsdShort(bar.appVm.bookBrightnessUsdRef)
            color: bar.textColor
            font.pixelSize: 12
            Layout.preferredWidth: 58
            horizontalAlignment: Text.AlignRight
        }

        Label {
            text: "Min Visible"
            color: bar.mutedTextColor
            font.pixelSize: 12
        }

        Slider {
            Layout.preferredWidth: 120
            from: 0.0
            to: 1.0
            value: bar.interaction.usdValueToSlider(bar.appVm.bookMinVisibleUsd)
            onMoved: bar.appVm.bookMinVisibleUsd = bar.interaction.usdSliderToValue(value)
        }

        Label {
            text: bar.interaction.formatUsdShort(bar.appVm.bookMinVisibleUsd)
            color: bar.textColor
            font.pixelSize: 12
            Layout.preferredWidth: 58
            horizontalAlignment: Text.AlignRight
        }

        Item { Layout.fillWidth: true }

        Label {
            text: bar.appVm.renderDiagnosticsText
            color: bar.mutedTextColor
            font.pixelSize: 12
        }
    }
}
