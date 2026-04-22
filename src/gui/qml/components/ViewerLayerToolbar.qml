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

    property color liveControlBg: '#050505'
    property color liveControlBorder: '#6e6e75'
    property color liveControlGlow: '#101214'
    property color liveControlPopup: '#090909'
    property color liveControlActive: '#1fd0d8'
    property color liveControlActiveText: '#031114'

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
            enabled: bar.chart.hasTrades
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
            text: "Live"
            color: bar.mutedTextColor
            font.pixelSize: 12
        }

        ComboBox {
            id: liveModeCombo
            Layout.preferredWidth: 108
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
                leftPadding: 12
                rightPadding: 28
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
            text: bar.appVm.liveUpdateMode === "tick" ? "JSON aggressive polling" : "JSON polling"
            color: bar.mutedTextColor
            font.pixelSize: 12
        }

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
