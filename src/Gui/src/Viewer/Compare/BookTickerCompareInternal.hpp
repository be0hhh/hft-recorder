#pragma once

#include "BookTickerCompareItem.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QColor>
#include <QFontMetricsF>
#include <QPainter>
#include <QPen>
#include <QPolygonF>
#include <QRectF>

#include "BookTickerCompareCandlePaint.hpp"
#include "BookTickerCompareController.hpp"
#include "../Chart/ColorScheme.hpp"
#include "../RateLimit/RateLimitGraphPainter.hpp"
#include "../RateLimit/RateLimitUsage.hpp"

namespace hftrec::gui::viewer::compare_detail {

constexpr double kE8 = 100000000.0;
constexpr qreal kLeftMargin = 12.0;
constexpr qreal kTopMargin = 10.0;
constexpr qreal kRightScaleWidth = 118.0;
constexpr qreal kBottomScaleHeight = 28.0;
constexpr qreal kGap = 12.0;

struct Ranges {
    std::int64_t tsMin{0};
    std::int64_t tsMax{0};
    std::int64_t currentTs{0};
    std::int64_t priceMin{0};
    std::int64_t priceMax{0};
    double spreadMin{0.0};
    double spreadMax{1.0};
    double rawSpreadMin{0.0};
    double rawSpreadMax{0.0};
    bool hasRawSpreadRange{false};
    double internalPenaltyAvg{0.0};
    double feePenaltyAvg{0.0};
    double meanAvg{0.0};
    double deviationAbsMax{0.0};
    double costBandMax{0.0};
    double edgeAfterCostMax{0.0};
    double totalFeesBps{0.0};
};

struct LayoutRects {
    QRectF primaryRect{};
    QRectF spreadRect{};
    QRectF timeRect{};
    QRectF primaryScaleRect{};
    QRectF spreadScaleRect{};
};

QString indicatorRawLabel(const StrategyIndicatorData& indicator);

bool isToxicFlowPairIndicator(const StrategyIndicatorData& indicator) noexcept;

double bpsFromE8(std::int64_t value) noexcept;

bool isBasisConvergenceOverlay(const StrategyOverlayData& overlay) noexcept;

std::int64_t candleClosePriceE8(const hftrec::replay::CandleRow& row) noexcept;

void absorbPrice(const hftrec::replay::BookTickerRow& row, Ranges& ranges, bool& hasPrice) noexcept;

void absorbOverlayPrice(std::int64_t priceE8, Ranges& ranges, bool& hasPrice) noexcept;

void absorbCandlePrice(const hftrec::replay::CandleRow& row, Ranges& ranges, bool& hasPrice) noexcept;

void applyScaledIntRange(std::int64_t& minValue, std::int64_t& maxValue, double zoom, double pan) noexcept;

void applyScaledDoubleRange(double& minValue, double& maxValue, double zoom, double pan) noexcept;

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
                     double totalFeesBps) noexcept;

void applyControllerScale(Ranges& ranges, const BookTickerCompareController& controller, CompareLowerPaneKind lowerPaneKind) noexcept;

LayoutRects makeLayout(const QRectF& bounds) noexcept;

double xFor(std::int64_t ts, const Ranges& ranges, const QRectF& rect) noexcept;

std::int64_t tsForX(qreal x, const Ranges& ranges, const QRectF& rect) noexcept;

double priceYFor(std::int64_t price, const Ranges& ranges, const QRectF& rect) noexcept;

std::int64_t priceForY(qreal y, const Ranges& ranges, const QRectF& rect) noexcept;

double spreadYFor(double spreadBps, const Ranges& ranges, const QRectF& rect) noexcept;

double spreadForY(qreal y, const Ranges& ranges, const QRectF& rect) noexcept;

void drawPolyline(QPainter& painter, const QPolygonF& points, const QColor& color, int width = 1);

QColor sourceColor(std::uint32_t legIndex) noexcept;

std::uint32_t oppositeLegIndex(std::uint32_t legIndex) noexcept;

QColor marketBidColor(std::uint32_t legIndex) noexcept;

QColor marketAskColor(std::uint32_t legIndex) noexcept;

QColor oppositeMarkerColor(std::uint32_t legIndex, bool buy) noexcept;

QColor orderColor(std::uint32_t legIndex, bool buy) noexcept;

QColor spreadDirectionColor(std::uint8_t direction) noexcept;

BookTickerCompareCandlePaintRanges candlePaintRanges(const Ranges& ranges) noexcept;

void appendTickerStepPoints(const std::vector<hftrec::replay::BookTickerRow>& rows,
                            bool bid,
                            const Ranges& ranges,
                            const QRectF& rect,
                            QPolygonF& out);

void drawSpreadSegments(QPainter& painter,
                        const std::vector<hftrec::arbitrage::BookTickerSpreadPoint>& spreads,
                        const Ranges& ranges,
                        const QRectF& rect);

void drawMeanBands(QPainter& painter,
                   const std::vector<hftrec::arbitrage::BookTickerSpreadMeanPoint>& means,
                   const Ranges& ranges,
                   const QRectF& rect);

void drawStrategySpreadTrace(QPainter& painter,
                             const std::vector<StrategySpreadPoint>& points,
                             const Ranges& ranges,
                             const QRectF& rect,
                             bool basisOnly);

void drawStrategyIndicatorTrace(QPainter& painter,
                                const StrategyIndicatorData& indicator,
                                const Ranges& ranges,
                                const QRectF& rect);

void drawSideTriangle(QPainter& painter, qreal x, qreal y, qreal r, bool buy, const QColor& fill);

void drawStrategyOverlay(QPainter& painter,
                         const StrategyOverlayData& overlay,
                         const Ranges& ranges,
                         const QRectF& rect);

const hftrec::arbitrage::BookTickerSpreadPoint* nearestSpreadPoint(
    const std::vector<hftrec::arbitrage::BookTickerSpreadPoint>& points,
    std::int64_t ts) noexcept;

const hftrec::arbitrage::BookTickerSpreadMeanPoint* nearestMeanPoint(
    const std::vector<hftrec::arbitrage::BookTickerSpreadMeanPoint>& points,
    std::int64_t ts) noexcept;

const hftrec::arbitrage::CandleSpreadPoint* nearestCandleSpreadPoint(
    const std::vector<hftrec::arbitrage::CandleSpreadPoint>& points,
    std::int64_t ts) noexcept;

const StrategySpreadPoint* nearestStrategySpreadPoint(
    const std::vector<StrategySpreadPoint>& points,
    std::int64_t ts) noexcept;

const StrategyIndicatorPoint* nearestStrategyIndicatorPoint(
    const std::vector<StrategyIndicatorPoint>& points,
    std::int64_t ts) noexcept;

void drawStrategySpreadFillMarkers(QPainter& painter,
                                   const StrategyOverlayData& overlay,
                                   const std::vector<hftrec::arbitrage::BookTickerSpreadPoint>& spreads,
                                   const Ranges& ranges,
                                   const QRectF& rect);

QString formatPrice(std::int64_t priceE8);

QString formatBps(double bps);

QString formatPercent(double value);

double pctFromE4(std::int64_t pctE4) noexcept;

double rateLimitPctAt(const RateLimitUsageData& usage, std::int64_t ts) noexcept;

QString formatDurationNs(std::int64_t ns);

QString formatTimeOffset(std::int64_t ts, const Ranges& ranges);

void drawAxisTicks(QPainter& painter,
                   const QRectF& plotRect,
                   const QRectF& scaleRect,
                   const Ranges& ranges,
                   bool priceAxis,
                   const QString& lowerAxisLabel = QStringLiteral("bps"));

void drawTimeTicks(QPainter& painter, const QRectF& plotRect, const QRectF& timeRect, const Ranges& ranges);

const QRectF* rectForPoint(const QPointF& point, const LayoutRects& layout) noexcept;

void drawLabel(QPainter& painter, const QPointF& anchor, const QString& text, const QRectF& bounds);

}  // namespace hftrec::gui::viewer::compare_detail
