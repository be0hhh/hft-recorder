#include "gui/viewer/hit_test/HoverDetection.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include "gui/viewer/RenderSnapshot.hpp"
#include "gui/viewer/ViewportMap.hpp"
#include "gui/viewer/detail/BookMath.hpp"

namespace hftrec::gui::viewer::hit_test {

namespace {

std::int64_t timestampAtX(const ViewportMap& vp, qreal x) noexcept {
    if (vp.tMax <= vp.tMin || vp.w <= 0.0) return vp.tMin;
    const double clampedX = std::clamp(static_cast<double>(x), 0.0, vp.w);
    const double span = static_cast<double>(vp.tMax - vp.tMin);
    return vp.tMin + static_cast<std::int64_t>(std::llround((clampedX / vp.w) * span));
}

std::ptrdiff_t activeSegmentIndexAt(const RenderSnapshot& snap, qreal x) noexcept {
    if (snap.bookSegments.empty()) return -1;
    const std::int64_t cursor = timestampAtX(snap.vp, x);
    const auto it = std::upper_bound(
        snap.bookSegments.begin(),
        snap.bookSegments.end(),
        cursor,
        [](std::int64_t ts, const BookSegment& seg) noexcept { return ts < seg.tsStartNs; });
    if (it == snap.bookSegments.begin()) return -1;
    const auto index = static_cast<std::ptrdiff_t>((it - snap.bookSegments.begin()) - 1);
    const auto& seg = snap.bookSegments[static_cast<std::size_t>(index)];
    if (cursor < seg.tsStartNs || cursor > seg.tsEndNs) return -1;
    return index;
}

std::int64_t minVisibleAmountE8(const RenderSnapshot& snap) noexcept {
    constexpr std::int64_t kUsdScaleE8 = 100000000ll;
    const qreal clampedUsd = std::clamp<qreal>(snap.bookRenderDetail, 0.0, 1000000.0);
    return static_cast<std::int64_t>(std::llround(clampedUsd * static_cast<qreal>(kUsdScaleE8)));
}

const BookLevel* findVisibleLevelByPrice(const std::vector<BookLevel>& levels,
                                         const ViewportMap& vp,
                                         std::int64_t minVisibleLevelAmountE8,
                                         std::int64_t priceE8) noexcept {
    for (const auto& level : levels) {
        if (level.priceE8 != priceE8) continue;
        if (level.qtyE8 <= 0 || level.priceE8 < vp.pMin || level.priceE8 > vp.pMax) return nullptr;
        const auto amountE8 = detail::multiplyScaledE8(level.qtyE8, level.priceE8);
        return amountE8 >= minVisibleLevelAmountE8 ? &level : nullptr;
    }
    return nullptr;
}

void expandBookSpan(const RenderSnapshot& snap,
                    std::ptrdiff_t segmentIndex,
                    bool bidSide,
                    std::int64_t priceE8,
                    std::int64_t& outStartNs,
                    std::int64_t& outEndNs) noexcept {
    const auto& segments = snap.bookSegments;
    const auto minVisibleLevelAmountE8 = minVisibleAmountE8(snap);
    outStartNs = segments[static_cast<std::size_t>(segmentIndex)].tsStartNs;
    outEndNs = segments[static_cast<std::size_t>(segmentIndex)].tsEndNs;

    for (std::ptrdiff_t i = segmentIndex - 1; i >= 0; --i) {
        const auto& seg = segments[static_cast<std::size_t>(i)];
        const auto& levels = bidSide ? seg.bids : seg.asks;
        if (findVisibleLevelByPrice(levels, snap.vp, minVisibleLevelAmountE8, priceE8) == nullptr) break;
        outStartNs = seg.tsStartNs;
    }
    for (std::ptrdiff_t i = segmentIndex + 1; i < static_cast<std::ptrdiff_t>(segments.size()); ++i) {
        const auto& seg = segments[static_cast<std::size_t>(i)];
        const auto& levels = bidSide ? seg.bids : seg.asks;
        if (findVisibleLevelByPrice(levels, snap.vp, minVisibleLevelAmountE8, priceE8) == nullptr) break;
        outEndNs = seg.tsEndNs;
    }
}

const BookTickerSample* nearestBookTickerSample(const RenderSnapshot& snap, qreal x) noexcept {
    if (snap.bookTickerTrace.samples.empty()) return nullptr;
    const std::int64_t targetTs = timestampAtX(snap.vp, x);
    const auto& samples = snap.bookTickerTrace.samples;
    const auto it = std::lower_bound(
        samples.begin(),
        samples.end(),
        targetTs,
        [](const BookTickerSample& sample, std::int64_t target) noexcept {
            return sample.tsNs < target;
        });

    const BookTickerSample* best = nullptr;
    double bestDistancePx = std::numeric_limits<double>::max();
    if (it != samples.end()) {
        best = &*it;
        bestDistancePx = std::abs(snap.vp.toX(it->tsNs) - x);
    }
    if (it != samples.begin()) {
        const auto* prev = &*std::prev(it);
        const double distancePx = std::abs(snap.vp.toX(prev->tsNs) - x);
        if (distancePx < bestDistancePx) {
            best = prev;
            bestDistancePx = distancePx;
        }
    }
    return bestDistancePx <= 6.0 ? best : nullptr;
}

qreal fundingStripY(const RenderSnapshot& snap) noexcept {
    if (snap.vp.h <= 36.0) return std::max<qreal>(8.0, snap.vp.h * 0.5);
    return std::clamp<qreal>(snap.vp.h - 18.0, 18.0, snap.vp.h - 8.0);
}

void computeFundingHover(const RenderSnapshot& snap, const QPointF& point, HoverInfo& out) {
    if (!snap.fundingVisible || snap.fundings.empty()) return;
    constexpr qreal kStripHitPx = 12.0;
    constexpr qreal kMarkerHitPx = 8.0;
    constexpr qreal kAxisReserveRight = 88.0;
    const qreal stripLeft = 8.0;
    const qreal stripRight = std::max<qreal>(stripLeft, snap.vp.w - kAxisReserveRight);
    const qreal y = fundingStripY(snap);
    if (point.x() < stripLeft - kStripHitPx || point.x() > stripRight + kStripHitPx) return;
    if (std::abs(point.y() - y) > kStripHitPx) return;

    const std::int64_t targetTs = timestampAtX(snap.vp, point.x());
    const auto& rows = snap.fundings;
    const auto it = std::lower_bound(
        rows.begin(),
        rows.end(),
        targetTs,
        [](const hftrec::replay::FundingRow& row, std::int64_t target) noexcept {
            return row.tsNs < target;
        });

    const hftrec::replay::FundingRow* best = nullptr;
    std::size_t bestIndex = 0;
    qreal bestDistancePx = kMarkerHitPx;
    auto consider = [&](const hftrec::replay::FundingRow* row, std::size_t index) {
        if (row == nullptr || row->tsNs < snap.vp.tMin || row->tsNs > snap.vp.tMax) return;
        const qreal x = snap.vp.toX(row->tsNs);
        if (x < stripLeft || x > stripRight) return;
        const qreal distancePx = std::abs(x - point.x());
        if (distancePx <= bestDistancePx) {
            bestDistancePx = distancePx;
            best = row;
            bestIndex = index;
        }
    };
    if (it != rows.end()) consider(&*it, static_cast<std::size_t>(it - rows.begin()));
    if (it != rows.begin()) {
        const auto prev = std::prev(it);
        consider(&*prev, static_cast<std::size_t>(prev - rows.begin()));
    }
    if (best == nullptr) return;

    out.fundingHit = true;
    out.fundingEventTsNs = best->tsNs;
    out.fundingRateE8 = best->fundingRateE8;
    out.fundingTsNs = best->fundingTsNs;
    out.nextFundingTsNs = best->nextFundingTsNs;
    if (best->fundingTsNs > 0 && best->nextFundingTsNs > best->fundingTsNs) {
        out.fundingCadenceNs = best->nextFundingTsNs - best->fundingTsNs;
    } else if (bestIndex > 0u && best->tsNs > rows[bestIndex - 1u].tsNs) {
        out.fundingCadenceNs = best->tsNs - rows[bestIndex - 1u].tsNs;
    } else if ((bestIndex + 1u) < rows.size() && rows[bestIndex + 1u].tsNs > best->tsNs) {
        out.fundingCadenceNs = rows[bestIndex + 1u].tsNs - best->tsNs;
    }
}

}  // namespace

void computeHover(const RenderSnapshot& snap,
                  const QPointF& point,
                  bool contextActive,
                  HoverInfo& out) {
    out = HoverInfo{};
    out.active = true;
    out.contextActive = contextActive;
    out.point = point;

    if (!snap.loaded) {
        out.active = false;
        return;
    }
    const auto& vp = snap.vp;
    if (vp.tMax <= vp.tMin || vp.pMax <= vp.pMin || vp.w <= 0.0 || vp.h <= 0.0) {
        out.active = false;
        return;
    }

    if (contextActive) computeFundingHover(snap, point, out);

    if (snap.bookTickerVisible) {
        if (const auto* sample = nearestBookTickerSample(snap, point.x()); sample != nullptr) {
            constexpr double kTickerHitPx = 6.0;
            const double bidDist = sample->bidPriceE8 == 0
                ? std::numeric_limits<double>::max()
                : std::abs(vp.toY(sample->bidPriceE8) - point.y());
            const double askDist = sample->askPriceE8 == 0
                ? std::numeric_limits<double>::max()
                : std::abs(vp.toY(sample->askPriceE8) - point.y());

            if (bidDist <= kTickerHitPx || askDist <= kTickerHitPx) {
                const bool bidWins = bidDist <= askDist;
                out.bookKind = bidWins ? 1 : 2;
                out.bookPriceE8 = bidWins ? sample->bidPriceE8 : sample->askPriceE8;
                out.bookQtyE8 = bidWins ? sample->bidQtyE8 : sample->askQtyE8;
                out.bookTsNs = sample->tsNs;
            }
        }
    }

    if (out.bookKind == 0 && snap.orderbookVisible) {
        const auto segIndex = activeSegmentIndexAt(snap, point.x());
        if (segIndex >= 0) {
            const auto* seg = &snap.bookSegments[static_cast<std::size_t>(segIndex)];
            constexpr double kBookHitPx = 8.0;
            std::int64_t priceE8 = 0;
            std::int64_t qtyE8 = 0;
            if (detail::findNearestBookLevel(seg->bids, vp, point.y(), kBookHitPx, priceE8, qtyE8)) {
                out.bookKind = 3;
                out.bookPriceE8 = priceE8;
                out.bookQtyE8 = qtyE8;
                out.bookTsNs = std::clamp(timestampAtX(vp, point.x()), seg->tsStartNs, seg->tsEndNs);
                expandBookSpan(snap, segIndex, true, priceE8, out.bookTsStartNs, out.bookTsEndNs);
            }

            std::int64_t askPriceE8 = 0;
            std::int64_t askQtyE8 = 0;
            if (detail::findNearestBookLevel(seg->asks, vp, point.y(), kBookHitPx, askPriceE8, askQtyE8)) {
                const double askDistPx = std::abs(vp.toY(askPriceE8) - point.y());
                const double curDistPx = out.bookKind == 3
                    ? std::abs(vp.toY(out.bookPriceE8) - point.y())
                    : std::numeric_limits<double>::max();
                if (askDistPx <= curDistPx) {
                    out.bookKind = 4;
                    out.bookPriceE8 = askPriceE8;
                    out.bookQtyE8 = askQtyE8;
                    out.bookTsNs = std::clamp(timestampAtX(vp, point.x()), seg->tsStartNs, seg->tsEndNs);
                    expandBookSpan(snap, segIndex, false, askPriceE8, out.bookTsStartNs, out.bookTsEndNs);
                }
            }
        }
    }

    if (snap.liquidationsVisible) {
        constexpr double kHitRadiusPx = 9.0;
        const double hitSq = kHitRadiusPx * kHitRadiusPx;
        double bestSq = hitSq;
        for (const auto& dot : snap.liquidationDots) {
            const double x = vp.toX(dot.tsNs);
            if (x < (point.x() - kHitRadiusPx)) continue;
            if (x > (point.x() + kHitRadiusPx)) break;
            const double dx = x - point.x();
            const double dy = vp.toY(dot.priceE8) - point.y();
            const double distSq = dx * dx + dy * dy;
            if (distSq <= bestSq) {
                bestSq = distSq;
                out.liquidationHit = true;
                out.liquidationOrigIndex = dot.origIndex;
                out.liquidationTsNs = dot.tsNs;
                out.liquidationPriceE8 = dot.priceE8;
                out.liquidationQtyE8 = dot.qtyE8;
                out.liquidationAvgPriceE8 = dot.avgPriceE8;
                out.liquidationFilledQtyE8 = dot.filledQtyE8;
                out.liquidationSideBuy = dot.sideBuy;
            }
        }
        if (out.liquidationHit) return;
    }

    {
        constexpr double kHitRadiusPx = 9.0;
        const double hitSq = kHitRadiusPx * kHitRadiusPx;
        double bestSq = hitSq;
        for (const auto& marker : snap.strategyFillMarkers) {
            const double x = vp.toX(marker.tsNs);
            if (x < (point.x() - kHitRadiusPx)) continue;
            if (x > (point.x() + kHitRadiusPx)) break;
            const double dx = x - point.x();
            const double dy = vp.toY(marker.priceE8) - point.y();
            const double distSq = dx * dx + dy * dy;
            if (distSq <= bestSq) {
                bestSq = distSq;
                out.strategyFillHit = true;
                out.strategyFillOrderId = marker.orderId;
                out.strategyFillTsNs = marker.tsNs;
                out.strategyFillPriceE8 = marker.priceE8;
                out.strategyFillQtyE8 = marker.qtyE8;
                out.strategyFillAmountE8 = detail::multiplyScaledE8(marker.qtyE8, marker.priceE8);
                out.strategyFillSideBuy = marker.sideBuy;
                out.strategyFillReduceOnly = marker.reduceOnly;
                out.strategyFillReason = marker.fillReason;
                out.strategyFillLiquidity = marker.liquidity;
                out.strategyFillOrderQtyE8 = marker.orderQtyE8;
                out.strategyFillCumulativeFilledQtyE8 = marker.cumulativeFilledQtyE8;
                out.strategyFillRemainingQtyE8 = marker.remainingQtyE8;
                out.strategyFillAvgPriceE8 = marker.avgPriceE8;
                out.strategyFillBookLevelQtyE8 = marker.bookLevelQtyE8;
                out.strategyFillBookVisibleExecutableQtyE8 = marker.bookVisibleExecutableQtyE8;
                out.strategyFillBookConsumedPctE8 = marker.bookConsumedPctE8;
                out.strategyFillQueueAheadBeforeE8 = marker.queueAheadBeforeE8;
                out.strategyFillQueueAheadAfterE8 = marker.queueAheadAfterE8;
                out.strategyFillChunkIndex = marker.chunkIndex;
                out.strategyFillChunkCount = marker.chunkCount;
                out.strategyFillExecutionQtyE8 = marker.executionQtyE8;
                out.strategyFillExecutionAvgPriceE8 = marker.executionAvgPriceE8;
                out.strategyFillReferencePriceE8 = marker.referencePriceE8;
                out.strategyFillSlippageE8 = marker.slippageE8;
                out.strategyFillSlippageBpsE8 = marker.slippageBpsE8;
                out.strategyFillExecutionBookConsumedPctE8 = marker.executionBookConsumedPctE8;
            }
        }
        if (out.strategyFillHit) return;
    }

    if (!snap.tradesVisible) return;

    constexpr double kHitRadiusPx = 9.0;
    const double hitSq = kHitRadiusPx * kHitRadiusPx;
    double bestSq = hitSq;
    for (const auto& dot : snap.tradeDots) {
        const double x = vp.toX(dot.tsNs);
        if (x < (point.x() - kHitRadiusPx)) continue;
        if (x > (point.x() + kHitRadiusPx)) break;
        const double dx = x - point.x();
        const double dy = vp.toY(dot.priceE8) - point.y();
        const double distSq = dx * dx + dy * dy;
        if (distSq <= bestSq) {
            bestSq = distSq;
            out.tradeHit = true;
            out.tradeOrigIndex = dot.origIndex;
            out.tradeTsNs = dot.tsNs;
            out.tradePriceE8 = dot.priceE8;
            out.tradeQtyE8 = dot.qtyE8;
            out.tradeTotalQtyE8 = dot.totalQtyE8;
            out.tradeTotalAmountE8 = dot.totalAmountE8;
            out.tradeTsStartNs = dot.tsStartNs != 0 ? dot.tsStartNs : dot.tsNs;
            out.tradeTsEndNs = dot.tsEndNs != 0 ? dot.tsEndNs : dot.tsNs;
            out.tradeCount = dot.tradeCount != 0 ? dot.tradeCount : 1;
            out.tradeBuyQtyE8 = dot.buyQtyE8;
            out.tradeSellQtyE8 = dot.sellQtyE8;
            out.tradeBuyAmountE8 = dot.buyAmountE8;
            out.tradeSellAmountE8 = dot.sellAmountE8;
            out.tradeRepresentativePriceE8 = dot.representativePriceE8 != 0 ? dot.representativePriceE8 : dot.priceE8;
            out.tradeAggregated = dot.aggregated;
            out.tradeSideBuy = dot.sideBuy;
            out.tradeGroupEntries = dot.groupEntries;
        }
    }
}

}  // namespace hftrec::gui::viewer::hit_test
