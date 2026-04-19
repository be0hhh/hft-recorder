#include "gui/viewer/ChartController.hpp"

#include <QDateTime>
#include <QByteArray>
#include <QStringList>
#include <algorithm>
#include <filesystem>
#include <limits>
#include <string>
#include <sstream>

#include "gui/viewer/detail/BookMath.hpp"
#include "gui/viewer/detail/Formatters.hpp"

namespace hftrec::gui::viewer {

namespace {

constexpr std::int64_t kOneMsNs = 1000000ll;
constexpr std::size_t kViewportBookLevelsPerSide = 24;

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
    rawStep = std::max<std::int64_t>(rawStep, 1);

    std::int64_t magnitude = 1;
    while (magnitude <= rawStep / 10 && magnitude <= (std::numeric_limits<std::int64_t>::max() / 10)) {
        magnitude *= 10;
    }

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

void absorbPrice(std::int64_t price, bool& hasPrice, std::int64_t& pMin, std::int64_t& pMax) {
    if (price <= 0) return;
    if (!hasPrice) {
        pMin = price;
        pMax = price;
        hasPrice = true;
        return;
    }
    pMin = std::min(pMin, price);
    pMax = std::max(pMax, price);
}

template <typename MapT>
void absorbBookLevels(const MapT& levels,
                      std::size_t maxLevels,
                      bool& hasPrice,
                      std::int64_t& pMin,
                      std::int64_t& pMax) {
    std::size_t seen = 0;
    for (const auto& [price, qty] : levels) {
        if (seen >= maxLevels) break;
        if (qty <= 0) continue;
        absorbPrice(price, hasPrice, pMin, pMax);
        ++seen;
    }
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

std::int64_t clampPriceToBand(const std::int64_t price, const std::int64_t lo, const std::int64_t hi) {
    return std::max(lo, std::min(hi, price));
}

template <typename BidMapT>
bool findBestBidInBand(const BidMapT& bids,
                       std::int64_t priceMinE8,
                       std::int64_t priceMaxE8,
                       std::int64_t& outPriceE8) {
    for (const auto& [price, qty] : bids) {
        if (qty <= 0) continue;
        if (price < priceMinE8) break;
        if (price > priceMaxE8) continue;
        outPriceE8 = price;
        return true;
    }
    return false;
}

template <typename AskMapT>
bool findBestAskInBand(const AskMapT& asks,
                       std::int64_t priceMinE8,
                       std::int64_t priceMaxE8,
                       std::int64_t& outPriceE8) {
    for (const auto& [price, qty] : asks) {
        if (qty <= 0) continue;
        if (price > priceMaxE8) break;
        if (price < priceMinE8) continue;
        outPriceE8 = price;
        return true;
    }
    return false;
}

QString formatPctE8(std::int64_t pctE8) {
    return detail::formatTrimmedE8(pctE8) + QStringLiteral("%");
}

std::int64_t percentScaledE8(std::int64_t firstPriceE8, std::int64_t lastPriceE8) {
    if (firstPriceE8 <= 0) return 0;

    constexpr std::int64_t kPercentScaleE8 = 10000000000ll;  // 100 * 1e8
    const std::int64_t diff = lastPriceE8 - firstPriceE8;
    const std::int64_t whole = diff / firstPriceE8;
    const std::int64_t rem = diff % firstPriceE8;

    return whole * kPercentScaleE8 + (rem * kPercentScaleE8) / firstPriceE8;
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
    selectionActive_ = false;
    selectionSummaryText_.clear();
    statusText_ = QStringLiteral("Choose a session.");
    emit sessionChanged();
    emit statusChanged();
    emit viewportChanged();
    emit selectionChanged();
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
    clearSelection();
    replay_.finalize();
    if (!isOk(replay_.status())) {
        loaded_ = false;
        currentBookTickerIndex_ = -1;
        statusText_ = replay_.errorDetail().empty()
            ? QStringLiteral("Finalize failed: %1").arg(QString::fromUtf8(statusToString(replay_.status()).data()))
            : QStringLiteral("Finalize failed: %1")
                  .arg(QString::fromStdString(std::string{replay_.errorDetail()}));
        emit sessionChanged();
        emit statusChanged();
        emit viewportChanged();
        return;
    }
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
    clearSelection();

    const auto path = std::filesystem::path(stripFileUrl(dir));
    const auto st = replay_.open(path);
    if (!isOk(st)) {
        statusText_ = replay_.errorDetail().empty()
            ? QStringLiteral("Failed to load session: %1")
                  .arg(QString::fromUtf8(statusToString(st).data()))
            : QStringLiteral("Failed to load session: %1")
                  .arg(QString::fromStdString(std::string{replay_.errorDetail()}));
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
    if (!replay_.errorDetail().empty()) {
        statusText_ += QStringLiteral(" | %1").arg(QString::fromStdString(std::string{replay_.errorDetail()}));
    }
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
    bool hasPrice = false;

    for (const auto& trade : replay_.trades()) {
        absorbPrice(trade.priceE8, hasPrice, pMin, pMax);
    }
    for (const auto& ticker : replay_.bookTickers()) {
        absorbPrice(ticker.bidPriceE8, hasPrice, pMin, pMax);
        absorbPrice(ticker.askPriceE8, hasPrice, pMin, pMax);
    }

    // Use only the near-touch envelope from the current book. Full-depth scans
    // drag the initial viewport towards far-away passive levels and make
    // cheap instruments look incorrectly scaled.
    absorbBookLevels(replay_.book().bids(), kViewportBookLevelsPerSide, hasPrice, pMin, pMax);
    absorbBookLevels(replay_.book().asks(), kViewportBookLevelsPerSide, hasPrice, pMin, pMax);

    if (!hasPrice) {
        pMin = 0;
        pMax = 1;
    }

    if (pMax <= pMin) pMax = pMin + 1;
    const std::int64_t pad = (pMax - pMin) / 10 + 1;
    priceMinE8_ = std::max<std::int64_t>(0, pMin - pad);
    priceMaxE8_ = pMax + pad;
    if (priceMaxE8_ <= priceMinE8_) priceMaxE8_ = priceMinE8_ + 1;
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

bool ChartController::commitSelectionRect(qreal plotWidthPx, qreal plotHeightPx,
                                          qreal x0, qreal y0, qreal x1, qreal y1) {
    const auto range = selectionFromRect_(plotWidthPx, plotHeightPx, x0, y0, x1, y1);
    if (!range.valid) {
        clearSelection();
        return false;
    }

    const auto summary = buildSelectionSummary_(range);
    selectionSummaryText_ = formatSelectionSummary_(range, summary);
    selectionActive_ = !selectionSummaryText_.isEmpty();
    emit selectionChanged();
    return selectionActive_;
}

void ChartController::clearSelection() {
    if (!selectionActive_ && selectionSummaryText_.isEmpty()) return;
    selectionActive_ = false;
    selectionSummaryText_.clear();
    emit selectionChanged();
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

ChartController::SelectionRange ChartController::selectionFromRect_(qreal plotWidthPx, qreal plotHeightPx,
                                                                    qreal x0, qreal y0,
                                                                    qreal x1, qreal y1) const noexcept {
    SelectionRange range{};
    if (!loaded_ || plotWidthPx <= 1.0 || plotHeightPx <= 1.0) return range;

    const qreal left = std::clamp(std::min(x0, x1), 0.0, plotWidthPx);
    const qreal right = std::clamp(std::max(x0, x1), 0.0, plotWidthPx);
    const qreal top = std::clamp(std::min(y0, y1), 0.0, plotHeightPx);
    const qreal bottom = std::clamp(std::max(y0, y1), 0.0, plotHeightPx);
    if ((right - left) < 2.0 || (bottom - top) < 2.0) return range;

    const qreal timeSpan = static_cast<qreal>(std::max<qint64>(1, tsMax_ - tsMin_));
    const qreal priceSpan = static_cast<qreal>(std::max<qint64>(1, priceMaxE8_ - priceMinE8_));

    range.timeStartNs = tsMin_ + static_cast<qint64>((left / plotWidthPx) * timeSpan);
    range.timeEndNs = tsMin_ + static_cast<qint64>((right / plotWidthPx) * timeSpan);
    range.priceMaxE8 = priceMaxE8_ - static_cast<qint64>((top / plotHeightPx) * priceSpan);
    range.priceMinE8 = priceMaxE8_ - static_cast<qint64>((bottom / plotHeightPx) * priceSpan);

    if (range.timeEndNs <= range.timeStartNs || range.priceMaxE8 <= range.priceMinE8) return SelectionRange{};
    range.priceMinE8 = clampPriceToBand(range.priceMinE8, 0, priceMaxE8_);
    range.priceMaxE8 = clampPriceToBand(range.priceMaxE8, range.priceMinE8 + 1, std::numeric_limits<std::int64_t>::max());
    range.valid = true;
    return range;
}

ChartController::SelectionSummary ChartController::buildSelectionSummary_(const SelectionRange& range) {
    SelectionSummary summary{};
    if (!range.valid || !loaded_) return summary;

    summary.durationUs = std::max<std::int64_t>(0, (range.timeEndNs - range.timeStartNs) / 1000);

    bool firstTradeCaptured = false;
    std::int64_t firstTradePriceE8 = 0;
    std::int64_t lastTradePriceE8 = 0;

    for (const auto& trade : replay_.trades()) {
        if (trade.tsNs < range.timeStartNs) continue;
        if (trade.tsNs > range.timeEndNs) break;
        if (trade.priceE8 < range.priceMinE8 || trade.priceE8 > range.priceMaxE8) continue;

        ++summary.tradeCount;
        const auto notionalE8 = detail::multiplyScaledE8(trade.qtyE8, trade.priceE8);
        if (trade.sideBuy != 0) {
            summary.buyQtyE8 += trade.qtyE8;
            summary.buyNotionalE8 += notionalE8;
        } else {
            summary.sellQtyE8 += trade.qtyE8;
            summary.sellNotionalE8 += notionalE8;
        }

        if (!firstTradeCaptured) {
            firstTradeCaptured = true;
            firstTradePriceE8 = trade.priceE8;
        }
        lastTradePriceE8 = trade.priceE8;
    }

    if (firstTradeCaptured && summary.tradeCount >= 2 && firstTradePriceE8 > 0) {
        summary.hasMovePct = true;
        summary.movePctE8 = percentScaledE8(firstTradePriceE8, lastTradePriceE8);
    }

    for (const auto& ticker : replay_.bookTickers()) {
        if (ticker.tsNs < range.timeStartNs) continue;
        if (ticker.tsNs > range.timeEndNs) break;
        const bool bidInBand = ticker.bidPriceE8 >= range.priceMinE8 && ticker.bidPriceE8 <= range.priceMaxE8;
        const bool askInBand = ticker.askPriceE8 >= range.priceMinE8 && ticker.askPriceE8 <= range.priceMaxE8;
        if (!bidInBand && !askInBand) continue;
        ++summary.bookTickerCount;
    }

    for (const auto& depth : replay_.depths()) {
        if (depth.tsNs < range.timeStartNs) continue;
        if (depth.tsNs > range.timeEndNs) break;
        bool rowMatched = false;

        for (const auto& level : depth.bids) {
            if (level.priceE8 < range.priceMinE8 || level.priceE8 > range.priceMaxE8) continue;
            rowMatched = true;
            ++summary.bidLevelUpdates;
            if (level.qtyE8 == 0) ++summary.bidRemovals;
            else summary.bidQtyUpdatedE8 += level.qtyE8;
        }
        for (const auto& level : depth.asks) {
            if (level.priceE8 < range.priceMinE8 || level.priceE8 > range.priceMaxE8) continue;
            rowMatched = true;
            ++summary.askLevelUpdates;
            if (level.qtyE8 == 0) ++summary.askRemovals;
            else summary.askQtyUpdatedE8 += level.qtyE8;
        }

        if (rowMatched) ++summary.depthEventCount;
    }

    replay_.seek(range.timeStartNs);
    auto state = replay_.book();

    auto captureBookState = [&](bool& outHasState,
                                std::int64_t& outBidE8,
                                std::int64_t& outAskE8,
                                std::int64_t& outSpreadE8) {
        std::int64_t bidE8 = 0;
        std::int64_t askE8 = 0;
        const bool hasBid = findBestBidInBand(state.bids(), range.priceMinE8, range.priceMaxE8, bidE8);
        const bool hasAsk = findBestAskInBand(state.asks(), range.priceMinE8, range.priceMaxE8, askE8);
        if (!hasBid || !hasAsk || askE8 < bidE8) return;
        outHasState = true;
        outBidE8 = bidE8;
        outAskE8 = askE8;
        outSpreadE8 = askE8 - bidE8;
    };

    auto updateExtrema = [&](std::int64_t bidE8, std::int64_t askE8, std::int64_t spreadE8) {
        if (!summary.hasBestBidMax || bidE8 > summary.bestBidMaxE8) {
            summary.hasBestBidMax = true;
            summary.bestBidMaxE8 = bidE8;
        }
        if (!summary.hasBestAskMin || askE8 < summary.bestAskMinE8) {
            summary.hasBestAskMin = true;
            summary.bestAskMinE8 = askE8;
        }
        if (!summary.hasSpreadMin || spreadE8 < summary.spreadMinE8) {
            summary.hasSpreadMin = true;
            summary.spreadMinE8 = spreadE8;
        }
        if (!summary.hasSpreadMax || spreadE8 > summary.spreadMaxE8) {
            summary.hasSpreadMax = true;
            summary.spreadMaxE8 = spreadE8;
        }
    };

    captureBookState(summary.hasBookStart, summary.bestBidStartE8, summary.bestAskStartE8, summary.spreadStartE8);
    if (summary.hasBookStart) {
        updateExtrema(summary.bestBidStartE8, summary.bestAskStartE8, summary.spreadStartE8);
    }

    for (const auto& depth : replay_.depths()) {
        if (depth.tsNs < range.timeStartNs) continue;
        if (depth.tsNs > range.timeEndNs) break;
        state.applyDelta(depth);

        std::int64_t bidE8 = 0;
        std::int64_t askE8 = 0;
        std::int64_t spreadE8 = 0;
        bool hasState = false;
        captureBookState(hasState, bidE8, askE8, spreadE8);
        if (hasState) updateExtrema(bidE8, askE8, spreadE8);
    }

    captureBookState(summary.hasBookEnd, summary.bestBidEndE8, summary.bestAskEndE8, summary.spreadEndE8);
    syncReplayCursorToViewport();
    return summary;
}

QString ChartController::formatSelectionSummary_(const SelectionRange& range,
                                                 const SelectionSummary& summary) const {
    if (!range.valid) return {};

    QStringList lines;
    lines << QStringLiteral("Selection");
    lines << QStringLiteral("Time   %1 -> %2")
                 .arg(formatShortTimeNs(range.timeStartNs))
                 .arg(formatShortTimeNs(range.timeEndNs));
    lines << QStringLiteral("DeltaT %1 us").arg(summary.durationUs);
    lines << QStringLiteral("Price  %1 .. %2")
                 .arg(detail::formatTrimmedE8(range.priceMinE8))
                 .arg(detail::formatTrimmedE8(range.priceMaxE8));
    lines << QString{};

    const auto totalQtyE8 = summary.buyQtyE8 + summary.sellQtyE8;
    const auto totalNotionalE8 = summary.buyNotionalE8 + summary.sellNotionalE8;
    const auto qtyDeltaE8 = summary.buyQtyE8 - summary.sellQtyE8;
    const auto usdDeltaE8 = summary.buyNotionalE8 - summary.sellNotionalE8;

    lines << QStringLiteral("Trades");
    lines << QStringLiteral("Count  %1").arg(summary.tradeCount);
    lines << QStringLiteral("Buy    %1 coin | $%2")
                 .arg(detail::formatTrimmedE8(summary.buyQtyE8))
                 .arg(detail::formatTrimmedE8(summary.buyNotionalE8));
    lines << QStringLiteral("Sell   %1 coin | $%2")
                 .arg(detail::formatTrimmedE8(summary.sellQtyE8))
                 .arg(detail::formatTrimmedE8(summary.sellNotionalE8));
    lines << QStringLiteral("Total  %1 coin | $%2")
                 .arg(detail::formatTrimmedE8(totalQtyE8))
                 .arg(detail::formatTrimmedE8(totalNotionalE8));
    lines << QStringLiteral("Delta  %1 coin | $%2")
                 .arg(detail::formatTrimmedE8(qtyDeltaE8))
                 .arg(detail::formatTrimmedE8(usdDeltaE8));
    lines << QStringLiteral("Move   %1")
                 .arg(summary.hasMovePct ? formatPctE8(summary.movePctE8) : QStringLiteral("n/a"));
    return lines.join(QLatin1Char('\n'));
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

    const std::int64_t coverageStart = std::max<std::int64_t>(snap.vp.tMin, replay_.firstTsNs());
    const std::int64_t coverageEnd = std::min<std::int64_t>(snap.vp.tMax, replay_.lastTsNs());
    if (coverageEnd <= coverageStart) return snap;

    replay_.seek(coverageStart);
    const auto& events  = replay_.events();
    const auto& tickers = replay_.bookTickers();

    int activeTickerIndex = -1;
    {
        const auto it = std::upper_bound(
            tickers.begin(), tickers.end(), coverageStart,
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
    std::int64_t segStart    = coverageStart;
    std::size_t  eventCursor = replay_.cursor();
    while (eventCursor < events.size() && events[eventCursor].tsNs <= coverageEnd) {
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
    emitSegment(segStart, coverageEnd);

    return snap;
}

}  // namespace hftrec::gui::viewer
