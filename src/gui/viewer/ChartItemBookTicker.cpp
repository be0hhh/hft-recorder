#include "gui/viewer/ChartItemPaintInternal.hpp"

#include <QPainter>

#include "gui/viewer/ChartController.hpp"
#include "gui/viewer/ChartItem.hpp"
#include "gui/viewer/RenderContext.hpp"
#include "gui/viewer/RenderSnapshot.hpp"
#include "gui/viewer/renderers/BookTickerRenderer.hpp"

namespace hftrec::gui::viewer::detail {

namespace {

void drawBookTicker(QPainter* painter,
                    const RenderSnapshot& snap,
                    double dpr) {
    if (!snap.bookTickerVisible) return;
    if (snap.bookTickerTrace.samples.empty()) return;
    RenderContext ctx{painter, snap, HoverInfo{}, dpr};
    renderers::renderBookTicker(ctx);
}

RenderSnapshot buildInteractiveTickerSnapshot(ChartController& controller,
                                              const ChartItem& item,
                                              qreal width,
                                              qreal height) {
    SnapshotInputs tickerInputs{};
    tickerInputs.tradesVisible = false;
    tickerInputs.liquidationsVisible = false;
    tickerInputs.candlesVisible = false;
    tickerInputs.candles2Visible = false;
    tickerInputs.orderbookVisible = false;
    tickerInputs.bookTickerVisible = item.bookTickerVisible();
    tickerInputs.interactiveMode = true;
    tickerInputs.overlayOnly = item.overlayOnly();
    tickerInputs.exactTradeRendering = false;
    tickerInputs.tradeAmountScale = item.tradeAmountScale();
    tickerInputs.candleWidthPx = item.candleWidthPx();
    tickerInputs.bookOpacityGain = item.bookOpacityGain();
    tickerInputs.bookRenderDetail = item.bookRenderDetail();
    tickerInputs.bookDepthWindowPct = item.bookDepthWindowPct();
    tickerInputs.gpuOrderbookVertices = false;
    if (!tickerInputs.bookTickerVisible) return RenderSnapshot{};
    return controller.buildSnapshot(width, height, tickerInputs);
}

}  // namespace

void paintBookTickerLayer(QPainter* painter,
                          ChartController& controller,
                          const ChartItem& item,
                          const RenderSnapshot& settledSnapshot,
                          qreal width,
                          qreal height,
                          double dpr,
                          bool interactiveMode) {
    if (interactiveMode) {
        auto interactiveTicker = buildInteractiveTickerSnapshot(controller, item, width, height);
        drawBookTicker(painter, interactiveTicker, dpr);
        return;
    }
    drawBookTicker(painter, settledSnapshot, dpr);
}

}  // namespace hftrec::gui::viewer::detail
