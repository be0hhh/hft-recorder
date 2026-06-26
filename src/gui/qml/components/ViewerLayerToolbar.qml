import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: bar

    required property var appVm
    required property var chart
    required property var interaction
    required property bool showTradesLayer
    required property bool showLiquidationsLayer
    required property bool showCandlesLayer
    required property bool showCandles2Layer
    required property bool showOrderbookLayer
    required property bool showBookTickerLayer
    required property bool showMarkPriceLayer
    required property bool showIndexPriceLayer
    required property bool showFundingLayer
    required property bool showPriceLimitLayer
    required property bool showRateLimitLayer
    required property bool effectiveBookTickerLayer
    required property color chromeColor
    required property color panelColor
    required property color panelAltColor
    required property color borderColor
    required property color textColor
    required property color mutedTextColor
    required property color accentBuyColor
    property bool compareMode: false

    signal toggleTrades()
    signal toggleLiquidations()
    signal toggleCandles()
    signal toggleCandles2()
    signal toggleOrderbook()
    signal toggleBookTicker()
    signal toggleMarkPrice()
    signal toggleIndexPrice()
    signal toggleFunding()
    signal togglePriceLimit()
    signal toggleRateLimit()

    property color liveControlBg: '#050505'
    property color liveControlBorder: '#6e6e75'
    property color liveControlGlow: '#101214'
    property color liveControlPopup: '#090909'
    property color liveControlActive: '#1fd0d8'
    property color liveControlActiveText: '#031114'
    property bool compact: width < 2200
    property int toolbarSpacing: compact ? 4 : 8
    property int sliderMinWidth: compact ? 44 : 64
    property int sliderPreferredWidth: compact ? 64 : 104
    property int usdInputWidth: compact ? 54 : 70

    function clamp(value, lo, hi) {
        return Math.max(lo, Math.min(hi, value))
    }

    function liveModeIndex() {
        if (bar.appVm.liveUpdateMode === "tick")
            return 0
        if (bar.appVm.liveUpdateMode === "250ms")
            return 2
        if (bar.appVm.liveUpdateMode === "500ms")
            return 3
        return 1
    }

    Layout.fillWidth: true
    Layout.preferredHeight: 52
    color: bar.chromeColor
    border.color: bar.borderColor
    border.width: 1

    Flickable {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        anchors.topMargin: 2
        anchors.bottomMargin: 2
        clip: true
        contentWidth: Math.max(width, bar.compareMode ? compareToolbarRow.implicitWidth : toolbarRow.implicitWidth)
        contentHeight: height
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.HorizontalFlick

        ScrollBar.horizontal: ScrollBar {
            policy: ScrollBar.AsNeeded
            height: 6
        }

        Row {
            id: toolbarRow
            visible: !bar.compareMode
            height: parent.height - 6
            spacing: bar.toolbarSpacing

        ViewerChannelButton {
            text: "Trades"
            active: bar.showTradesLayer
            compact: bar.compact
            panelColor: bar.panelColor
            panelAltColor: bar.panelAltColor
            borderColor: bar.borderColor
            textColor: bar.textColor
            mutedTextColor: bar.mutedTextColor
            accentBuyColor: bar.accentBuyColor
            onClicked: bar.toggleTrades()
        }

        ViewerChannelButton {
            text: bar.compact ? "Liq" : "Liquidations"
            active: bar.showLiquidationsLayer
            compact: bar.compact
            panelColor: bar.panelColor
            panelAltColor: bar.panelAltColor
            borderColor: bar.borderColor
            textColor: bar.textColor
            mutedTextColor: bar.mutedTextColor
            accentBuyColor: bar.accentBuyColor
            onClicked: bar.toggleLiquidations()
        }

        ViewerChannelButton {
            text: "C"
            active: bar.showCandlesLayer
            compact: bar.compact
            panelColor: bar.panelColor
            panelAltColor: bar.panelAltColor
            borderColor: bar.borderColor
            textColor: bar.textColor
            mutedTextColor: bar.mutedTextColor
            accentBuyColor: bar.accentBuyColor
            onClicked: bar.toggleCandles()
        }

        ViewerChannelButton {
            text: "C2"
            active: bar.showCandles2Layer
            compact: bar.compact
            panelColor: bar.panelColor
            panelAltColor: bar.panelAltColor
            borderColor: bar.borderColor
            textColor: bar.textColor
            mutedTextColor: bar.mutedTextColor
            accentBuyColor: bar.accentBuyColor
            onClicked: bar.toggleCandles2()
        }
        ViewerChannelButton {
            text: bar.compact ? "Book" : "Orderbook"
            active: bar.showOrderbookLayer
            compact: bar.compact
            panelColor: bar.panelColor
            panelAltColor: bar.panelAltColor
            borderColor: bar.borderColor
            textColor: bar.textColor
            mutedTextColor: bar.mutedTextColor
            accentBuyColor: bar.accentBuyColor
            onClicked: bar.toggleOrderbook()
        }

        ViewerChannelButton {
            text: bar.compact ? "BBO" : "BookTicker"
            active: bar.effectiveBookTickerLayer
            compact: bar.compact
            panelColor: bar.panelColor
            panelAltColor: bar.panelAltColor
            borderColor: bar.borderColor
            textColor: bar.textColor
            mutedTextColor: bar.mutedTextColor
            accentBuyColor: bar.accentBuyColor
            onClicked: bar.toggleBookTicker()
        }

        ViewerChannelButton {
            text: "Mark"
            active: bar.showMarkPriceLayer
            compact: bar.compact
            panelColor: bar.panelColor
            panelAltColor: bar.panelAltColor
            borderColor: bar.borderColor
            textColor: bar.textColor
            mutedTextColor: bar.mutedTextColor
            accentBuyColor: bar.accentBuyColor
            onClicked: bar.toggleMarkPrice()
        }

        ViewerChannelButton {
            text: "Index"
            active: bar.showIndexPriceLayer
            compact: bar.compact
            panelColor: bar.panelColor
            panelAltColor: bar.panelAltColor
            borderColor: bar.borderColor
            textColor: bar.textColor
            mutedTextColor: bar.mutedTextColor
            accentBuyColor: bar.accentBuyColor
            onClicked: bar.toggleIndexPrice()
        }

        ViewerChannelButton {
            text: bar.compact ? "Fund" : "Funding"
            active: bar.showFundingLayer
            compact: bar.compact
            panelColor: bar.panelColor
            panelAltColor: bar.panelAltColor
            borderColor: bar.borderColor
            textColor: bar.textColor
            mutedTextColor: bar.mutedTextColor
            accentBuyColor: bar.accentBuyColor
            onClicked: bar.toggleFunding()
        }

        ViewerChannelButton {
            text: "Limits"
            active: bar.showPriceLimitLayer
            compact: bar.compact
            panelColor: bar.panelColor
            panelAltColor: bar.panelAltColor
            borderColor: bar.borderColor
            textColor: bar.textColor
            mutedTextColor: bar.mutedTextColor
            accentBuyColor: bar.accentBuyColor
            onClicked: bar.togglePriceLimit()
        }

        ViewerChannelButton {
            text: "Rate"
            active: bar.showRateLimitLayer
            compact: bar.compact
            panelColor: bar.panelColor
            panelAltColor: bar.panelAltColor
            borderColor: bar.borderColor
            textColor: bar.textColor
            mutedTextColor: bar.mutedTextColor
            accentBuyColor: bar.accentBuyColor
            onClicked: bar.toggleRateLimit()
        }

        Label {
            text: "Live"
            color: bar.mutedTextColor
            font.pixelSize: 12
        }

        ComboBox {
            id: liveModeCombo
            width: bar.compact ? 84 : 108
            Layout.preferredWidth: bar.compact ? 84 : 108
            model: ["Tick", "100 ms", "250 ms", "500 ms"]
            currentIndex: bar.liveModeIndex()
            font.pixelSize: 12

            background: Rectangle {
                radius: 9
                color: bar.liveControlBg
                border.color: liveModeCombo.popup.visible || liveModeCombo.hovered ? bar.liveControlActive : bar.liveControlBorder
                border.width: liveModeCombo.visualFocus || liveModeCombo.popup.visible ? 2 : 1
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 1
                    radius: parent.radius - 1
                    color: 'transparent'
                    border.color: bar.liveControlGlow
                    border.width: 1
                    opacity: 0.9
                }
            }

            contentItem: Text {
                leftPadding: bar.compact ? 8 : 12
                rightPadding: bar.compact ? 22 : 28
                text: liveModeCombo.displayText
                color: bar.textColor
                verticalAlignment: Text.AlignVCenter
                font.pixelSize: 12
                font.bold: true
                elide: Text.ElideRight
            }

            indicator: Canvas {
                x: liveModeCombo.width - width - 10
                y: (liveModeCombo.height - height) / 2
                width: 12
                height: 8
                contextType: '2d'
                onPaint: {
                    context.reset()
                    context.fillStyle = bar.liveControlActive
                    context.beginPath()
                    context.moveTo(0, 0)
                    context.lineTo(width, 0)
                    context.lineTo(width / 2, height)
                    context.closePath()
                    context.fill()
                }
            }

            delegate: ItemDelegate {
                id: liveModeDelegate
                required property int index
                width: liveModeCombo.width - 8
                height: 34
                highlighted: liveModeCombo.highlightedIndex === index
                background: Rectangle {
                    radius: 7
                    color: liveModeCombo.currentIndex === liveModeDelegate.index ? bar.liveControlActive : (liveModeDelegate.highlighted ? '#191b1e' : 'transparent')
                    border.color: liveModeCombo.currentIndex === liveModeDelegate.index ? '#8cf3f6' : 'transparent'
                    border.width: liveModeCombo.currentIndex === liveModeDelegate.index ? 1 : 0
                }
                contentItem: Text {
                    text: liveModeCombo.textAt(liveModeDelegate.index)
                    color: liveModeCombo.currentIndex === liveModeDelegate.index ? bar.liveControlActiveText : bar.textColor
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: 12
                    rightPadding: 12
                    font.pixelSize: 12
                    font.bold: liveModeCombo.currentIndex === liveModeDelegate.index
                    elide: Text.ElideRight
                }
            }

            popup: Popup {
                y: liveModeCombo.height + 6
                width: liveModeCombo.width
                padding: 4
                background: Rectangle {
                    radius: 10
                    color: bar.liveControlPopup
                    border.color: bar.liveControlBorder
                    border.width: 1
                }
                contentItem: ListView {
                    clip: true
                    spacing: 4
                    implicitHeight: contentHeight
                    model: liveModeCombo.popup.visible ? liveModeCombo.delegateModel : null
                    currentIndex: liveModeCombo.highlightedIndex
                }
            }
            onActivated: function(index) {
                if (index === 0)
                    bar.appVm.liveUpdateMode = "tick"
                else if (index === 1)
                    bar.appVm.liveUpdateMode = "100ms"
                else if (index === 2)
                    bar.appVm.liveUpdateMode = "250ms"
                else
                    bar.appVm.liveUpdateMode = "500ms"
            }
        }

        Label {
            text: bar.compact ? (bar.appVm.liveUpdateMode === "tick" ? "Aggressive" : "Polling") : (bar.appVm.liveUpdateMode === "tick" ? "JSON aggressive polling" : "JSON polling")
            color: bar.mutedTextColor
            font.pixelSize: 12
        }

        Label {
            text: "Window"
            color: bar.mutedTextColor
            font.pixelSize: 12
        }

        TextField {
            id: renderWindowInput
            width: bar.compact ? 46 : 58
            Layout.preferredWidth: bar.compact ? 46 : 58
            text: ""
            color: bar.textColor
            selectionColor: bar.accentBuyColor
            selectedTextColor: "#101012"
            font.pixelSize: 12
            horizontalAlignment: Text.AlignRight
            validator: IntValidator { bottom: -1; top: 86400 }
            background: Rectangle {
                color: bar.panelColor
                border.color: renderWindowInput.activeFocus ? bar.accentBuyColor : bar.borderColor
                radius: 4
            }
            Binding {
                target: renderWindowInput
                property: "text"
                value: bar.appVm.renderWindowSeconds
                when: !renderWindowInput.activeFocus
            }
            onEditingFinished: {
                var seconds = Number(text)
                if (!Number.isFinite(seconds))
                    seconds = 0
                bar.appVm.renderWindowSeconds = bar.clamp(Math.round(seconds), -1, 86400)
                text = bar.appVm.renderWindowSeconds
            }
        }

        Label { text: "s"; color: bar.mutedTextColor; font.pixelSize: 12 }

        Label {
            text: bar.compact ? "Trades" : "Trades Size"
            color: bar.mutedTextColor
            font.pixelSize: 12
        }

        Slider {
            width: bar.sliderPreferredWidth
            Layout.fillWidth: true
            Layout.minimumWidth: bar.sliderMinWidth
            Layout.preferredWidth: bar.sliderPreferredWidth
            from: 0.0
            to: 1.0
            value: bar.appVm.tradeAmountScale
            onMoved: bar.appVm.tradeAmountScale = value
        }

        TextField {
            id: tradeSizeInput
            width: bar.compact ? 36 : 44
            Layout.preferredWidth: bar.compact ? 36 : 44
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
                const pct = bar.clamp(Number(text), 0, 100);
                bar.appVm.tradeAmountScale = pct / 100.0
                text = Math.round(bar.appVm.tradeAmountScale * 100)
            }
        }

        Label { text: "%"; color: bar.mutedTextColor; font.pixelSize: 12 }

        Label {
            text: bar.compact ? "Candle" : "Candle Width"
            color: bar.mutedTextColor
            font.pixelSize: 12
        }

        Slider {
            width: bar.sliderPreferredWidth
            Layout.fillWidth: true
            Layout.minimumWidth: bar.sliderMinWidth
            Layout.preferredWidth: bar.sliderPreferredWidth
            from: 1
            to: 80
            stepSize: 1
            snapMode: Slider.SnapAlways
            value: bar.appVm.candleWidthPx
            onMoved: bar.appVm.candleWidthPx = value
        }

        TextField {
            id: candleWidthInput
            width: bar.compact ? 36 : 42
            Layout.preferredWidth: bar.compact ? 36 : 42
            text: ""
            color: bar.textColor
            selectionColor: bar.accentBuyColor
            selectedTextColor: "#101012"
            font.pixelSize: 12
            horizontalAlignment: Text.AlignRight
            validator: IntValidator { bottom: 1; top: 80 }
            background: Rectangle {
                color: bar.panelColor
                border.color: candleWidthInput.activeFocus ? bar.accentBuyColor : bar.borderColor
                radius: 4
            }
            Binding {
                target: candleWidthInput
                property: "text"
                value: Math.round(bar.appVm.candleWidthPx)
                when: !candleWidthInput.activeFocus
            }
            onEditingFinished: {
                const px = bar.clamp(Number(text), 1, 80);
                bar.appVm.candleWidthPx = px
                text = Math.round(bar.appVm.candleWidthPx)
            }
        }

        Label { text: "px"; color: bar.mutedTextColor; font.pixelSize: 12 }

        Label {
            text: bar.compact ? "Bright @" : "Full Bright @"
            color: bar.mutedTextColor
            font.pixelSize: 12
        }

        Slider {
            width: bar.sliderPreferredWidth
            Layout.fillWidth: true
            Layout.minimumWidth: bar.sliderMinWidth
            Layout.preferredWidth: bar.sliderPreferredWidth
            from: 0.0
            to: 1.0
            value: bar.interaction.usdValueToSlider(bar.appVm.bookBrightnessUsdRef)
            onMoved: bar.appVm.bookBrightnessUsdRef = bar.interaction.usdSliderToValue(value)
        }

        TextField {
            id: fullBrightInput
            width: bar.usdInputWidth
            Layout.preferredWidth: bar.usdInputWidth
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
            text: bar.compact ? "Min"
                              : "Min Visible"
            color: bar.mutedTextColor
            font.pixelSize: 12
        }

        Slider {
            width: bar.sliderPreferredWidth
            Layout.fillWidth: true
            Layout.minimumWidth: bar.sliderMinWidth
            Layout.preferredWidth: bar.sliderPreferredWidth
            from: 0.0
            to: 1.0
            value: bar.interaction.usdValueToSliderMin0(bar.appVm.bookMinVisibleUsd)
            onMoved: bar.appVm.bookMinVisibleUsd = bar.interaction.usdSliderToValueMin0(value)
        }

        TextField {
            id: minVisibleInput
            width: bar.usdInputWidth
            Layout.preferredWidth: bar.usdInputWidth
            text: ""
            color: bar.textColor
            selectionColor: bar.accentBuyColor
            selectedTextColor: "#101012"
            font.pixelSize: 12
            horizontalAlignment: Text.AlignRight
            validator: IntValidator { bottom: 0; top: 1000000 }
            background: Rectangle {
                color: bar.panelColor
                border.color: minVisibleInput.activeFocus ? bar.accentBuyColor : bar.borderColor
                radius: 4
            }
            Binding {
                target: minVisibleInput
                property: "text"
                value: bar.interaction.formatUsdInputMin0(bar.appVm.bookMinVisibleUsd)
                when: !minVisibleInput.activeFocus
            }
            onEditingFinished: {
                bar.appVm.bookMinVisibleUsd = bar.interaction.parseUsdInputMin0(text, bar.appVm.bookMinVisibleUsd)
                text = bar.interaction.formatUsdInputMin0(bar.appVm.bookMinVisibleUsd)
            }
        }

        Label {
            text: bar.compact ? "Depth" : "Depth Window"
            color: bar.mutedTextColor
            font.pixelSize: 12
        }

        Slider {
            width: bar.sliderPreferredWidth
            Layout.fillWidth: true
            Layout.minimumWidth: bar.sliderMinWidth
            Layout.preferredWidth: bar.sliderPreferredWidth
            from: 1
            to: 25
            stepSize: 1
            snapMode: Slider.SnapAlways
            value: bar.appVm.bookDepthWindowPct
            onMoved: bar.appVm.bookDepthWindowPct = value
        }

        TextField {
            id: depthWindowInput
            width: bar.compact ? 34 : 40
            Layout.preferredWidth: bar.compact ? 34 : 40
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
                const pct = bar.clamp(Number(text), 1, 25);
                bar.appVm.bookDepthWindowPct = pct
                text = Math.round(bar.appVm.bookDepthWindowPct)
            }
        }

        Label { text: "%"; color: bar.mutedTextColor; font.pixelSize: 12 }
        }

        Row {
            id: compareToolbarRow
            visible: bar.compareMode
            height: parent.height - 6
            spacing: bar.toolbarSpacing

            ViewerChannelButton {
                text: "Rate"
                active: bar.showRateLimitLayer
                compact: bar.compact
                panelColor: bar.panelColor
                panelAltColor: bar.panelAltColor
                borderColor: bar.borderColor
                textColor: bar.textColor
                mutedTextColor: bar.mutedTextColor
                accentBuyColor: bar.accentBuyColor
                onClicked: bar.toggleRateLimit()
            }

            Label {
                text: "Lower pane"
                color: bar.mutedTextColor
                font.pixelSize: 12
            }
        }
}
}

