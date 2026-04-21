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

    function clamp(value, lo, hi) {
        return Math.max(lo, Math.min(hi, value))
    }

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

        TextField {
            id: tradeSizeInput
            Layout.preferredWidth: 44
            text: ""
            color: bar.textColor
            selectionColor: bar.accentBuyColor
            selectedTextColor: "#101012"
            font.pixelSize: 12
            horizontalAlignment: Text.AlignRight
            validator: IntValidator { bottom: 0; top: 100 }
            background: Rectangle {
                color: bar.panelColor
                border.color: tradeSizeInput.activeFocus ? bar.accentBuyColor : bar.borderColor
                radius: 4
            }
            Binding {
                target: tradeSizeInput
                property: "text"
                value: Math.round(bar.appVm.tradeAmountScale * 100)
                when: !tradeSizeInput.activeFocus
            }
            onEditingFinished: {
                const pct = bar.clamp(Number(text), 0, 100)
                bar.appVm.tradeAmountScale = pct / 100.0
                text = Math.round(bar.appVm.tradeAmountScale * 100)
            }
        }

        Label { text: "%"; color: bar.mutedTextColor; font.pixelSize: 12 }

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

        TextField {
            id: fullBrightInput
            Layout.preferredWidth: 76
            text: ""
            color: bar.textColor
            selectionColor: bar.accentBuyColor
            selectedTextColor: "#101012"
            font.pixelSize: 12
            horizontalAlignment: Text.AlignRight
            validator: IntValidator { bottom: 1000; top: 1000000 }
            background: Rectangle {
                color: bar.panelColor
                border.color: fullBrightInput.activeFocus ? bar.accentBuyColor : bar.borderColor
                radius: 4
            }
            Binding {
                target: fullBrightInput
                property: "text"
                value: bar.interaction.formatUsdInput(bar.appVm.bookBrightnessUsdRef)
                when: !fullBrightInput.activeFocus
            }
            onEditingFinished: {
                bar.appVm.bookBrightnessUsdRef = bar.interaction.parseUsdInput(text, bar.appVm.bookBrightnessUsdRef)
                text = bar.interaction.formatUsdInput(bar.appVm.bookBrightnessUsdRef)
            }
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

        TextField {
            id: minVisibleInput
            Layout.preferredWidth: 76
            text: ""
            color: bar.textColor
            selectionColor: bar.accentBuyColor
            selectedTextColor: "#101012"
            font.pixelSize: 12
            horizontalAlignment: Text.AlignRight
            validator: IntValidator { bottom: 1000; top: 1000000 }
            background: Rectangle {
                color: bar.panelColor
                border.color: minVisibleInput.activeFocus ? bar.accentBuyColor : bar.borderColor
                radius: 4
            }
            Binding {
                target: minVisibleInput
                property: "text"
                value: bar.interaction.formatUsdInput(bar.appVm.bookMinVisibleUsd)
                when: !minVisibleInput.activeFocus
            }
            onEditingFinished: {
                bar.appVm.bookMinVisibleUsd = bar.interaction.parseUsdInput(text, bar.appVm.bookMinVisibleUsd)
                text = bar.interaction.formatUsdInput(bar.appVm.bookMinVisibleUsd)
            }
        }

        Label {
            text: "Depth Window"
            color: bar.mutedTextColor
            font.pixelSize: 12
        }

        Slider {
            Layout.preferredWidth: 110
            from: 1
            to: 25
            stepSize: 1
            snapMode: Slider.SnapAlways
            value: bar.appVm.bookDepthWindowPct
            onMoved: bar.appVm.bookDepthWindowPct = value
        }

        TextField {
            id: depthWindowInput
            Layout.preferredWidth: 40
            text: ""
            color: bar.textColor
            selectionColor: bar.accentBuyColor
            selectedTextColor: "#101012"
            font.pixelSize: 12
            horizontalAlignment: Text.AlignRight
            validator: IntValidator { bottom: 1; top: 25 }
            background: Rectangle {
                color: bar.panelColor
                border.color: depthWindowInput.activeFocus ? bar.accentBuyColor : bar.borderColor
                radius: 4
            }
            Binding {
                target: depthWindowInput
                property: "text"
                value: Math.round(bar.appVm.bookDepthWindowPct)
                when: !depthWindowInput.activeFocus
            }
            onEditingFinished: {
                const pct = bar.clamp(Number(text), 1, 25)
                bar.appVm.bookDepthWindowPct = pct
                text = Math.round(bar.appVm.bookDepthWindowPct)
            }
        }

        Label { text: "%"; color: bar.mutedTextColor; font.pixelSize: 12 }

        Item { Layout.fillWidth: true }

        Label {
            text: bar.appVm.renderDiagnosticsText
            color: bar.mutedTextColor
            font.pixelSize: 12
        }
    }
}
