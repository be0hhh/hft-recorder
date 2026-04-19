#include "gui/viewer/ChartController.hpp"

#include <QDateTime>
#include <QByteArray>
#include <algorithm>
#include <filesystem>

#include "gui/viewer/detail/BookMath.hpp"

namespace hftrec::gui::viewer {

namespace {

constexpr std::int64_t kOneCentE8 = 1000000ll;
constexpr std::int64_t kOneMsNs = 1000000ll;

std::int64_t ceilToStep(std::int64_t value, std::int64_t step) {
    if (step <= 0) return value;
    if (value >= 0) return ((value + step - 1) / step) * step;
    return (value / step) * step;
}

std::int64_t floorToStep(std::int64_t value, std::int64_t step) {
    if (step <= 0) return value;
    if (value >= 0) return (value / step) * step;
    return ((value - step + 1) / step) * step;
}

std::int64_t nicePriceStepE8(std::int64_t spanE8, int tickCount) {
    const auto safeTicks = std::max(2, tickCount);
    std::int64_t rawStep = spanE8 / static_cast<std::int64_t>(safeTicks - 1);
    rawStep = std::max(rawStep, kOneCentE8);

    std::int64_t magnitude = 1;
    while (magnitude <= rawStep / 10) magnitude *= 10;

    for (;;) {
        const std::int64_t candidates[] = {magnitude, magnitude * 2, magnitude * 5, magnitude * 10};
        for (const auto candidate : candidates) {
            if (candidate >= rawStep) return candidate;
        }
        magnitude *= 10;
    }
}

std::int64_t niceTimeStepNs(std::int64_t spanNs, int tickCount) {
    static constexpr std::int64_t kCandidates[] = {
        100ll * kOneMsNs,
        200ll * kOneMsNs,
        500ll * kOneMsNs,
        1000ll * kOneMsNs,
        2000ll * kOneMsNs,
        5000ll * kOneMsNs,
        10000ll * kOneMsNs,
        15000ll * kOneMsNs,
        30000ll * kOneMsNs,
        60000ll * kOneMsNs,
        120000ll * kOneMsNs,
        300000ll * kOneMsNs,
    };

    const auto safeTicks = std::max(2, tickCount);
    const std::int64_t rawStep = std::max<std::int64_t>(spanNs / static_cast<std::int64_t>(safeTicks - 1),
                                                        100ll * kOneMsNs);
    for (const auto candidate : kCandidates) {
        if (candidate >= rawStep) return candidate;
    }
    return kCandidates[std::size(kCandidates) - 1];
}

std::string stripFileUrl(const QString& path) {
    QString p = path;
    if (p.startsWith(QStringLiteral("file:///"))) p.remove(0, 8);
    else if (p.startsWith(QStringLiteral("file://"))) p.remove(0, 7);
    return p.toStdString();
}

QString formatScaledE8(std::int64_t value) {
    const bool negative = value < 0;
    const std::uint64_t absValue = negative
        ? static_cast<std::uint64_t>(-(value + 1)) + 1u
        : static_cast<std::uint64_t>(value);
    const std::uint64_t integerPart = absValue / 100000000ull;
    const std::uint64_t fractionPart = absValue % 100000000ull;
    return QStringLiteral("%1%2.%3")
        .arg(negative ? QStringLiteral("-") : QString{})
        .arg(integerPart)
        .arg(fractionPart, 8, 10, QLatin1Char('0'));
}

QString formatShortTimeNs(std::int64_t tsNs) {
    const qint64 ms = static_cast<qint64>(tsNs / 1000000ll);
    const auto dt = QDateTime::fromMSecsSinceEpoch(ms, Qt::UTC);
    if (!dt.isValid()) return QString::number(tsNs);
    return dt.toString(QStringLiteral("HH:mm:ss.zzz"));
}

bool envForcesSoftwareRenderer() {
    const auto qsgBackend = qEnvironmentVariable("QSG_RHI_BACKEND").trimmed().toLower();
    const auto quickBackend = qEnvironmentVariable("QT_QUICK_BACKEND").trimmed().toLower();
    const auto libglSoftware = qEnvironmentVariable("LIBGL_ALWAYS_SOFTWARE").trimmed().toLower();
    const auto xcbIntegration = qEnvironmentVariable("QT_XCB_GL_INTEGRATION").trimmed().toLower();

    return qsgBackend == QStringLiteral("software")
        || quickBackend == QStringLiteral("software")
        || libglSoftware == QStringLiteral("1")
        || xcbIntegration == QStringLiteral("none");
}

}  // namespace

ChartController::ChartController(QObject* parent) : QObject(parent) {
    gpuRendererAvailable_ = !envForcesSoftwareRenderer();
}

void ChartController::resetSession() {
    replay_.reset();
    loaded_ = false;
    sessionDir_.clear();
    tsMin_ = tsMax_ = priceMinE8_ = priceMaxE8_ = 0;
    currentBookTickerIndex_ = -1;
    statusText_ = QStringLiteral("Choose a session.");
    emit sessionChanged();
    emit statusChanged();
    emit viewportChanged();
}

bool ChartController::addTradesFile(const QString& path) {
    if (path.trimmed().isEmpty()) {
        statusText_ = QStringLiteral("No path. Enter a trades.jsonl path first.");
        emit statusChanged();
        return false;
    }

    const auto st = replay_.addTradesFile(stripFileUrl(path));
    if (!isOk(st)) {
        statusText_ = QStringLiteral("trades load failed: %1")
                          .arg(QString::fromUtf8(statusToString(st).data()));
        emit statusChanged();
        return false;
    }

    statusText_ = QStringLiteral("+ trades (now %1 rows)").arg(replay_.trades().size());
    emit statusChanged();
    return true;
}

bool ChartController::addBookTickerFile(const QString& path) {
    if (path.trimmed().isEmpty()) {
        statusText_ = QStringLiteral("No path. Enter a bookticker.jsonl path first.");
        emit statusChanged();
        return false;
    }

    const auto st = replay_.addBookTickerFile(stripFileUrl(path));
    if (!isOk(st)) {
        statusText_ = QStringLiteral("bookticker load failed: %1")
                          .arg(QString::fromUtf8(statusToString(st).data()));
        emit statusChanged();
        return false;
    }

    statusText_ = QStringLiteral("+ bookticker (now %1 rows)").arg(replay_.bookTickers().size());
    emit statusChanged();
    return true;
}

bool ChartController::addDepthFile(const QString& path) {
    if (path.trimmed().isEmpty()) {
        statusText_ = QStringLiteral("No path. Enter a depth.jsonl path first.");
        emit statusChanged();
        return false;
    }

    const auto st = replay_.addDepthFile(stripFileUrl(path));
    if (!isOk(st)) {
        statusText_ = QStringLiteral("depth load failed: %1")
                          .arg(QString::fromUtf8(statusToString(st).data()));
        emit statusChanged();
        return false;
    }

    statusText_ = QStringLiteral("+ depth (now %1 rows)").arg(replay_.depths().size());
    emit statusChanged();
    return true;
}

bool ChartController::addSnapshotFile(const QString& path) {
    if (path.trimmed().isEmpty()) {
        statusText_ = QStringLiteral("No path. Enter a snapshot_*.json path first.");
        emit statusChanged();
        return false;
    }

    const auto st = replay_.addSnapshotFile(stripFileUrl(path));
    if (!isOk(st)) {
        statusText_ = QStringLiteral("snapshot load failed: %1")
                          .arg(QString::fromUtf8(statusToString(st).data()));
        emit statusChanged();
        return false;
    }

    statusText_ = QStringLiteral("+ snapshot");
    emit statusChanged();
    return true;
}

void ChartController::finalizeFiles() {
    replay_.finalize();
    loaded_ = (replay_.events().size() > 0) || (replay_.book().bids().size() + replay_.book().asks().size() > 0);
    if (loaded_) computeInitialViewport_();
    currentBookTickerIndex_ = -1;

    statusText_ = QStringLiteral("Finalized.");
    emit sessionChanged();
    emit statusChanged();
    emit viewportChanged();
}

bool ChartController::loadSession(const QString& dir) {
    sessionDir_ = dir;
    loaded_ = false;
    replay_ = hftrec::replay::SessionReplay{};

    const auto path = std::filesystem::path(stripFileUrl(dir));
    const auto st = replay_.open(path);
    if (!isOk(st)) {
        statusText_ = QStringLiteral("Failed to load session: %1")
                          .arg(QString::fromUtf8(statusToString(st).data()));
        emit sessionChanged();
        emit statusChanged();
        return false;
    }

    loaded_ = !replay_.events().empty() || !replay_.book().bids().empty() || !replay_.book().asks().empty();
    currentBookTickerIndex_ = -1;
    statusText_ = QStringLiteral("Loaded trades=%1 depth=%2 bookticker=%3")
                      .arg(replay_.trades().size())
                      .arg(replay_.depths().size())
                      .arg(replay_.bookTickers().size());
    computeInitialViewport_();
    emit sessionChanged();
    emit statusChanged();
    emit viewportChanged();
    return true;
}

void ChartController::computeInitialViewport_() {
    tsMin_ = replay_.firstTsNs();
    tsMax_ = replay_.lastTsNs();
    if (tsMax_ == tsMin_) tsMax_ = tsMin_ + 1;

    std::int64_t pMin = 0;
    std::int64_t pMax = 0;
    if (!replay_.trades().empty()) {
        pMin = replay_.trades().front().priceE8;
        pMax = pMin;
    } else if (!replay_.book().bids().empty() || !replay_.book().asks().empty()) {
        pMin = replay_.book().bids().empty() ? replay_.book().bestAskPrice() : replay_.book().bestBidPrice();
        pMax = replay_.book().asks().empty() ? replay_.book().bestBidPrice() : replay_.book().bestAskPrice();
    } else if (!replay_.bookTickers().empty()) {
        pMin = replay_.bookTickers().front().bidPriceE8;
        pMax = replay_.bookTickers().front().askPriceE8;
    } else {
        pMin = 0;
        pMax = 1;
    }

    for (const auto& trade : replay_.trades()) {
        pMin = std::min(pMin, trade.priceE8);
        pMax = std::max(pMax, trade.priceE8);
    }
    for (const auto& ticker : replay_.bookTickers()) {
        if (ticker.bidPriceE8 != 0) pMin = std::min(pMin, ticker.bidPriceE8);
        if (ticker.askPriceE8 != 0) pMax = std::max(pMax, ticker.askPriceE8);
    }
    for (const auto& [price, _] : replay_.book().bids()) {
        pMin = std::min(pMin, price);
        pMax = std::max(pMax, price);
    }
    for (const auto& [price, _] : replay_.book().asks()) {
        pMin = std::min(pMin, price);
        pMax = std::max(pMax, price);
    }

    if (pMax <= pMin) pMax = pMin + 1;
    const std::int64_t pad = (pMax - pMin) / 10 + 1;
    priceMinE8_ = pMin - pad;
    priceMaxE8_ = pMax + pad;
}

void ChartController::setViewport(qint64 tsMin, qint64 tsMax,
                                  qint64 priceMinE8, qint64 priceMaxE8) {
    if (tsMax <= tsMin || priceMaxE8 <= priceMinE8) return;
    tsMin_ = tsMin;
    tsMax_ = tsMax;
    priceMinE8_ = priceMinE8;
    priceMaxE8_ = priceMaxE8;
    currentBookTickerIndex_ = -1;
    emit viewportChanged();
}

void ChartController::panTime(double fraction) {
    const qint64 w = tsMax_ - tsMin_;
    const qint64 dt = static_cast<qint64>(static_cast<double>(w) * fraction);
    tsMin_ += dt;
    tsMax_ += dt;
    currentBookTickerIndex_ = -1;
    emit viewportChanged();
}

void ChartController::panPrice(double fraction) {
    const qint64 h = priceMaxE8_ - priceMinE8_;
    const qint64 dp = static_cast<qint64>(static_cast<double>(h) * fraction);
    priceMinE8_ += dp;
    priceMaxE8_ += dp;
    emit viewportChanged();
}

void ChartController::zoomTime(double factor) {
    if (factor <= 0.0) return;
    const qint64 centre = (tsMin_ + tsMax_) / 2;
    const qint64 halfW = static_cast<qint64>(static_cast<double>(tsMax_ - tsMin_) / (2.0 * factor));
    tsMin_ = centre - halfW;
    tsMax_ = centre + halfW;
    if (tsMax_ <= tsMin_) tsMax_ = tsMin_ + 1;
    currentBookTickerIndex_ = -1;
    emit viewportChanged();
}

void ChartController::zoomPrice(double factor) {
    if (factor <= 0.0) return;
    const qint64 centre = (priceMinE8_ + priceMaxE8_) / 2;
    const qint64 halfH = static_cast<qint64>(static_cast<double>(priceMaxE8_ - priceMinE8_) / (2.0 * factor));
    priceMinE8_ = centre - halfH;
    priceMaxE8_ = centre + halfH;
    if (priceMaxE8_ <= priceMinE8_) priceMaxE8_ = priceMinE8_ + 1;
    emit viewportChanged();
}

void ChartController::autoFit() {
    if (!loaded_) return;
    computeInitialViewport_();
    currentBookTickerIndex_ = -1;
    emit viewportChanged();
}

void ChartController::jumpToStart() {
    const qint64 w = tsMax_ - tsMin_;
    tsMin_ = replay_.firstTsNs();
    tsMax_ = tsMin_ + w;
    currentBookTickerIndex_ = -1;
    emit viewportChanged();
}

void ChartController::jumpToEnd() {
    const qint64 w = tsMax_ - tsMin_;
    tsMax_ = replay_.lastTsNs();
    tsMin_ = tsMax_ - w;
    currentBookTickerIndex_ = -1;
    emit viewportChanged();
}

QString ChartController::formatPriceAt(double ratio) const {
    if (ratio < 0.0) ratio = 0.0;
    if (ratio > 1.0) ratio = 1.0;
    const auto span = priceMaxE8_ - priceMinE8_;
    const auto value = priceMaxE8_ - static_cast<qint64>(static_cast<double>(span) * ratio);
    return formatScaledE8(value);
}

QString ChartController::formatTimeAt(double ratio) const {
    if (ratio < 0.0) ratio = 0.0;
    if (ratio > 1.0) ratio = 1.0;
    const auto span = tsMax_ - tsMin_;
    const auto value = tsMin_ + static_cast<qint64>(static_cast<double>(span) * ratio);
    return formatShortTimeNs(value);
}

QString ChartController::formatPriceScaleLabel(int index, int tickCount) const {
    const auto safeTicks = std::max(2, tickCount);
    const auto step = nicePriceStepE8(std::max<std::int64_t>(priceMaxE8_ - priceMinE8_, 1), safeTicks);
    const auto top = ceilToStep(priceMaxE8_, step);
    const auto value = top - static_cast<std::int64_t>(index) * step;
    return formatScaledE8(value);
}

QString ChartController::formatTimeScaleLabel(int index, int tickCount) const {
    const auto safeTicks = std::max(2, tickCount);
    const auto step = niceTimeStepNs(std::max<std::int64_t>(tsMax_ - tsMin_, 1), safeTicks);
    const auto start = floorToStep(tsMin_, step);
    const auto value = start + static_cast<std::int64_t>(index) * step;
    return formatShortTimeNs(value);
}

std::int64_t ChartController::viewportCursorTs() const noexcept {
    return (tsMin_ + tsMax_) / 2;
}

void ChartController::syncReplayCursorToViewport() {
    if (!loaded_) {
        currentBookTickerIndex_ = -1;
        return;
    }

    const auto cursorTs = viewportCursorTs();
    replay_.seek(cursorTs);
    currentBookTickerIndex_ = -1;

    const auto& tickers = replay_.bookTickers();
    for (std::size_t i = 0; i < tickers.size(); ++i) {
        if (tickers[i].tsNs > cursorTs) break;
        currentBookTickerIndex_ = static_cast<std::int64_t>(i);
    }
}

const hftrec::replay::BookTickerRow* ChartController::currentBookTicker() const noexcept {
    if (currentBookTickerIndex_ < 0) return nullptr;
    const auto index = static_cast<std::size_t>(currentBookTickerIndex_);
    if (index >= replay_.bookTickers().size()) return nullptr;
    return &replay_.bookTickers()[index];
}

RenderSnapshot ChartController::buildSnapshot(qreal widthPx, qreal heightPx,
                                              const SnapshotInputs& in) {
    RenderSnapshot snap{};
    snap.vp = ViewportMap{tsMin_, tsMax_, priceMinE8_, priceMaxE8_,
                          static_cast<double>(widthPx), static_cast<double>(heightPx)};
    snap.loaded              = loaded_;
    snap.tradesVisible       = in.tradesVisible;
    snap.orderbookVisible    = in.orderbookVisible;
    snap.bookTickerVisible   = in.bookTickerVisible;
    snap.interactiveMode     = in.interactiveMode;
    snap.overlayOnly         = in.overlayOnly;
    snap.tradeAmountScale    = in.tradeAmountScale;
    snap.bookOpacityGain     = in.bookOpacityGain;
    snap.bookRenderDetail    = in.bookRenderDetail;

    if (!loaded_ || widthPx <= 0.0 || heightPx <= 0.0) return snap;
    if (snap.vp.tMax <= snap.vp.tMin || snap.vp.pMax <= snap.vp.pMin) return snap;

    // Trade dots — pre-filtered to viewport. origIndex kept so the connector
    // polyline can break on filtered-out neighbours.
    const auto& trades = replay_.trades();
    snap.tradeDots.reserve(trades.size());
    for (int i = 0; i < static_cast<int>(trades.size()); ++i) {
        const auto& t = trades[static_cast<std::size_t>(i)];
        if (t.tsNs < snap.vp.tMin || t.tsNs > snap.vp.tMax) continue;
        if (t.priceE8 < snap.vp.pMin || t.priceE8 > snap.vp.pMax) continue;
        snap.tradeDots.push_back(TradeDot{
            t.tsNs, t.priceE8, t.qtyE8,
            t.sideBuy != 0,
            i
        });
    }

    // Book segments — only when orderbook or ticker is drawn.
    if (!in.orderbookVisible && !in.bookTickerVisible) return snap;

    replay_.seek(snap.vp.tMin);
    const auto& events  = replay_.events();
    const auto& tickers = replay_.bookTickers();

    int activeTickerIndex = -1;
    {
        const auto it = std::upper_bound(
            tickers.begin(), tickers.end(), snap.vp.tMin,
            [](std::int64_t ts, const hftrec::replay::BookTickerRow& row) noexcept {
                return ts < row.tsNs;
            });
        if (it != tickers.begin()) {
            activeTickerIndex = static_cast<int>(std::distance(tickers.begin(), it) - 1);
        }
    }

    auto emitSegment = [&](std::int64_t tsStart, std::int64_t tsEnd) {
        if (tsEnd <= tsStart) return;
        BookSegment seg;
        seg.tsStartNs = tsStart;
        seg.tsEndNs   = tsEnd;

        const auto& book = replay_.book();
        if (in.orderbookVisible) {
            std::int64_t maxBid = 0;
            std::int64_t maxAsk = 0;
            for (const auto& [price, qty] : book.bids()) {
                if (price < snap.vp.pMin || price > snap.vp.pMax || qty <= 0) continue;
                seg.bids.push_back(BookLevel{price, qty});
                if (qty > maxBid) maxBid = qty;
            }
            for (const auto& [price, qty] : book.asks()) {
                if (price < snap.vp.pMin || price > snap.vp.pMax || qty <= 0) continue;
                seg.asks.push_back(BookLevel{price, qty});
                if (qty > maxAsk) maxAsk = qty;
            }
            seg.maxBidQty = std::max<std::int64_t>(maxBid, 1);
            seg.maxAskQty = std::max<std::int64_t>(maxAsk, 1);
        }
        if (in.bookTickerVisible && activeTickerIndex >= 0) {
            const auto& tk = tickers[static_cast<std::size_t>(activeTickerIndex)];
            seg.tickerBidE8 = tk.bidPriceE8;
            seg.tickerAskE8 = tk.askPriceE8;
        }
        snap.bookSegments.push_back(std::move(seg));
    };

    // Pixel-budget pruning: at high event density the viewport easily contains
    // more events than it has columns of pixels. Emitting a BookSegment per
    // event would force the renderer to redraw the full book on every sub-
    // pixel strip — O(events × levels) draw ops. Instead we keep replaying
    // deltas (so book state stays correct) but only materialize a new segment
    // once `stampTs` is at least one pixel to the right of the pending
    // segment's start. Visually identical; orders of magnitude cheaper.
    std::int64_t segStart    = snap.vp.tMin;
    std::size_t  eventCursor = replay_.cursor();
    while (eventCursor < events.size() && events[eventCursor].tsNs <= snap.vp.tMax) {
        const auto stampTs = events[eventCursor].tsNs;
        const double xStart = snap.vp.toX(segStart);
        const double xStamp = snap.vp.toX(stampTs);
        const bool   wide   = (xStamp - xStart) >= 1.0;

        if (wide) {
            emitSegment(segStart, stampTs);
            segStart = stampTs;
        }
        while (eventCursor < events.size() && events[eventCursor].tsNs == stampTs) {
            const auto& ev = events[eventCursor];
            if (ev.kind == hftrec::replay::SessionReplay::EventKind::BookTicker) {
                activeTickerIndex = static_cast<int>(ev.rowIndex);
            }
            ++eventCursor;
        }
        replay_.seek(stampTs);
        eventCursor = replay_.cursor();
    }
    emitSegment(segStart, snap.vp.tMax);

    return snap;
}

}  // namespace hftrec::gui::viewer
