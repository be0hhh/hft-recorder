#pragma once

#include <cstdint>
#include <vector>

#include <QPointF>
#include <QString>

#include "gui/viewer/StrategyOverlay.hpp"

#include "core/replay/EventRows.hpp"
#include "gui/viewer/ViewportMap.hpp"

namespace hftrec::gui::viewer {

struct BookLevel {
    std::int64_t priceE8{0};
    std::int64_t qtyE8{0};
    std::uint8_t alpha{255};
};

struct GpuBookLineVertex {
    float x{0.0F};
    float y{0.0F};
    std::uint8_t r{0};
    std::uint8_t g{0};
    std::uint8_t b{0};
    std::uint8_t a{0};
};

struct BookTickerLine {
    qreal x0{0.0};
    qreal y0{0.0};
    qreal x1{0.0};
    qreal y1{0.0};
};

struct BookTickerSample {
    int xPx{0};
    std::int64_t tsNs{0};
    std::int64_t bidPriceE8{0};
    std::int64_t bidQtyE8{0};
    std::int64_t askPriceE8{0};
    std::int64_t askQtyE8{0};
};

struct BookTickerTrace {
    std::vector<BookTickerLine> bidLines;
    std::vector<BookTickerLine> askLines;
    std::vector<BookTickerSample> samples;
};

// One time-slice of the chart: constant book state covering [tsStartNs,
// tsEndNs]. Materialized by ChartController::buildSnapshot so the paint path
// never mutates replay state.
struct BookSegment {
    std::int64_t tsStartNs{0};
    std::int64_t tsEndNs{0};
    std::vector<BookLevel> bids;    // best-first (sorted high -> low)
    std::vector<BookLevel> asks;    // best-first (sorted low  -> high)
    std::int64_t maxBidQty{1};
    std::int64_t maxAskQty{1};
};

struct TradeGroupEntry {
    std::int64_t priceE8{0};
    std::int64_t qtyE8{0};
    std::int64_t amountE8{0};
    bool         sideBuy{true};
    int          origIndex{-1};
};

struct TradeDot {
    std::int64_t tsNs{0};
    std::int64_t priceE8{0};
    std::int64_t qtyE8{0};
    bool         sideBuy{true};
    int          origIndex{-1};  // index into SessionReplay::trades()
    int          firstOrigIndex{-1};
    int          lastOrigIndex{-1};
    std::int64_t totalQtyE8{0};
    std::int64_t totalAmountE8{0};
    std::int64_t tsStartNs{0};
    std::int64_t tsEndNs{0};
    bool         aggregated{false};
    std::int64_t tradeCount{0};
    std::int64_t buyQtyE8{0};
    std::int64_t sellQtyE8{0};
    std::int64_t buyAmountE8{0};
    std::int64_t sellAmountE8{0};
    std::int64_t representativePriceE8{0};
    std::vector<TradeGroupEntry> groupEntries;
};

struct CandleRect {
    std::int64_t tier{0};
    std::int64_t tsNs{0};
    std::int64_t highE8{0};
    std::int64_t lowE8{0};
    std::int64_t quoteAmountE8{0};
    qreal x{0.0};
    qreal y{0.0};
    qreal w{0.0};
    qreal h{0.0};
    bool up{true};
};

struct LiquidationDot {
    std::int64_t tsNs{0};
    std::int64_t priceE8{0};
    std::int64_t qtyE8{0};
    std::int64_t avgPriceE8{0};
    std::int64_t filledQtyE8{0};
    bool         sideBuy{true};
    int          origIndex{-1};
};

struct VerticalMarker {
    std::int64_t tsNs{0};
    QString label{};
};

struct HoverInfo {
    bool    active{false};
    bool    contextActive{false};
    QPointF point{};

    // Trade hit-test result.
    bool         tradeHit{false};
    int          tradeOrigIndex{-1};
    std::int64_t tradeTsNs{0};
    std::int64_t tradePriceE8{0};
    std::int64_t tradeQtyE8{0};
    std::int64_t tradeTotalQtyE8{0};
    std::int64_t tradeTotalAmountE8{0};
    std::int64_t tradeTsStartNs{0};
    std::int64_t tradeTsEndNs{0};
    std::int64_t tradeCount{0};
    std::int64_t tradeBuyQtyE8{0};
    std::int64_t tradeSellQtyE8{0};
    std::int64_t tradeBuyAmountE8{0};
    std::int64_t tradeSellAmountE8{0};
    std::int64_t tradeRepresentativePriceE8{0};
    bool         tradeAggregated{false};
    bool         tradeSideBuy{true};
    std::vector<TradeGroupEntry> tradeGroupEntries;

    // Liquidation hit-test result.
    bool         liquidationHit{false};
    int          liquidationOrigIndex{-1};
    std::int64_t liquidationTsNs{0};
    std::int64_t liquidationPriceE8{0};
    std::int64_t liquidationQtyE8{0};
    std::int64_t liquidationAvgPriceE8{0};
    std::int64_t liquidationFilledQtyE8{0};
    bool         liquidationSideBuy{true};

    // Strategy/backtest fill hit-test result.
    bool         strategyFillHit{false};
    std::int64_t strategyFillTsNs{0};
    std::int64_t strategyFillPriceE8{0};
    std::int64_t strategyFillQtyE8{0};
    std::int64_t strategyFillAmountE8{0};
    bool         strategyFillSideBuy{true};
    bool         strategyFillReduceOnly{false};

    // Book hit-test result. 0 none, 1 bid ticker, 2 ask ticker, 3 bid book, 4 ask book.
    int          bookKind{0};
    std::int64_t bookPriceE8{0};
    std::int64_t bookQtyE8{0};
    std::int64_t bookTsNs{0};
    std::int64_t bookTsStartNs{0};
    std::int64_t bookTsEndNs{0};
};

// Everything renderers need to draw one frame. Plain POD; owned by whoever
// built it. Renderers are stateless and only read from it.
struct RenderSnapshot {
    ViewportMap vp{};
    bool        loaded{false};
    bool        tradeDecimated{false};
    bool        exactTradeRendering{false};

    // Visibility + tuning knobs (snapshot of ChartItem state at build time).
    bool  tradesVisible{true};
    bool  liquidationsVisible{true};
    bool  candlesVisible{false};
    bool  tradeConnectorsVisible{false};
    bool  orderbookVisible{false};
    bool  bookTickerVisible{false};
    bool  markPriceVisible{false};
    bool  indexPriceVisible{false};
    bool  fundingVisible{false};
    bool  priceLimitVisible{false};
    bool  interactiveMode{false};
    bool  overlayOnly{false};
    qreal tradeAmountScale{0.45};
    qreal candleWidthPx{10.0};
    qreal bookOpacityGain{15000.0};
    qreal bookRenderDetail{5000.0};
    qreal bookDepthWindowPct{5.0};

    // Orderbook history frames and the separately prepared continuous bookTicker trace.
    std::vector<BookSegment> bookSegments;
    BookTickerTrace bookTickerTrace;
    std::vector<GpuBookLineVertex> gpuBookVertices;

    // Trades pre-filtered to viewport (original order by tsNs).
    std::vector<TradeDot> tradeDots;
    std::vector<CandleRect> candleRects;
    std::vector<LiquidationDot> liquidationDots;
    std::vector<hftrec::replay::MarkPriceRow> markPrices;
    std::vector<hftrec::replay::IndexPriceRow> indexPrices;
    std::vector<hftrec::replay::FundingRow> fundings;
    std::vector<hftrec::replay::PriceLimitRow> priceLimits;
    std::vector<StrategyOrderSegment> strategyOrderSegments;
    std::vector<StrategyFillMarker> strategyFillMarkers;

    // API-injected vertical markers for the active chart.
    std::vector<VerticalMarker> verticalMarkers;

    HoverInfo hover{};
};

// Inputs for buildSnapshot — mirror of the visibility/tuning knobs owned by
// ChartItem. Kept as a struct so the signature doesn't drift when knobs change.
struct SnapshotInputs {
    bool  tradesVisible{true};
    bool  liquidationsVisible{true};
    bool  candlesVisible{false};
    bool  orderbookVisible{false};
    bool  bookTickerVisible{false};
    bool  interactiveMode{false};
    bool  overlayOnly{false};
    bool  exactTradeRendering{false};
    qreal tradeAmountScale{0.45};
    qreal candleWidthPx{10.0};
    qreal bookOpacityGain{15000.0};
    qreal bookRenderDetail{5000.0};
    qreal bookDepthWindowPct{5.0};
    bool  gpuOrderbookVertices{false};
    bool  markPriceVisible{false};
    bool  indexPriceVisible{false};
    bool  fundingVisible{false};
    bool  priceLimitVisible{false};
};

}  // namespace hftrec::gui::viewer


