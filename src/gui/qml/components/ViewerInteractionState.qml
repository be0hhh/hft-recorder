import QtQuick

QtObject {
    id: state

    property bool interactiveMode: false
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
        state.rangeSelectionActive = false
        state.selectionCommitted = false
        state.selectionStartX = 0
        state.selectionStartY = 0
        state.selectionEndX = 0
        state.selectionEndY = 0
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
        return Math.min(state.selectionStartX, state.selectionEndX)
    }

    function selectionTop() {
        return Math.min(state.selectionStartY, state.selectionEndY)
    }

    function selectionWidth() {
        return Math.abs(state.selectionEndX - state.selectionStartX)
    }

    function selectionHeight() {
        return Math.abs(state.selectionEndY - state.selectionStartY)
    }

    function beginSelection(x, y) {
        state.rangeSelectionActive = true
        state.selectionCommitted = false
        state.selectionStartX = x
        state.selectionStartY = y
        state.selectionEndX = x
        state.selectionEndY = y
    }

    function updateSelection(x, y, plotWidth, plotHeight) {
        state.selectionEndX = Math.max(0, Math.min(plotWidth, x))
        state.selectionEndY = Math.max(0, Math.min(plotHeight, y))
    }

    function commitSelection(chart, plotWidth, plotHeight) {
        state.rangeSelectionActive = false
        state.selectionCommitted = chart.commitSelectionRect(
            plotWidth, plotHeight,
            state.selectionStartX, state.selectionStartY,
            state.selectionEndX, state.selectionEndY)
        if (!state.selectionCommitted)
            state.clearSelectionVisual()
    }

    function startInteractiveMode(timer) {
        state.interactiveMode = true
        timer.restart()
    }

    function stopInteractiveModeSoon(timer) {
        timer.restart()
    }

    function anyHoverableLayerVisible(showTradesLayer, effectiveBookTickerLayer, showOrderbookLayer) {
        return showTradesLayer || effectiveBookTickerLayer || showOrderbookLayer
    }
}
