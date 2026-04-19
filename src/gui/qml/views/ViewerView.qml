import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import HftRecorder 1.0

Pane {
    id: root
    padding: 0
    focus: true
    required property AppViewModel appVm

    property color windowColor: "#161616"
    property color chromeColor: "#202024"
    property color panelColor: "#2c2c2f"
    property color panelAltColor: "#35353a"
    property color borderColor: "#49494f"
    property color textColor: "#f5f5f5"
    property color mutedTextColor: "#aaaaaf"
    property color accentBuyColor: "#24c2cb"
    property color chartColor: "#202022"
    property color scaleColor: "#26262b"
    property int priceTickCount: 6
    property int timeTickCount: 5
    property string selectedSessionId: ""
    property bool showTradesLayer: true
    property bool showOrderbookLayer: false
    property bool showBookTickerLayer: false
    property bool effectiveBookTickerLayer: showBookTickerLayer || showOrderbookLayer
    property bool interactiveMode: false
    // Kept for backwards compatibility with existing QML references; the
    // scene-graph `ChartItem` always runs on the GPU now (Qt RHI composites
    // QSGGeometryNode natively — no separate GPU/software switch needed).
    property bool useGpuRenderer: true
    property bool plotDragging: false
    property bool priceScaleDragging: false
    property bool timeScaleDragging: false
    property bool rangeSelectionActive: false
    property bool selectionCommitted: false
    property real selectionStartX: 0
    property real selectionStartY: 0
    property real selectionEndX: 0
    property real selectionEndY: 0

    function clearSelectionVisual() {
        root.rangeSelectionActive = false
        root.selectionCommitted = false
        root.selectionStartX = 0
        root.selectionStartY = 0
        root.selectionEndX = 0
        root.selectionEndY = 0
    }

    function usdSliderToValue(sliderValue) {
        const clamped = Math.max(0.0, Math.min(1.0, sliderValue))
        const minUsd = 100.0
        const maxUsd = 100000.0
        return minUsd + (maxUsd - minUsd) * clamped
    }

    function usdValueToSlider(usdValue) {
        const minUsd = 100.0
        const maxUsd = 100000.0
        const clamped = Math.max(minUsd, Math.min(maxUsd, usdValue))
        return (clamped - minUsd) / (maxUsd - minUsd)
    }

    function formatUsdShort(usdValue) {
        if (usdValue >= 1000)
            return "$" + (usdValue / 1000).toFixed(usdValue >= 10000 ? 0 : 1).replace(/\.0$/, "") + "k"
        if (usdValue >= 100)
            return "$" + Math.round(usdValue)
        if (usdValue >= 10)
            return "$" + usdValue.toFixed(1).replace(/\.0$/, "")
        return "$" + usdValue.toFixed(2).replace(/0+$/, "").replace(/\.$/, "")
    }

    function selectionLeft() {
        return Math.min(root.selectionStartX, root.selectionEndX)
    }

    function selectionTop() {
        return Math.min(root.selectionStartY, root.selectionEndY)
    }

    function selectionWidth() {
        return Math.abs(root.selectionEndX - root.selectionStartX)
    }

    function selectionHeight() {
        return Math.abs(root.selectionEndY - root.selectionStartY)
    }

    function beginSelection(x, y) {
        root.rangeSelectionActive = true
        root.selectionCommitted = false
        root.selectionStartX = x
        root.selectionStartY = y
        root.selectionEndX = x
        root.selectionEndY = y
    }

    function updateSelection(x, y) {
        root.selectionEndX = Math.max(0, Math.min(plotFrame.width, x))
        root.selectionEndY = Math.max(0, Math.min(plotFrame.height, y))
    }

    function commitSelection() {
        root.rangeSelectionActive = false
        root.selectionCommitted = chart.commitSelectionRect(
            plotFrame.width, plotFrame.height,
            root.selectionStartX, root.selectionStartY,
            root.selectionEndX, root.selectionEndY)
        if (!root.selectionCommitted)
            root.clearSelectionVisual()
    }

    function syncChannelView() {
        root.clearSelectionVisual()
        chart.clearSelection()
        if (selectedSessionId === "") {
            chart.resetSession()
            return
        }
        chart.loadSession("./recordings/" + selectedSessionId)
    }

    function anyHoverableLayerVisible() {
        return root.showTradesLayer || root.effectiveBookTickerLayer || root.showOrderbookLayer
    }

    function activeInteractionItem() {
        return chartItem
    }

    function ensureSessionSelection() {
        if (sessionPicker.count <= 0) {
            selectedSessionId = ""
            chart.resetSession()
            return
        }

        var desiredIndex = sessionPicker.find(selectedSessionId)
        if (desiredIndex < 0)
            desiredIndex = Math.max(0, sessionPicker.currentIndex)
        if (desiredIndex < 0)
            desiredIndex = 0

        sessionPicker.currentIndex = desiredIndex
        var nextSessionId = sessionPicker.textAt(desiredIndex)
        if (nextSessionId === "")
            nextSessionId = sessionPicker.currentText

        if (selectedSessionId !== nextSessionId) {
            selectedSessionId = nextSessionId
            syncChannelView()
        } else if (chart.loaded !== true) {
            syncChannelView()
        }
    }

    component ChromeButton: Button {
        id: control
        background: Rectangle {
            radius: 7
            color: control.down ? root.panelAltColor : root.panelColor
            border.color: root.borderColor
            border.width: 1
        }
        contentItem: Text {
            text: control.text
            color: root.textColor
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: 13
        }
    }

    component ChannelButton: Button {
        id: control
        property bool active: false
        background: Rectangle {
            radius: 7
            color: control.active ? root.panelAltColor : root.panelColor
            border.color: control.active ? root.accentBuyColor : root.borderColor
            border.width: 1
        }
        contentItem: Text {
            text: control.text
            color: control.active ? root.textColor : root.mutedTextColor
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            font.pixelSize: 13
            font.bold: control.active
        }
    }

    background: Rectangle { color: root.windowColor }

    SessionListModel { id: sessionsModel; Component.onCompleted: reload() }
    ChartController { id: chart }
    Timer {
        id: interactiveModeTimer
        interval: 120
        repeat: false
        onTriggered: root.interactiveMode = false
    }
    Component.onCompleted: Qt.callLater(root.ensureSessionSelection)

    Connections {
        target: sessionsModel
        function onModelReset() {
            Qt.callLater(root.ensureSessionSelection)
        }
    }

    function startInteractiveMode() {
        root.interactiveMode = true
        interactiveModeTimer.restart()
    }

    function stopInteractiveModeSoon() {
        interactiveModeTimer.restart()
    }

    Keys.onEscapePressed: {
        root.clearSelectionVisual()
        chart.clearSelection()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 50
            color: root.chromeColor
            border.color: root.borderColor
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 10

                Label {
                    text: "Session:"
                    color: root.textColor
                    font.pixelSize: 14
                }

                ComboBox {
                    id: sessionPicker
                    Layout.fillWidth: true
                    model: sessionsModel
                    textRole: "sessionId"

                    background: Rectangle {
                        radius: 7
                        color: root.panelColor
                        border.color: root.borderColor
                        border.width: 1
                    }

                    contentItem: Text {
                        leftPadding: 12
                        rightPadding: 12
                        text: sessionPicker.displayText
                        color: root.textColor
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
                            context.fillStyle = root.mutedTextColor
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
                            color: delegateControl.highlighted ? root.panelAltColor : root.panelColor
                        }
                        contentItem: Text {
                            text: delegateControl.text
                            color: root.textColor
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
                            color: root.panelColor
                            border.color: root.borderColor
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
                        selectedSessionId = currentText
                        syncChannelView()
                    }

                    onCountChanged: Qt.callLater(root.ensureSessionSelection)
                }

                ChromeButton {
                    text: "Reload"
                    onClicked: {
                        sessionsModel.reload()
                        Qt.callLater(root.ensureSessionSelection)
                    }
                }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 48
            color: root.chromeColor
            border.color: root.borderColor
            border.width: 1

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 8

                ChannelButton {
                    text: "Trades"
                    active: root.showTradesLayer
                    onClicked: {
                        root.showTradesLayer = !root.showTradesLayer
                        if (root.showTradesLayer && !chart.loaded && root.selectedSessionId !== "")
                            root.syncChannelView()
                    }
                }

                ChannelButton {
                    text: "Orderbook"
                    active: root.showOrderbookLayer
                    enabled: chart.hasOrderbook
                    onClicked: {
                        root.showOrderbookLayer = !root.showOrderbookLayer
                    }
                }

                ChannelButton {
                    text: "BookTicker"
                    active: root.effectiveBookTickerLayer
                    enabled: chart.hasBookTicker
                    onClicked: {
                        root.showBookTickerLayer = !root.showBookTickerLayer
                    }
                }

                Item { Layout.fillWidth: true }

                Label {
                    text: "Trades Size"
                    color: root.mutedTextColor
                    font.pixelSize: 12
                }

                Slider {
                    id: tradeSizeSlider
                    Layout.preferredWidth: 120
                    from: 0.0
                    to: 1.0
                    value: root.appVm.tradeAmountScale
                    onMoved: root.appVm.tradeAmountScale = value
                }

                Label {
                    text: Math.round(root.appVm.tradeAmountScale * 100) + "%"
                    color: root.textColor
                    font.pixelSize: 12
                    Layout.preferredWidth: 34
                    horizontalAlignment: Text.AlignRight
                }

                Label {
                    text: "Full Bright @"
                    color: root.mutedTextColor
                    font.pixelSize: 12
                }

                Slider {
                    id: bookDensitySlider
                    Layout.preferredWidth: 120
                    from: 0.0
                    to: 1.0
                    value: root.usdValueToSlider(root.appVm.bookBrightnessUsdRef)
                    onMoved: root.appVm.bookBrightnessUsdRef = root.usdSliderToValue(value)
                }

                Label {
                    text: root.formatUsdShort(root.appVm.bookBrightnessUsdRef)
                    color: root.textColor
                    font.pixelSize: 12
                    Layout.preferredWidth: 58
                    horizontalAlignment: Text.AlignRight
                }

                Label {
                    text: "Min Visible"
                    color: root.mutedTextColor
                    font.pixelSize: 12
                }

                Slider {
                    id: bookDetailSlider
                    Layout.preferredWidth: 120
                    from: 0.0
                    to: 1.0
                    value: root.usdValueToSlider(root.appVm.bookMinVisibleUsd)
                    onMoved: root.appVm.bookMinVisibleUsd = root.usdSliderToValue(value)
                }

                Label {
                    text: root.formatUsdShort(root.appVm.bookMinVisibleUsd)
                    color: root.textColor
                    font.pixelSize: 12
                    Layout.preferredWidth: 58
                    horizontalAlignment: Text.AlignRight
                }

                Item { Layout.fillWidth: true }
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true

            Rectangle {
                anchors.fill: parent
                color: root.chartColor
            }

            Item {
                id: plotFrame
                anchors.fill: parent
                anchors.rightMargin: 88
                anchors.bottomMargin: 38

                ChartItem {
                    id: chartItem
                    anchors.fill: parent
                    controller: chart
                    tradesVisible: root.showTradesLayer
                    orderbookVisible: root.showOrderbookLayer
                    bookTickerVisible: root.effectiveBookTickerLayer
                    tradeAmountScale: root.appVm.tradeAmountScale
                    bookOpacityGain: root.appVm.bookBrightnessUsdRef
                    bookRenderDetail: root.appVm.bookMinVisibleUsd
                    interactiveMode: root.interactiveMode
                }

                MouseArea {
                    anchors.fill: parent
                    property real lastX: 0
                    property real lastY: 0
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    hoverEnabled: true
                    preventStealing: true

                    onPressed: function(mouse) {
                        if (mouse.button === Qt.RightButton) {
                            root.activeInteractionItem().activateContextPoint(mouse.x, mouse.y)
                            return
                        }
                        if ((mouse.modifiers & Qt.ShiftModifier) && mouse.button === Qt.LeftButton) {
                            root.startInteractiveMode()
                            root.beginSelection(mouse.x, mouse.y)
                            root.activeInteractionItem().clearHover()
                            return
                        }
                        root.startInteractiveMode()
                        root.plotDragging = true
                        lastX = mouse.x
                        lastY = mouse.y
                        root.activeInteractionItem().clearHover()
                    }

                    onPositionChanged: function(mouse) {
                        if (root.rangeSelectionActive) {
                            root.updateSelection(mouse.x, mouse.y)
                            return
                        }
                        if (mouse.buttons & Qt.LeftButton) {
                            var dx = mouse.x - lastX
                            var dy = mouse.y - lastY
                            lastX = mouse.x
                            lastY = mouse.y
                            chart.panTime(-dx / Math.max(1, width))
                            chart.panPrice(dy / Math.max(1, height))
                            return
                        }
                        if (root.priceScaleDragging || root.timeScaleDragging)
                            return
                        if (!root.anyHoverableLayerVisible())
                            return
                        root.activeInteractionItem().setHoverPoint(mouse.x, mouse.y)
                    }

                    onReleased: {
                        if (root.rangeSelectionActive) {
                            root.commitSelection()
                            root.stopInteractiveModeSoon()
                            return
                        }
                        root.plotDragging = false
                        root.stopInteractiveModeSoon()
                    }
                    onCanceled: {
                        if (root.rangeSelectionActive)
                            root.clearSelectionVisual()
                        root.plotDragging = false
                        root.stopInteractiveModeSoon()
                    }
                    onExited: {
                        if (!root.rangeSelectionActive)
                            root.activeInteractionItem().clearHover()
                    }

                    onWheel: function(wheel) {
                        root.startInteractiveMode()
                        root.activeInteractionItem().clearHover()
                        var factor = wheel.angleDelta.y > 0 ? 1.18 : 0.84
                        chart.zoomTime(factor)
                        chart.zoomPrice(factor)
                        root.stopInteractiveModeSoon()
                        wheel.accepted = true
                    }
                }

                Rectangle {
                    visible: root.rangeSelectionActive || root.selectionCommitted
                    x: root.selectionLeft()
                    y: root.selectionTop()
                    width: root.selectionWidth()
                    height: root.selectionHeight()
                    color: "#2448c8d3"
                    border.color: root.accentBuyColor
                    border.width: 1
                }

                Rectangle {
                    id: selectionSummaryCard
                    visible: root.selectionCommitted && chart.selectionActive && chart.selectionSummaryText !== ""
                    radius: 8
                    color: "#f014161a"
                    border.color: root.accentBuyColor
                    border.width: 1
                    width: Math.min(Math.max(340, plotFrame.width * 0.34), 460)
                    x: {
                        const desiredRight = root.selectionLeft() + root.selectionWidth() + 12
                        const fallbackLeft = root.selectionLeft() - width - 12
                        if (desiredRight + width <= plotFrame.width - 8)
                            return desiredRight
                        if (fallbackLeft >= 8)
                            return fallbackLeft
                        return Math.max(8, plotFrame.width - width - 8)
                    }
                    y: {
                        const desiredTop = root.selectionTop()
                        const maxY = plotFrame.height - height - 8
                        return Math.max(8, Math.min(desiredTop, maxY))
                    }
                    height: summaryText.implicitHeight + 18

                    Text {
                        id: summaryText
                        anchors.fill: parent
                        anchors.margins: 12
                        text: chart.selectionSummaryText
                        color: root.textColor
                        font.pixelSize: 12
                        font.family: "Consolas"
                        wrapMode: Text.Wrap
                        lineHeight: 1.18
                        lineHeightMode: Text.ProportionalHeight
                        textFormat: Text.PlainText
                    }
                }
            }

            Rectangle {
                id: priceScale
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.bottom: timeScale.top
                width: 88
                color: root.scaleColor
                border.color: root.borderColor
                border.width: 1

                Repeater {
                    model: root.priceTickCount
                    delegate: Item {
                        required property int index
                        property real tickRatio: index / Math.max(1, root.priceTickCount - 1)
                        width: priceScale.width
                        height: 20
                        y: (priceScale.height - height) * tickRatio

                        Label {
                            anchors.right: parent.right
                            anchors.rightMargin: 10
                            anchors.verticalCenter: parent.verticalCenter
                            text: {
                                let _ = chart.priceMinE8 + chart.priceMaxE8
                                return chart.formatPriceScaleLabel(index, root.priceTickCount)
                            }
                            color: root.mutedTextColor
                            font.pixelSize: 12
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    property real lastY: 0
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    cursorShape: Qt.SizeVerCursor
                    preventStealing: true

                    onPressed: function(mouse) {
                        root.startInteractiveMode()
                        root.priceScaleDragging = true
                        lastY = mouse.y
                        root.activeInteractionItem().clearHover()
                    }

                    onPositionChanged: function(mouse) {
                        if (!(mouse.buttons & Qt.LeftButton) && !(mouse.buttons & Qt.RightButton))
                            return
                        var dy = mouse.y - lastY
                        lastY = mouse.y
                        chart.zoomPrice(Math.exp(-dy * 0.012))
                    }

                    onReleased: {
                        root.priceScaleDragging = false
                        root.stopInteractiveModeSoon()
                    }
                    onCanceled: {
                        root.priceScaleDragging = false
                        root.stopInteractiveModeSoon()
                    }
                }
            }

            Rectangle {
                id: timeScale
                anchors.left: parent.left
                anchors.right: priceScale.left
                anchors.bottom: parent.bottom
                height: 38
                color: root.scaleColor
                border.color: root.borderColor
                border.width: 1

                Repeater {
                    model: root.timeTickCount
                    delegate: Item {
                        required property int index
                        property real tickRatio: index / Math.max(1, root.timeTickCount - 1)
                        width: 70
                        height: timeScale.height
                        x: (timeScale.width - width) * tickRatio

                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.verticalCenter: parent.verticalCenter
                            text: {
                                let _ = chart.tsMin + chart.tsMax
                                return chart.formatTimeAt(parent.tickRatio)
                            }
                            color: root.mutedTextColor
                            font.pixelSize: 12
                        }
                    }
                }

                MouseArea {
                    anchors.fill: parent
                    property real lastX: 0
                    acceptedButtons: Qt.LeftButton | Qt.RightButton
                    cursorShape: Qt.SizeHorCursor
                    preventStealing: true

                    onPressed: function(mouse) {
                        root.startInteractiveMode()
                        root.timeScaleDragging = true
                        lastX = mouse.x
                        root.activeInteractionItem().clearHover()
                    }

                    onPositionChanged: function(mouse) {
                        if (!(mouse.buttons & Qt.LeftButton) && !(mouse.buttons & Qt.RightButton))
                            return
                        var dx = mouse.x - lastX
                        lastX = mouse.x
                        chart.zoomTime(Math.exp(dx * 0.012))
                    }

                    onReleased: {
                        root.timeScaleDragging = false
                        root.stopInteractiveModeSoon()
                    }
                    onCanceled: {
                        root.timeScaleDragging = false
                        root.stopInteractiveModeSoon()
                    }
                }
            }

            Rectangle {
                anchors.left: parent.left
                anchors.top: parent.top
                anchors.leftMargin: 16
                anchors.topMargin: 14
                radius: 8
                color: "#2b2b31"
                border.color: root.borderColor
                border.width: 1
                visible: root.showOrderbookLayer || root.showBookTickerLayer || !root.showTradesLayer
                implicitWidth: layerStatusText.implicitWidth + 20
                implicitHeight: layerStatusText.implicitHeight + 12

                Label {
                    id: layerStatusText
                    anchors.centerIn: parent
                    text: !root.showTradesLayer
                          ? "Trades hidden"
                          : root.showOrderbookLayer
                            ? "Orderbook + BookTicker"
                            : "BookTicker mode"
                    color: root.mutedTextColor
                    font.pixelSize: 12
                }
            }
        }
    }
}
