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
        const minUsd = 1000.0
        const maxUsd = 1000000.0
        return minUsd + (maxUsd - minUsd) * clamped
    }

    function usdValueToSlider(usdValue) {
        const minUsd = 1000.0
        const maxUsd = 1000000.0
        const clamped = Math.max(minUsd, Math.min(maxUsd, usdValue))
        return (clamped - minUsd) / (maxUsd - minUsd)
    }

    function formatUsdShort(usdValue) {
        if (usdValue >= 1000000)
            return "$" + (usdValue / 1000000).toFixed(usdValue >= 10000000 ? 0 : 1).replace(/\.0$/, "") + "m"
        if (usdValue >= 1000)
            return "$" + (usdValue / 1000).toFixed(usdValue >= 10000 ? 0 : 1).replace(/\.0$/, "") + "k"
        if (usdValue >= 100)
            return "$" + Math.round(usdValue)
        if (usdValue >= 10)
            return "$" + usdValue.toFixed(1).replace(/\.0$/, "")
        return "$" + usdValue.toFixed(2).replace(/0+$/, "").replace(/\.$/, "")
    }

    function formatUsdInput(usdValue) {
        return String(Math.round(Math.max(1000, Math.min(1000000, usdValue))))
    }

    function parseUsdInput(text, fallback) {
        const value = Number(String(text).replace(/[$,\s]/g, ""))
        if (!isFinite(value))
            return fallback
        return Math.max(1000, Math.min(1000000, value))
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
