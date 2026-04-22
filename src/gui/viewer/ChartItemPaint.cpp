#include "gui/viewer/ChartItem.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include <QImage>
#include <QPainter>
#include <QPen>
#include <QQuickWindow>

#include "gui/viewer/ChartController.hpp"
#include "gui/viewer/ChartItemInternal.hpp"
#include "gui/viewer/ChartItemPaintInternal.hpp"
#include "gui/viewer/ColorScheme.hpp"
#include "gui/viewer/RenderContext.hpp"
#include "gui/viewer/RenderSnapshot.hpp"
#include "gui/viewer/renderers/BookRenderer.hpp"
#include "gui/viewer/renderers/BookTickerRenderer.hpp"
#include "gui/viewer/renderers/OverlayRenderer.hpp"
#include "gui/viewer/renderers/TradeRenderer.hpp"

namespace hftrec::gui::viewer {

namespace {

void paintSnapshotFrame(QPainter* painter,
                        const RenderSnapshot& snap,
                        const HoverInfo& hover,
                        double dpr,
                        bool drawBackground) {
    const QRectF rect{0.0, 0.0, snap.vp.w, snap.vp.h};
    if (drawBackground && !snap.overlayOnly) painter->fillRect(rect, bgColor());

    if (!snap.loaded) {
        painter->setPen(axisTextColor());
        painter->drawText(QRectF{8, 8, snap.vp.w - 16, 24},
                          Qt::AlignLeft | Qt::AlignVCenter,
                          QStringLiteral("Pick a session, then load Trades."));
        return;
    }
    if (snap.vp.tMax <= snap.vp.tMin || snap.vp.pMax <= snap.vp.pMin) return;

    RenderContext ctx{painter, snap, hover, dpr};
    renderers::renderBook(ctx);
    renderers::renderTrades(ctx);
    renderers::renderOverlay(ctx);
}

void paintSnapshotLayers(QPainter* painter,
                         const RenderSnapshot& snap,
                         bool drawBackground,
                         bool drawOrderbook,
                         bool drawBookTicker,
                         bool drawTrades,
                         bool drawOverlay,
                         const HoverInfo& hover,
                         double dpr) {
    RenderSnapshot layerSnap = snap;
    layerSnap.orderbookVisible = drawOrderbook && snap.orderbookVisible;
    layerSnap.bookTickerVisible = drawBookTicker && snap.bookTickerVisible;
    layerSnap.tradesVisible = drawTrades && snap.tradesVisible;
    layerSnap.tradeConnectorsVisible = drawTrades && snap.tradeConnectorsVisible;
    if (drawBackground && !layerSnap.overlayOnly) {
        painter->fillRect(QRectF{0.0, 0.0, layerSnap.vp.w, layerSnap.vp.h}, bgColor());
    }
    if (!layerSnap.loaded) return;
    if (layerSnap.vp.tMax <= layerSnap.vp.tMin || layerSnap.vp.pMax <= layerSnap.vp.pMin) return;

    RenderContext ctx{painter, layerSnap, drawOverlay ? hover : HoverInfo{}, dpr};
    if (layerSnap.orderbookVisible) renderers::renderBook(ctx);
    if (layerSnap.bookTickerVisible) renderers::renderBookTicker(ctx);
    if (layerSnap.tradesVisible) renderers::renderTrades(ctx);
    if (drawOverlay) renderers::renderOverlay(ctx);
}

QRectF sourceRectForViewport(const ViewportMap& cachedVp,
                             qint64 tsMin,
                             qint64 tsMax,
                             qint64 priceMinE8,
                             qint64 priceMaxE8) {
    const qreal x0 = cachedVp.toX(tsMin);
    const qreal x1 = cachedVp.toX(tsMax);
    const qreal y0 = cachedVp.toY(priceMaxE8);
    const qreal y1 = cachedVp.toY(priceMinE8);
    return QRectF{
        std::min(x0, x1),
        std::min(y0, y1),
        std::abs(x1 - x0),
        std::abs(y1 - y0),
    };
}

RenderSnapshot baseSnapshotForCache(const RenderSnapshot& snap) {
    return snap;
}

int nextTradeOrigIndex(const RenderSnapshot& snap) noexcept {
    if (snap.tradeDots.empty()) return 0;
    if (snap.tradeDots.back().origIndex >= std::numeric_limits<int>::max()) {
        return std::numeric_limits<int>::max();
    }
    return snap.tradeDots.back().origIndex + 1;
}

std::int64_t maxTradeTs(const RenderSnapshot& snap) noexcept {
    return snap.tradeDots.empty() ? 0 : snap.tradeDots.back().tsNs;
}

std::int64_t maxOrderbookTs(const RenderSnapshot& snap) noexcept {
    return snap.bookSegments.empty() ? 0 : snap.bookSegments.back().tsEndNs;
}

std::int64_t maxBookTickerTs(const RenderSnapshot& snap) noexcept {
    return snap.bookTickerTrace.samples.empty() ? 0 : snap.bookTickerTrace.samples.back().tsNs;
}

std::int64_t liveHoldNsForViewport(const ViewportMap& vp) noexcept {
    const auto widthPx = std::max<std::int64_t>(1, static_cast<std::int64_t>(std::ceil(vp.w)));
    const auto pixelSpanNs = std::max<std::int64_t>(1, (vp.tMax - vp.tMin + widthPx - 1) / widthPx);
    return std::max<std::int64_t>(pixelSpanNs, pixelSpanNs * 12ll);
}

struct LiveBookTickerPixelState {
    bool has{false};
    std::int64_t firstPriceE8{0};
    std::int64_t lastPriceE8{0};
    std::int64_t minPriceE8{0};
    std::int64_t maxPriceE8{0};
    std::int64_t lastQtyE8{0};
    std::int64_t lastTsNs{0};

    void absorb(std::int64_t priceE8, std::int64_t qtyE8, std::int64_t tsNs) noexcept {
        if (priceE8 <= 0) return;
        if (!has) {
            has = true;
            firstPriceE8 = priceE8;
            minPriceE8 = priceE8;
            maxPriceE8 = priceE8;
        } else {
            minPriceE8 = std::min(minPriceE8, priceE8);
            maxPriceE8 = std::max(maxPriceE8, priceE8);
        }
        lastPriceE8 = priceE8;
        lastQtyE8 = qtyE8;
        lastTsNs = tsNs;
    }
};

bool visiblyDifferent(qreal lhs, qreal rhs) noexcept {
    return std::abs(lhs - rhs) >= 0.5;
}

void appendLiveBookTickerSideLines(std::vector<BookTickerLine>& out,
                                   const std::vector<LiveBookTickerPixelState>& pixels,
                                   const ViewportMap& vp) {
    bool prevHas = false;
    int prevPx = -1;
    qreal prevLastY = 0.0;

    for (int px = 0; px < static_cast<int>(pixels.size()); ++px) {
        const auto& state = pixels[static_cast<std::size_t>(px)];
        if (!state.has) {
            prevHas = false;
            prevPx = -1;
            continue;
        }

        const qreal x0 = static_cast<qreal>(px);
        const qreal x1 = std::min<qreal>(x0 + 1.0, vp.w);
        const qreal firstY = vp.toY(state.firstPriceE8);
        const qreal lastY = vp.toY(state.lastPriceE8);

        if (prevHas && prevPx + 1 == px && visiblyDifferent(prevLastY, firstY)) {
            out.push_back(BookTickerLine{x0, prevLastY, x0, firstY});
        }

        if (state.minPriceE8 != state.maxPriceE8) {
            const qreal y0 = vp.toY(state.minPriceE8);
            const qreal y1 = vp.toY(state.maxPriceE8);
            if (visiblyDifferent(y0, y1)) out.push_back(BookTickerLine{x0, y0, x0, y1});
        }

        if (x1 > x0) out.push_back(BookTickerLine{x0, lastY, x1, lastY});

        prevHas = true;
        prevPx = px;
        prevLastY = lastY;
    }
}

void absorbLiveBookTickerInterval(std::vector<LiveBookTickerPixelState>& bidPixels,
                                  std::vector<LiveBookTickerPixelState>& askPixels,
                                  const ViewportMap& vp,
                                  const hftrec::replay::BookTickerRow& ticker,
                                  std::int64_t tsStart,
                                  std::int64_t tsEnd) {
    if (tsEnd <= tsStart || bidPixels.empty() || askPixels.empty()) return;

    const qreal xLeft = std::clamp(vp.toX(tsStart), 0.0, vp.w);
    const qreal xRight = std::clamp(vp.toX(tsEnd), 0.0, vp.w);
    if (xRight <= xLeft) return;

    const int maxPx = static_cast<int>(bidPixels.size()) - 1;
    const int startPx = std::clamp(static_cast<int>(std::floor(xLeft)), 0, maxPx);
    const int endPx = std::clamp(static_cast<int>(std::ceil(xRight)) - 1, 0, maxPx);
    if (endPx < startPx) return;

    for (int px = startPx; px <= endPx; ++px) {
        bidPixels[static_cast<std::size_t>(px)].absorb(ticker.bidPriceE8, ticker.bidQtyE8, ticker.tsNs);
        askPixels[static_cast<std::size_t>(px)].absorb(ticker.askPriceE8, ticker.askQtyE8, ticker.tsNs);
    }
}

void buildLiveBookTickerTrace(BookTickerTrace& trace,
                              const ViewportMap& vp,
                              const std::vector<const hftrec::replay::BookTickerRow*>& rows) {
    trace = BookTickerTrace{};
    if (rows.empty() || vp.w <= 0.0) return;

    constexpr std::int64_t kBookTickerStaleGapNs = 1'000'000'000ll;
    const int widthPx = std::max(1, static_cast<int>(std::ceil(vp.w)));
    const std::int64_t pixelSpanNs = std::max<std::int64_t>(
        1,
        (vp.tMax - vp.tMin + static_cast<std::int64_t>(widthPx) - 1) / static_cast<std::int64_t>(widthPx));
    std::vector<LiveBookTickerPixelState> bidPixels(static_cast<std::size_t>(widthPx));
    std::vector<LiveBookTickerPixelState> askPixels(static_cast<std::size_t>(widthPx));

    for (std::size_t i = 0; i < rows.size(); ++i) {
        const auto& row = *rows[i];
        if (row.tsNs > vp.tMax) break;

        const bool hasNext = (i + 1u < rows.size());
        if (!hasNext && row.tsNs < vp.tMin) break;

        const std::int64_t tsStart = hasNext ? std::max<std::int64_t>(vp.tMin, row.tsNs) : row.tsNs;
        std::int64_t nextTs = hasNext ? rows[i + 1u]->tsNs : (row.tsNs + pixelSpanNs);
        if (hasNext && nextTs - row.tsNs > kBookTickerStaleGapNs) nextTs = row.tsNs + pixelSpanNs;
        const std::int64_t tsEnd = std::min<std::int64_t>(vp.tMax, nextTs);
        absorbLiveBookTickerInterval(bidPixels, askPixels, vp, row, tsStart, tsEnd);
        if (!hasNext || nextTs >= vp.tMax) break;
    }

    trace.bidLines.reserve(static_cast<std::size_t>(widthPx) * 2u);
    trace.askLines.reserve(static_cast<std::size_t>(widthPx) * 2u);
    trace.samples.reserve(static_cast<std::size_t>(widthPx));
    appendLiveBookTickerSideLines(trace.bidLines, bidPixels, vp);
    appendLiveBookTickerSideLines(trace.askLines, askPixels, vp);

    for (int px = 0; px < widthPx; ++px) {
        const auto& bid = bidPixels[static_cast<std::size_t>(px)];
        const auto& ask = askPixels[static_cast<std::size_t>(px)];
        if (!bid.has && !ask.has) continue;
        trace.samples.push_back(BookTickerSample{
            px,
            std::max(bid.lastTsNs, ask.lastTsNs),
            bid.has ? bid.lastPriceE8 : 0,
            bid.has ? bid.lastQtyE8 : 0,
            ask.has ? ask.lastPriceE8 : 0,
            ask.has ? ask.lastQtyE8 : 0,
        });
    }
}

RenderSnapshot liveSnapshotFromDataBatch(const RenderSnapshot& base,
                                         const LiveDataBatch& batch,
                                         const hftrec::replay::BookTickerRow* bookTickerCarry,
                                         int tradeOrigIndexStart) {
    RenderSnapshot live = base;
    live.bookSegments.clear();
    live.bookTickerTrace = BookTickerTrace{};
    live.tradeDots.clear();
    live.gpuBookVertices.clear();
    live.tradeConnectorsVisible = true;

    if (!live.loaded || live.vp.tMax <= live.vp.tMin || live.vp.pMax <= live.vp.pMin) return live;

    int tradeOrigIndex = tradeOrigIndexStart;
    for (const auto& row : batch.trades) {
        const int rowOrigIndex = tradeOrigIndex;
        if (tradeOrigIndex < std::numeric_limits<int>::max()) ++tradeOrigIndex;
        if (row.tsNs < live.vp.tMin || row.tsNs > live.vp.tMax) continue;
        if (row.priceE8 < live.vp.pMin || row.priceE8 > live.vp.pMax) continue;
        live.tradeDots.push_back(TradeDot{row.tsNs, row.priceE8, row.qtyE8, row.sideBuy != 0, rowOrigIndex});
    }

    if (live.bookTickerVisible && !batch.bookTickers.empty()) {
        std::vector<const hftrec::replay::BookTickerRow*> rows;
        rows.reserve(batch.bookTickers.size() + 1u);
        if (bookTickerCarry != nullptr && bookTickerCarry->tsNs < batch.bookTickers.front().tsNs) {
            rows.push_back(bookTickerCarry);
        }
        for (const auto& row : batch.bookTickers) rows.push_back(&row);
        buildLiveBookTickerTrace(live.bookTickerTrace, live.vp, rows);
    }

    if (live.orderbookVisible && !batch.depths.empty()) {
        const std::int64_t holdNs = liveHoldNsForViewport(live.vp);
        live.bookSegments.reserve(batch.depths.size());
        for (std::size_t i = 0; i < batch.depths.size(); ++i) {
            const auto& row = batch.depths[i];
            if (row.tsNs > live.vp.tMax) break;
            if (row.tsNs < live.vp.tMin) continue;
            const std::int64_t nextTs = (i + 1u < batch.depths.size()) ? batch.depths[i + 1u].tsNs : row.tsNs + holdNs;
            BookSegment seg{};
            seg.tsStartNs = row.tsNs;
            seg.tsEndNs = std::min<std::int64_t>(live.vp.tMax, std::max<std::int64_t>(row.tsNs + 1, nextTs));
            std::int64_t maxBid = 0;
            std::int64_t maxAsk = 0;
            for (const auto& level : row.bids) {
                if (level.qtyE8 <= 0) continue;
                seg.bids.push_back(BookLevel{level.priceE8, level.qtyE8});
                if (level.qtyE8 > maxBid) maxBid = level.qtyE8;
            }
            for (const auto& level : row.asks) {
                if (level.qtyE8 <= 0) continue;
                seg.asks.push_back(BookLevel{level.priceE8, level.qtyE8});
                if (level.qtyE8 > maxAsk) maxAsk = level.qtyE8;
            }
            if (seg.bids.empty() && seg.asks.empty()) continue;
            seg.maxBidQty = std::max<std::int64_t>(maxBid, 1);
            seg.maxAskQty = std::max<std::int64_t>(maxAsk, 1);
            live.bookSegments.push_back(std::move(seg));
        }
    }

    return live;
}

bool hasLiveDataRows(const LiveDataBatch& batch) noexcept {
    return !batch.trades.empty() || !batch.bookTickers.empty() || !batch.depths.empty();
}

LiveDataBatch liveDataBaseBatch(const LiveDataCache& cache) {
    LiveDataBatch batch = cache.stableRows;
    batch.id = cache.version;
    return batch;
}

const hftrec::replay::BookTickerRow* liveDataCarryForLastBatch(const LiveDataCache& cache) noexcept {
    return cache.stableRows.bookTickers.empty() ? nullptr : &cache.stableRows.bookTickers.back();
}

void appendSnapshotRows(RenderSnapshot& target, RenderSnapshot&& rows) {
    target.bookSegments.insert(
        target.bookSegments.end(),
        std::make_move_iterator(rows.bookSegments.begin()),
        std::make_move_iterator(rows.bookSegments.end()));
    target.bookTickerTrace.bidLines.insert(
        target.bookTickerTrace.bidLines.end(),
        std::make_move_iterator(rows.bookTickerTrace.bidLines.begin()),
        std::make_move_iterator(rows.bookTickerTrace.bidLines.end()));
    target.bookTickerTrace.askLines.insert(
        target.bookTickerTrace.askLines.end(),
        std::make_move_iterator(rows.bookTickerTrace.askLines.begin()),
        std::make_move_iterator(rows.bookTickerTrace.askLines.end()));
    target.bookTickerTrace.samples.insert(
        target.bookTickerTrace.samples.end(),
        std::make_move_iterator(rows.bookTickerTrace.samples.begin()),
        std::make_move_iterator(rows.bookTickerTrace.samples.end()));
    target.tradeDots.insert(
        target.tradeDots.end(),
        std::make_move_iterator(rows.tradeDots.begin()),
        std::make_move_iterator(rows.tradeDots.end()));
}

RenderSnapshot baseSnapshotWithLiveDataCache(const RenderSnapshot& snap,
                                             const LiveDataCache& cache) {
    RenderSnapshot base = baseSnapshotForCache(snap);
    auto batch = liveDataBaseBatch(cache);
    if (hasLiveDataRows(batch)) {
        appendSnapshotRows(base, liveSnapshotFromDataBatch(base, batch, nullptr, nextTradeOrigIndex(base)));
    }
    return base;
}

void drawTradeBridge(QPainter* painter, const RenderSnapshot& base, const RenderSnapshot& live) {
    if (!base.tradesVisible || base.tradeDots.empty() || live.tradeDots.empty()) return;
    const auto& prev = base.tradeDots.back();
    const auto& last = live.tradeDots.front();
    if (prev.origIndex + 1 != last.origIndex) return;

    const QPointF p0{base.vp.toX(prev.tsNs), base.vp.toY(prev.priceE8)};
    const QPointF p1{base.vp.toX(last.tsNs), base.vp.toY(last.priceE8)};
    if (p0 == p1) return;

    painter->save();
    QPen pen(tradeConnectorColor());
    pen.setWidth(1);
    pen.setCapStyle(Qt::SquareCap);
    painter->setPen(pen);
    painter->drawLine(p0, p1);
    painter->restore();
}

void paintLiveSnapshot(QPainter* painter,
                       const RenderSnapshot& base,
                       const RenderSnapshot& live,
                       double dpr) {
    if (!live.loaded) return;
    RenderContext ctx{painter, live, HoverInfo{}, dpr};
    renderers::renderBook(ctx);
    renderers::renderBookTicker(ctx);
    drawTradeBridge(painter, base, live);
    renderers::renderTrades(ctx);
}

}  // namespace

void ChartItem::requestRepaint() {
    if (interactiveMode_ && cachedExactSnap_) {
        cachedInteractiveSnap_.reset();
        interactiveDirty_ = true;
    } else {
        invalidateSnapshotCache_();
        invalidateBaseImage_();
        interactiveDirty_ = false;
        exactDirty_ = false;
    }
    if (hoverActive_ && !interactiveMode_) updateHover_();
    update();
}

void ChartItem::requestLiveRepaint() {
    invalidateBaseImage_();
    cachedLiveSnap_.reset();
    update();
}

void ChartItem::geometryChange(const QRectF& newGeometry, const QRectF& oldGeometry) {
    QQuickPaintedItem::geometryChange(newGeometry, oldGeometry);
    if (newGeometry.size() != oldGeometry.size()) {
        invalidateSnapshotCache_();
        invalidateBaseImage_();
        interactiveDirty_ = false;
        exactDirty_ = false;
    }
}

void ChartItem::invalidateSnapshotCache_() {
    cachedInteractiveSnap_.reset();
    cachedExactSnap_.reset();
    interactiveDirty_ = false;
    exactDirty_ = false;
}

void ChartItem::invalidateBaseImage_() {
    cachedOrderbookImage_ = QImage{};
    cachedBookTickerImage_ = QImage{};
    cachedTradesImage_ = QImage{};
    cachedLayerImageW_ = 0.0;
    cachedLayerImageH_ = 0.0;
    cachedOrderbookEndTsNs_ = 0;
    cachedBookTickerEndTsNs_ = 0;
    cachedTradesEndTsNs_ = 0;
    cachedLiveDataBatchId_ = 0;
    cachedLiveSnap_.reset();
}

std::unique_ptr<RenderSnapshot>& ChartItem::activeSnapshotCache_() noexcept {
    return interactiveMode_ ? cachedInteractiveSnap_ : cachedExactSnap_;
}

void ChartItem::mergeLiveSnapshotIntoBaseImage_() {
    if (!cachedLiveSnap_ || cachedLiveSnap_->overlayOnly) return;
    if (cachedLayerImageW_ <= 0.0 || cachedLayerImageH_ <= 0.0) return;

    if (!cachedOrderbookImage_.isNull()) {
        QPainter painter(&cachedOrderbookImage_);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        RenderSnapshot liveBook = *cachedLiveSnap_;
        liveBook.tradesVisible = false;
        liveBook.bookTickerVisible = false;
        RenderContext ctx{&painter, liveBook, HoverInfo{}, 1.0};
        renderers::renderBook(ctx);
        painter.end();
    }

    if (!cachedBookTickerImage_.isNull()) {
        QPainter painter(&cachedBookTickerImage_);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        RenderSnapshot liveTicker = *cachedLiveSnap_;
        liveTicker.tradesVisible = false;
        liveTicker.orderbookVisible = false;
        RenderContext ctx{&painter, liveTicker, HoverInfo{}, 1.0};
        renderers::renderBookTicker(ctx);
        painter.end();
    }

    if (!cachedTradesImage_.isNull()) {
        QPainter painter(&cachedTradesImage_);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        RenderSnapshot liveTrades = *cachedLiveSnap_;
        liveTrades.orderbookVisible = false;
        liveTrades.bookTickerVisible = false;
        const RenderSnapshot base = (cachedExactSnap_ && controller_ != nullptr)
            ? baseSnapshotWithLiveDataCache(*cachedExactSnap_, controller_->liveDataCache())
            : RenderSnapshot{};
        drawTradeBridge(&painter, base, liveTrades);
        RenderContext ctx{&painter, liveTrades, HoverInfo{}, 1.0};
        renderers::renderTrades(ctx);
        painter.end();
    }
    cachedOrderbookEndTsNs_ = std::max(cachedOrderbookEndTsNs_, maxOrderbookTs(*cachedLiveSnap_));
    cachedBookTickerEndTsNs_ = std::max(cachedBookTickerEndTsNs_, maxBookTickerTs(*cachedLiveSnap_));
    cachedTradesEndTsNs_ = std::max(cachedTradesEndTsNs_, maxTradeTs(*cachedLiveSnap_));
}

const RenderSnapshot& ChartItem::ensureSnapshot_() {
    const qreal w = width();
    const qreal h = height();
    auto& activeCache = activeSnapshotCache_();
    bool& activeDirty = interactiveMode_ ? interactiveDirty_ : exactDirty_;
    const bool sizeChanged = (cachedW_ != w || cachedH_ != h);
    const bool rebuildActive = activeDirty || !activeCache || sizeChanged;

    if (rebuildActive) {
        activeCache = std::make_unique<RenderSnapshot>(controller_->buildSnapshot(w, h, detail::collectInputs(*this)));
        cachedW_ = w;
        cachedH_ = h;
        activeDirty = false;
    }
    return *activeCache;
}

void ChartItem::ensureLayerImages_(const RenderSnapshot& snap, qreal w, qreal h) {
    if (overlayOnly_) return;
    if (!snap.loaded) return;
    const bool sizeMatches = (cachedLayerImageW_ == w && cachedLayerImageH_ == h);
    if (!sizeMatches) {
        cachedOrderbookImage_ = QImage{};
        cachedBookTickerImage_ = QImage{};
        cachedTradesImage_ = QImage{};
        cachedOrderbookEndTsNs_ = 0;
        cachedBookTickerEndTsNs_ = 0;
        cachedTradesEndTsNs_ = 0;
    }
    if (!cachedOrderbookImage_.isNull() && !cachedBookTickerImage_.isNull() && !cachedTradesImage_.isNull()) return;

    const auto& liveCache = controller_->liveDataCache();
    const RenderSnapshot baseSnap = baseSnapshotWithLiveDataCache(snap, liveCache);
    const std::int64_t baseOrderbookEndTsNs = maxOrderbookTs(baseSnap);
    const std::int64_t baseBookTickerEndTsNs = maxBookTickerTs(baseSnap);
    const std::int64_t baseTradesEndTsNs = maxTradeTs(baseSnap);

    const int imageW = std::max(1, static_cast<int>(std::ceil(w)));
    const int imageH = std::max(1, static_cast<int>(std::ceil(h)));

    if (cachedOrderbookImage_.isNull()) {
        QImage orderbookImage(imageW, imageH, QImage::Format_ARGB32_Premultiplied);
        orderbookImage.fill(bgColor().rgba());
        QPainter painter(&orderbookImage);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        paintSnapshotLayers(&painter, baseSnap, false, true, false, false, false, HoverInfo{}, 1.0);
        painter.end();
        cachedOrderbookImage_ = std::move(orderbookImage);
    }

    if (cachedBookTickerImage_.isNull()) {
        QImage bookTickerImage(imageW, imageH, QImage::Format_ARGB32_Premultiplied);
        bookTickerImage.fill(Qt::transparent);
        QPainter painter(&bookTickerImage);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        paintSnapshotLayers(&painter, baseSnap, false, false, true, false, false, HoverInfo{}, 1.0);
        painter.end();
        cachedBookTickerImage_ = std::move(bookTickerImage);
    }

    if (cachedTradesImage_.isNull()) {
        QImage tradesImage(imageW, imageH, QImage::Format_ARGB32_Premultiplied);
        tradesImage.fill(Qt::transparent);
        QPainter painter(&tradesImage);
        painter.setRenderHint(QPainter::Antialiasing, false);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        RenderSnapshot cachedTradesSnap = baseSnap;
        cachedTradesSnap.tradeConnectorsVisible = true;
        paintSnapshotLayers(&painter, cachedTradesSnap, false, false, false, true, false, HoverInfo{}, 1.0);
        painter.end();
        cachedTradesImage_ = std::move(tradesImage);
    }

    cachedLayerImageW_ = w;
    cachedLayerImageH_ = h;
    cachedOrderbookEndTsNs_ = std::max(cachedOrderbookEndTsNs_, baseOrderbookEndTsNs);
    cachedBookTickerEndTsNs_ = std::max(cachedBookTickerEndTsNs_, baseBookTickerEndTsNs);
    cachedTradesEndTsNs_ = std::max(cachedTradesEndTsNs_, baseTradesEndTsNs);
}

void ChartItem::paint(QPainter* painter) {
    painter->setRenderHint(QPainter::Antialiasing, false);
    painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter->setRenderHint(QPainter::TextAntialiasing, true);

    const QRectF rect = boundingRect();
    if (!overlayOnly_) painter->fillRect(rect, bgColor());

    if (!controller_ || width() <= 0 || height() <= 0) return;

    const double dpr = window() ? window()->effectiveDevicePixelRatio() : 1.0;
    const qreal w = width();
    const qreal h = height();

    const RenderSnapshot& snap = ensureSnapshot_();
    if (!snap.loaded) {
        paintSnapshotFrame(painter, snap, detail::buildHoverInfo(*this), dpr, false);
        return;
    }

    const RenderSnapshot& layerSnap = (interactiveMode_ && cachedExactSnap_) ? *cachedExactSnap_ : snap;
    controller_->refreshLiveDataWindow(snap.vp.tMin, snap.vp.tMax);
    ensureLayerImages_(layerSnap, w, h);
    const auto& liveCache = controller_->liveDataCache();
    const RenderSnapshot baseSnap = baseSnapshotWithLiveDataCache(snap, liveCache);
    const auto& liveBatch = liveCache.overlayRows;
    if (!cachedLiveSnap_ || cachedLiveDataBatchId_ != liveBatch.id) {
        const auto* carry = liveDataCarryForLastBatch(liveCache);
        const int liveTradeOrigIndexStart = nextTradeOrigIndex(baseSnap);
        cachedLiveSnap_ = std::make_unique<RenderSnapshot>(
            liveSnapshotFromDataBatch(snap, liveBatch, carry, liveTradeOrigIndexStart));
        cachedLiveDataBatchId_ = liveBatch.id;
    }

    if (interactiveMode_ && !cachedOrderbookImage_.isNull() && !cachedBookTickerImage_.isNull()
        && !cachedTradesImage_.isNull() && cachedExactSnap_) {
        const QRectF sourceRect = sourceRectForViewport(
            cachedExactSnap_->vp,
            controller_->tsMin(),
            controller_->tsMax(),
            controller_->priceMinE8(),
            controller_->priceMaxE8());
        const QRectF fullSource{
            0.0,
            0.0,
            static_cast<qreal>(cachedOrderbookImage_.width()),
            static_cast<qreal>(cachedOrderbookImage_.height())
        };
        const QRectF clippedSource = sourceRect.intersected(fullSource);
        if (sourceRect.width() > 0.5 && sourceRect.height() > 0.5
            && clippedSource.width() > 0.5 && clippedSource.height() > 0.5) {
            const QRectF destRect{
                rect.left() + ((clippedSource.left() - sourceRect.left()) / sourceRect.width()) * rect.width(),
                rect.top() + ((clippedSource.top() - sourceRect.top()) / sourceRect.height()) * rect.height(),
                (clippedSource.width() / sourceRect.width()) * rect.width(),
                (clippedSource.height() / sourceRect.height()) * rect.height(),
            };
            painter->drawImage(destRect, cachedOrderbookImage_, clippedSource);
            painter->drawImage(destRect, cachedBookTickerImage_, clippedSource);
            painter->drawImage(destRect, cachedTradesImage_, clippedSource);
            paintLiveSnapshot(painter, baseSnap, *cachedLiveSnap_, dpr);
            return;
        }
    }

    if (!cachedOrderbookImage_.isNull() && !overlayOnly_) {
        painter->drawImage(rect, cachedOrderbookImage_);
    }
    if (!cachedBookTickerImage_.isNull()) {
        painter->drawImage(rect, cachedBookTickerImage_);
    }
    if (!cachedTradesImage_.isNull()) {
        painter->drawImage(rect, cachedTradesImage_);
    }
    paintLiveSnapshot(painter, baseSnap, *cachedLiveSnap_, dpr);

    if (!interactiveMode_) {
        RenderContext ctx{painter, snap, detail::buildHoverInfo(*this), dpr};
        renderers::renderOverlay(ctx);
    }
}

}  // namespace hftrec::gui::viewer




