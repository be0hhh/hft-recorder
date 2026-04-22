#include "gui/viewer/ChartController.hpp"

#include <QDateTime>
#include <QByteArray>
#include <QStringList>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "gui/viewer/detail/BookMath.hpp"
#include "gui/viewer/detail/Formatters.hpp"

namespace hftrec::gui::viewer {

namespace {

QString formatShortTimeNs(std::int64_t tsNs) {
    const qint64 ms = static_cast<qint64>(tsNs / 1000000ll);
    const auto dt = QDateTime::fromMSecsSinceEpoch(ms, Qt::UTC);
    if (!dt.isValid()) return QString::number(tsNs);
    return dt.toString(QStringLiteral("HH:mm:ss.zzz"));
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

    constexpr std::int64_t kPercentScaleE8 = 10000000000ll;
    const std::int64_t diff = lastPriceE8 - firstPriceE8;
    const std::int64_t whole = diff / firstPriceE8;
    const std::int64_t rem = diff % firstPriceE8;
    return whole * kPercentScaleE8 + (rem * kPercentScaleE8) / firstPriceE8;
}

template <typename Row>
bool eventKeyLess(const Row& lhs, const Row& rhs) noexcept {
    if (lhs.tsNs != rhs.tsNs) return lhs.tsNs < rhs.tsNs;
    if (lhs.captureSeq != rhs.captureSeq) return lhs.captureSeq < rhs.captureSeq;
    return lhs.ingestSeq < rhs.ingestSeq;
}

template <typename Row>
bool stableContainsEvent(const std::vector<Row>& stable, const Row& row) {
    return std::any_of(stable.begin(), stable.end(), [&row](const Row& candidate) noexcept {
        return !eventKeyLess(candidate, row) && !eventKeyLess(row, candidate);
    });
}

template <typename Row, typename Fn>
void forEachLiveSelectionRow(const std::vector<Row>& stable,
                             const std::vector<Row>& overlay,
                             std::int64_t timeStartNs,
                             std::int64_t timeEndNs,
                             Fn&& fn) {
    for (const auto& row : stable) {
        if (row.tsNs < timeStartNs) continue;
        if (row.tsNs > timeEndNs) break;
        fn(row);
    }
    for (const auto& row : overlay) {
        if (row.tsNs < timeStartNs) continue;
        if (row.tsNs > timeEndNs) break;
        if (stableContainsEvent(stable, row)) continue;
        fn(row);
    }
}

}  // namespace

bool ChartController::commitSelectionRect(qreal plotWidthPx,
                                          qreal plotHeightPx,
                                          qreal x0,
                                          qreal y0,
                                          qreal x1,
                                          qreal y1) {
    return measureSelectionRect(plotWidthPx, plotHeightPx, x0, y0, x1, y1);
}

bool ChartController::measureSelectionRect(qreal plotWidthPx,
                                           qreal plotHeightPx,
                                           qreal x0,
                                           qreal y0,
                                           qreal x1,
                                           qreal y1) {
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

bool ChartController::measureTradeHighLowRect(qreal plotWidthPx,
                                              qreal plotHeightPx,
                                              qreal x0,
                                              qreal y0,
                                              qreal x1,
                                              qreal y1) {
    const auto outerRange = selectionFromRect_(plotWidthPx, plotHeightPx, x0, y0, x1, y1);
    if (!outerRange.valid) {
        clearSelection();
        return false;
    }

    bool found = false;
    std::int64_t highPriceE8 = 0;
    std::int64_t highTsNs = 0;
    std::int64_t lowPriceE8 = 0;
    std::int64_t lowTsNs = 0;

    const auto absorbTrade = [&](const hftrec::replay::TradeRow& trade) {
        if (trade.tsNs < outerRange.timeStartNs || trade.tsNs > outerRange.timeEndNs) return;
        if (trade.priceE8 < outerRange.priceMinE8 || trade.priceE8 > outerRange.priceMaxE8) return;
        if (!found) {
            found = true;
            highPriceE8 = lowPriceE8 = trade.priceE8;
            highTsNs = lowTsNs = trade.tsNs;
            return;
        }
        if (trade.priceE8 > highPriceE8) {
            highPriceE8 = trade.priceE8;
            highTsNs = trade.tsNs;
        }
        if (trade.priceE8 < lowPriceE8) {
            lowPriceE8 = trade.priceE8;
            lowTsNs = trade.tsNs;
        }
    };

    for (const auto& trade : replay_.trades()) absorbTrade(trade);
    forEachLiveSelectionRow(
        liveDataCache_.stableRows.trades,
        liveDataCache_.overlayRows.trades,
        outerRange.timeStartNs,
        outerRange.timeEndNs,
        absorbTrade);

    if (!found) {
        selectionSummaryText_ = QStringList{
            QStringLiteral("High/Low Trade Range"),
            QStringLiteral("No trades in range"),
        }.join(QLatin1Char('\n'));
        selectionActive_ = true;
        emit selectionChanged();
        return true;
    }

    SelectionRange range = outerRange;
    const auto summary = buildSelectionSummary_(range);
    QStringList lines;
    lines << QStringLiteral("High/Low Trade Range");
    lines << QStringLiteral("High  %1 @ %2")
                 .arg(detail::formatTrimmedE8(highPriceE8))
                 .arg(formatShortTimeNs(highTsNs));
    lines << QStringLiteral("Low   %1 @ %2")
                 .arg(detail::formatTrimmedE8(lowPriceE8))
                 .arg(formatShortTimeNs(lowTsNs));
    lines << QStringLiteral("Order %1")
                 .arg(highTsNs <= lowTsNs ? QStringLiteral("high -> low") : QStringLiteral("low -> high"));
    lines << QString{};
    lines << formatSelectionSummary_(range, summary);
    selectionSummaryText_ = lines.join(QLatin1Char('\n'));
    selectionActive_ = true;
    emit selectionChanged();
    return true;
}

bool ChartController::measurePointDistance(qreal plotWidthPx,
                                           qreal plotHeightPx,
                                           qreal x0,
                                           qreal y0,
                                           qreal x1,
                                           qreal y1) {
    if (plotWidthPx <= 1.0 || plotHeightPx <= 1.0 || tsMax_ <= tsMin_ || priceMaxE8_ <= priceMinE8_) {
        clearSelection();
        return false;
    }

    const qreal sx = std::clamp(x0, 0.0, plotWidthPx);
    const qreal sy = std::clamp(y0, 0.0, plotHeightPx);
    const qreal ex = std::clamp(x1, 0.0, plotWidthPx);
    const qreal ey = std::clamp(y1, 0.0, plotHeightPx);
    if ((std::abs(ex - sx) + std::abs(ey - sy)) < 2.0) {
        clearSelection();
        return false;
    }

    const qreal timeSpan = static_cast<qreal>(std::max<qint64>(1, tsMax_ - tsMin_));
    const qreal priceSpan = static_cast<qreal>(std::max<qint64>(1, priceMaxE8_ - priceMinE8_));
    const auto tsA = tsMin_ + static_cast<qint64>((sx / plotWidthPx) * timeSpan);
    const auto tsB = tsMin_ + static_cast<qint64>((ex / plotWidthPx) * timeSpan);
    const auto priceA = priceMaxE8_ - static_cast<qint64>((sy / plotHeightPx) * priceSpan);
    const auto priceB = priceMaxE8_ - static_cast<qint64>((ey / plotHeightPx) * priceSpan);

    QStringList lines;
    lines << QStringLiteral("Measure");
    lines << QStringLiteral("DeltaT %1 us").arg((tsB - tsA) / 1000);
    lines << QStringLiteral("Price  %1 -> %2")
                 .arg(detail::formatTrimmedE8(priceA))
                 .arg(detail::formatTrimmedE8(priceB));
    lines << QStringLiteral("Delta  %1").arg(detail::formatTrimmedE8(priceB - priceA));
    lines << QStringLiteral("Move   %1")
                 .arg(priceA > 0 ? formatPctE8(percentScaledE8(priceA, priceB)) : QStringLiteral("n/a"));
    selectionSummaryText_ = lines.join(QLatin1Char('\n'));
    selectionActive_ = true;
    emit selectionChanged();
    return true;
}

void ChartController::clearSelection() {
    if (!selectionActive_ && selectionSummaryText_.isEmpty()) return;
    selectionActive_ = false;
    selectionSummaryText_.clear();
    emit selectionChanged();
}

ChartController::SelectionRange ChartController::selectionFromRect_(qreal plotWidthPx,
                                                                    qreal plotHeightPx,
                                                                    qreal x0,
                                                                    qreal y0,
                                                                    qreal x1,
                                                                    qreal y1) const noexcept {
    SelectionRange range{};
    const bool hasSelectableRows = loaded_
        || !liveDataCache_.stableRows.trades.empty()
        || !liveDataCache_.stableRows.bookTickers.empty()
        || !liveDataCache_.stableRows.depths.empty()
        || !liveDataCache_.overlayRows.trades.empty()
        || !liveDataCache_.overlayRows.bookTickers.empty()
        || !liveDataCache_.overlayRows.depths.empty();
    if (!hasSelectableRows || plotWidthPx <= 1.0 || plotHeightPx <= 1.0) return range;

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
    range.priceMaxE8 =
        clampPriceToBand(range.priceMaxE8, range.priceMinE8 + 1, std::numeric_limits<std::int64_t>::max());
    range.valid = true;
    return range;
}

ChartController::SelectionSummary ChartController::buildSelectionSummary_(const SelectionRange& range) {
    SelectionSummary summary{};
    if (!range.valid) return summary;

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

    const auto absorbTrade = [&](const hftrec::replay::TradeRow& trade) {
        if (trade.priceE8 < range.priceMinE8 || trade.priceE8 > range.priceMaxE8) return;

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
    };
    forEachLiveSelectionRow(
        liveDataCache_.stableRows.trades,
        liveDataCache_.overlayRows.trades,
        range.timeStartNs,
        range.timeEndNs,
        absorbTrade);

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

    const auto absorbTicker = [&](const hftrec::replay::BookTickerRow& ticker) {
        const bool bidInBand = ticker.bidPriceE8 >= range.priceMinE8 && ticker.bidPriceE8 <= range.priceMaxE8;
        const bool askInBand = ticker.askPriceE8 >= range.priceMinE8 && ticker.askPriceE8 <= range.priceMaxE8;
        if (!bidInBand && !askInBand) return;
        ++summary.bookTickerCount;
    };
    forEachLiveSelectionRow(
        liveDataCache_.stableRows.bookTickers,
        liveDataCache_.overlayRows.bookTickers,
        range.timeStartNs,
        range.timeEndNs,
        absorbTicker);

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

    const auto absorbDepth = [&](const hftrec::replay::DepthRow& depth) {
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
    };
    forEachLiveSelectionRow(
        liveDataCache_.stableRows.depths,
        liveDataCache_.overlayRows.depths,
        range.timeStartNs,
        range.timeEndNs,
        absorbDepth);

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

    const auto absorbBookStateDepth = [&](const hftrec::replay::DepthRow& depth) {
        state.applyDelta(depth);

        std::int64_t bidE8 = 0;
        std::int64_t askE8 = 0;
        std::int64_t spreadE8 = 0;
        bool hasState = false;
        captureBookState(hasState, bidE8, askE8, spreadE8);
        if (hasState) updateExtrema(bidE8, askE8, spreadE8);
    };
    forEachLiveSelectionRow(
        liveDataCache_.stableRows.depths,
        liveDataCache_.overlayRows.depths,
        range.timeStartNs,
        range.timeEndNs,
        absorbBookStateDepth);

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

}  // namespace hftrec::gui::viewer
