#include "BookTickerCompareInternal.hpp"

namespace hftrec::gui::viewer::compare_detail {

QString indicatorRawLabel(const StrategyIndicatorData& indicator) {
    if (!indicator.auxLabel.isEmpty()) return indicator.auxLabel;
    if (!indicator.valueLabel.isEmpty()) return indicator.valueLabel;
    return QStringLiteral("value");
}

bool isToxicFlowPairIndicator(const StrategyIndicatorData& indicator) noexcept {
    return indicator.profile == QStringLiteral("toxic_flow_pair");
}

double bpsFromE8(std::int64_t value) noexcept {
    return static_cast<double>(value) / kE8;
}

bool isBasisConvergenceOverlay(const StrategyOverlayData& overlay) noexcept {
    return overlay.strategy == QStringLiteral("basis_convergence_probe");
}

std::int64_t candleClosePriceE8(const hftrec::replay::CandleRow& row) noexcept {
    if (row.closeE8 > 0) return row.closeE8;
    if (row.highE8 <= 0 || row.lowE8 <= 0 || row.highE8 < row.lowE8) return 0;
    return row.lowE8 + ((row.highE8 - row.lowE8) / 2);
}

void absorbPrice(const hftrec::replay::BookTickerRow& row, Ranges& ranges, bool& hasPrice) noexcept {
    if (row.bidPriceE8 <= 0 || row.askPriceE8 <= 0) return;
    if (!hasPrice) {
        ranges.priceMin = std::min(row.bidPriceE8, row.askPriceE8);
        ranges.priceMax = std::max(row.bidPriceE8, row.askPriceE8);
        hasPrice = true;
        return;
    }
    ranges.priceMin = std::min(ranges.priceMin, std::min(row.bidPriceE8, row.askPriceE8));
    ranges.priceMax = std::max(ranges.priceMax, std::max(row.bidPriceE8, row.askPriceE8));
}

void absorbOverlayPrice(std::int64_t priceE8, Ranges& ranges, bool& hasPrice) noexcept {
    if (priceE8 <= 0) return;
    if (!hasPrice) {
        ranges.priceMin = priceE8;
        ranges.priceMax = priceE8;
        hasPrice = true;
        return;
    }
    ranges.priceMin = std::min(ranges.priceMin, priceE8);
    ranges.priceMax = std::max(ranges.priceMax, priceE8);
}

void absorbCandlePrice(const hftrec::replay::CandleRow& row, Ranges& ranges, bool& hasPrice) noexcept {
    if (row.highE8 > 0 && row.lowE8 > 0 && row.highE8 >= row.lowE8) {
        absorbOverlayPrice(row.highE8, ranges, hasPrice);
        absorbOverlayPrice(row.lowE8, ranges, hasPrice);
        return;
    }
    const auto price = candleClosePriceE8(row);
    if (price > 0) absorbOverlayPrice(price, ranges, hasPrice);
}

void applyScaledIntRange(std::int64_t& minValue, std::int64_t& maxValue, double zoom, double pan) noexcept {
    if (maxValue <= minValue) maxValue = minValue + 1;
    if (!std::isfinite(zoom) || zoom < 1.0) zoom = 1.0;
    const double span = static_cast<double>(maxValue - minValue);
    const double nextSpan = std::max(1.0, span / zoom);
    const double center = (static_cast<double>(minValue) + static_cast<double>(maxValue)) * 0.5 + pan * span;
    minValue = static_cast<std::int64_t>(center - nextSpan * 0.5);
    maxValue = static_cast<std::int64_t>(center + nextSpan * 0.5);
    if (maxValue <= minValue) maxValue = minValue + 1;
}

void applyScaledDoubleRange(double& minValue, double& maxValue, double zoom, double pan) noexcept {
    if (maxValue <= minValue) maxValue = minValue + 1.0;
    if (!std::isfinite(zoom) || zoom < 1.0) zoom = 1.0;
    const double span = maxValue - minValue;
    const double nextSpan = std::max(0.000001, span / zoom);
    const double center = (minValue + maxValue) * 0.5 + pan * span;
    minValue = center - nextSpan * 0.5;
    maxValue = center + nextSpan * 0.5;
    if (maxValue <= minValue) maxValue = minValue + 1.0;
}

Ranges computeRanges(const std::vector<hftrec::replay::BookTickerRow>& a,
                     const std::vector<hftrec::replay::BookTickerRow>& b,
                     const std::vector<hftrec::replay::CandleRow>& candlesA,
                     const std::vector<hftrec::replay::CandleRow>& candlesB,
                     const std::vector<hftrec::arbitrage::BookTickerSpreadPoint>& spreads,
                     const std::vector<hftrec::arbitrage::BookTickerSpreadMeanPoint>& means,
                     const std::vector<hftrec::arbitrage::CandleSpreadPoint>& candleSpreads,
                     const StrategyOverlayData& overlay,
                     const StrategyIndicatorData& indicator,
                     CompareLowerPaneKind lowerPaneKind,
                     std::int64_t tsMin,
                     std::int64_t tsMax,
                     std::int64_t currentTs,
                     double totalFeesBps) noexcept {
    Ranges ranges{};
    ranges.tsMin = tsMin;
    ranges.tsMax = tsMax > tsMin ? tsMax : tsMin + 1;
    ranges.currentTs = currentTs;
    ranges.totalFeesBps = totalFeesBps;
    bool hasPrice = false;
    auto absorbCarryPrice = [&](const std::vector<hftrec::replay::BookTickerRow>& rows) noexcept {
        const hftrec::replay::BookTickerRow* carry = nullptr;
        for (const auto& row : rows) {
            if (row.tsNs > ranges.tsMin) break;
            if (row.bidPriceE8 > 0 && row.askPriceE8 > 0) carry = &row;
        }
        if (carry != nullptr) absorbPrice(*carry, ranges, hasPrice);
    };
    auto absorbCarryCandlePrice = [&](const std::vector<hftrec::replay::CandleRow>& rows) noexcept {
        const hftrec::replay::CandleRow* carry = nullptr;
        for (const auto& row : rows) {
            if (row.tsNs > ranges.tsMin) break;
            if (candleClosePriceE8(row) > 0) carry = &row;
        }
        if (carry != nullptr) absorbCandlePrice(*carry, ranges, hasPrice);
    };
    absorbCarryPrice(a);
    absorbCarryPrice(b);
    absorbCarryCandlePrice(candlesA);
    absorbCarryCandlePrice(candlesB);
    for (const auto& row : a) {
        if (row.tsNs < ranges.tsMin || row.tsNs > ranges.tsMax) continue;
        absorbPrice(row, ranges, hasPrice);
    }
    for (const auto& row : b) {
        if (row.tsNs < ranges.tsMin || row.tsNs > ranges.tsMax) continue;
        absorbPrice(row, ranges, hasPrice);
    }
    for (const auto& row : candlesA) {
        if (row.tsNs < ranges.tsMin || row.tsNs > ranges.tsMax) continue;
        absorbCandlePrice(row, ranges, hasPrice);
    }
    for (const auto& row : candlesB) {
        if (row.tsNs < ranges.tsMin || row.tsNs > ranges.tsMax) continue;
        absorbCandlePrice(row, ranges, hasPrice);
    }
    for (const auto& segment : overlay.orderSegments) {
        if (segment.tsEndNs < ranges.tsMin || segment.tsStartNs > ranges.tsMax) continue;
        absorbOverlayPrice(segment.priceE8, ranges, hasPrice);
    }
    for (const auto& marker : overlay.fillMarkers) {
        if (marker.tsNs < ranges.tsMin || marker.tsNs > ranges.tsMax) continue;
        absorbOverlayPrice(marker.priceE8, ranges, hasPrice);
    }
    bool hasSpread = false;
    const bool basisOnlySpread = lowerPaneKind == CompareLowerPaneKind::StrategySpread && isBasisConvergenceOverlay(overlay);
    if (lowerPaneKind == CompareLowerPaneKind::StrategySpread && !overlay.spreadPoints.empty()) {
        double emaSum = 0.0;
        std::size_t emaCount = 0u;
        for (const auto& point : overlay.spreadPoints) {
            if (point.tsNs < ranges.tsMin || point.tsNs > ranges.tsMax) continue;
            const double spread = bpsFromE8(point.spreadBpsE8);
            const double ema = bpsFromE8(point.emaBpsE8);
            const double cost = bpsFromE8(point.costBandBpsE8);
            if (!ranges.hasRawSpreadRange) {
                ranges.rawSpreadMin = spread;
                ranges.rawSpreadMax = spread;
                ranges.hasRawSpreadRange = true;
            } else {
                ranges.rawSpreadMin = std::min(ranges.rawSpreadMin, spread);
                ranges.rawSpreadMax = std::max(ranges.rawSpreadMax, spread);
            }
            if (!hasSpread) {
                ranges.spreadMin = basisOnlySpread ? spread : std::min(spread, ema - cost);
                ranges.spreadMax = basisOnlySpread ? spread : std::max(spread, ema + cost);
                hasSpread = true;
            } else {
                ranges.spreadMin = basisOnlySpread ? std::min(ranges.spreadMin, spread)
                                                   : std::min(ranges.spreadMin, std::min(spread, ema - cost));
                ranges.spreadMax = basisOnlySpread ? std::max(ranges.spreadMax, spread)
                                                   : std::max(ranges.spreadMax, std::max(spread, ema + cost));
            }
            emaSum += basisOnlySpread ? spread : ema;
            ranges.costBandMax = std::max(ranges.costBandMax, cost);
            ranges.deviationAbsMax = std::max(ranges.deviationAbsMax, std::abs(bpsFromE8(point.deviationBpsE8)));
            ranges.edgeAfterCostMax = std::max(ranges.edgeAfterCostMax, bpsFromE8(point.edgeAfterCostBpsE8));
            ++emaCount;
        }
        if (emaCount > 0u) ranges.meanAvg = emaSum / static_cast<double>(emaCount);
    }
    if (lowerPaneKind == CompareLowerPaneKind::StrategyIndicator && !indicator.points.empty()) {
        double rawSum = 0.0;
        std::size_t rawCount = 0u;
        const bool pairIndicator = isToxicFlowPairIndicator(indicator);
        auto absorbIndicator = [&](double raw) noexcept {
            if (!hasSpread) {
                ranges.spreadMin = raw;
                ranges.spreadMax = raw;
                hasSpread = true;
            } else {
                ranges.spreadMin = std::min(ranges.spreadMin, raw);
                ranges.spreadMax = std::max(ranges.spreadMax, raw);
            }
            rawSum += raw;
            ++rawCount;
        };
        for (const auto& point : indicator.points) {
            if (point.tsNs < ranges.tsMin || point.tsNs > ranges.tsMax) continue;
            if (pairIndicator) absorbIndicator(static_cast<double>(point.valueRaw));
            absorbIndicator(static_cast<double>(point.auxRaw));
        }
        if (rawCount > 0u) ranges.meanAvg = rawSum / static_cast<double>(rawCount);
    }
    if (lowerPaneKind == CompareLowerPaneKind::RateLimitUsage) {
        ranges.spreadMin = 0.0;
        ranges.spreadMax = 100.0;
        hasSpread = true;
    }
    double penaltySum = 0.0;
    double feePenaltySum = 0.0;
    std::size_t penaltyCount = 0u;
    if (lowerPaneKind == CompareLowerPaneKind::DefaultSpread || lowerPaneKind == CompareLowerPaneKind::MarketSpreadOverlay) {
        const hftrec::arbitrage::BookTickerSpreadPoint* carrySpread = nullptr;
        for (const auto& point : spreads) {
            if (point.tsNs > ranges.tsMin) break;
            carrySpread = &point;
        }
        if (carrySpread != nullptr) {
            ranges.spreadMin = std::min(0.0, carrySpread->spreadBps);
            ranges.spreadMax = std::max(1.0, carrySpread->spreadBps);
            ranges.rawSpreadMin = carrySpread->rawSpreadBps;
            ranges.rawSpreadMax = carrySpread->rawSpreadBps;
            ranges.hasRawSpreadRange = true;
            hasSpread = true;
        }
        for (const auto& point : spreads) {
            if (point.tsNs < ranges.tsMin || point.tsNs > ranges.tsMax) continue;
            if (!hasSpread) {
                ranges.spreadMin = std::min(0.0, point.spreadBps);
                ranges.spreadMax = std::max(1.0, point.spreadBps);
                ranges.rawSpreadMin = point.rawSpreadBps;
                ranges.rawSpreadMax = point.rawSpreadBps;
                ranges.hasRawSpreadRange = true;
                hasSpread = true;
            } else {
                ranges.spreadMin = std::min(ranges.spreadMin, point.spreadBps);
                ranges.spreadMax = std::max(ranges.spreadMax, point.spreadBps);
                ranges.rawSpreadMin = std::min(ranges.rawSpreadMin, point.rawSpreadBps);
                ranges.rawSpreadMax = std::max(ranges.rawSpreadMax, point.rawSpreadBps);
                ranges.hasRawSpreadRange = true;
            }
            penaltySum += point.internalPenaltyBps;
            feePenaltySum += point.feePenaltyBps;
            ++penaltyCount;
        }
        if (penaltyCount > 0u) ranges.internalPenaltyAvg = penaltySum / static_cast<double>(penaltyCount);
        if (penaltyCount > 0u) ranges.feePenaltyAvg = feePenaltySum / static_cast<double>(penaltyCount);
        double meanSum = 0.0;
        std::size_t meanCount = 0u;
        for (const auto& point : means) {
            if (point.tsNs < ranges.tsMin || point.tsNs > ranges.tsMax) continue;
            if (!hasSpread) {
                ranges.spreadMin = std::min(point.meanBps - point.costBandBps, point.meanBps);
                ranges.spreadMax = std::max(point.meanBps + point.costBandBps, point.meanBps);
                hasSpread = true;
            } else {
                ranges.spreadMin = std::min(ranges.spreadMin, point.meanBps - point.costBandBps);
                ranges.spreadMax = std::max(ranges.spreadMax, point.meanBps + point.costBandBps);
            }
            meanSum += point.meanBps;
            ranges.costBandMax = std::max(ranges.costBandMax, point.costBandBps);
            ranges.deviationAbsMax = std::max(ranges.deviationAbsMax, std::abs(point.deviationBps));
            ranges.edgeAfterCostMax = std::max(ranges.edgeAfterCostMax, point.edgeAfterCostBps);
            ++meanCount;
        }
        if (meanCount > 0u) ranges.meanAvg = meanSum / static_cast<double>(meanCount);
    }
    if ((lowerPaneKind == CompareLowerPaneKind::CandleSpread || lowerPaneKind == CompareLowerPaneKind::MarketSpreadOverlay)
        && !candleSpreads.empty()) {
        double candleSum = 0.0;
        std::size_t candleCount = 0u;
        for (const auto& point : candleSpreads) {
            if (point.tsNs < ranges.tsMin || point.tsNs > ranges.tsMax) continue;
            if (!hasSpread) {
                ranges.spreadMin = point.spreadBps;
                ranges.spreadMax = point.spreadBps;
                hasSpread = true;
            } else {
                ranges.spreadMin = std::min(ranges.spreadMin, point.spreadBps);
                ranges.spreadMax = std::max(ranges.spreadMax, point.spreadBps);
            }
            candleSum += point.spreadBps;
            ++candleCount;
        }
        if (candleCount > 0u && ranges.meanAvg == 0.0) ranges.meanAvg = candleSum / static_cast<double>(candleCount);
    }
    if (!hasPrice) {
        for (const auto& row : a) absorbPrice(row, ranges, hasPrice);
        for (const auto& row : b) absorbPrice(row, ranges, hasPrice);
        for (const auto& row : candlesA) absorbCandlePrice(row, ranges, hasPrice);
        for (const auto& row : candlesB) absorbCandlePrice(row, ranges, hasPrice);
    }
    if (ranges.priceMax <= ranges.priceMin) ranges.priceMax = ranges.priceMin + 1;
    const auto pricePad = std::max<std::int64_t>(1, (ranges.priceMax - ranges.priceMin) / 20);
    ranges.priceMin -= pricePad;
    ranges.priceMax += pricePad;
    if (lowerPaneKind == CompareLowerPaneKind::RateLimitUsage) {
        ranges.spreadMin = 0.0;
        ranges.spreadMax = 100.0;
    } else {
        if (std::abs(ranges.spreadMax - ranges.spreadMin) < 0.000001) ranges.spreadMax = ranges.spreadMin + 1.0;
        const double spreadPad = std::max(0.25, (ranges.spreadMax - ranges.spreadMin) * 0.10);
        ranges.spreadMin -= spreadPad;
        ranges.spreadMax += spreadPad;
    }
    if (lowerPaneKind != CompareLowerPaneKind::RateLimitUsage
        && lowerPaneKind != CompareLowerPaneKind::StrategyIndicator
        && !basisOnlySpread
        && lowerPaneKind != CompareLowerPaneKind::CandleSpread) {
        ranges.spreadMin = std::min(ranges.spreadMin, 0.0);
        ranges.spreadMax = std::max(ranges.spreadMax, 1.0);
    }
    return ranges;
}

void applyControllerScale(Ranges& ranges, const BookTickerCompareController& controller, CompareLowerPaneKind lowerPaneKind) noexcept {
    applyScaledIntRange(ranges.priceMin, ranges.priceMax, controller.priceZoom(), controller.pricePan());
    if (lowerPaneKind != CompareLowerPaneKind::RateLimitUsage) {
        applyScaledDoubleRange(ranges.spreadMin, ranges.spreadMax, controller.spreadZoom(), controller.spreadPan());
    }
}

}  // namespace hftrec::gui::viewer::compare_detail
