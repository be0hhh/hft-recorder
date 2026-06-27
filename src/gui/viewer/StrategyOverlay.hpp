#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <QString>

namespace hftrec::gui::viewer {

enum class StrategyFillShape : std::uint8_t {
    BuyUp = 0,
    SellDown = 1,
};

inline constexpr std::uint8_t kStrategyOrderTypeLimit = 1u;
inline constexpr std::uint8_t kStrategyOrderTypeStopMarket = 3u;

struct StrategyOrderSegment {
    std::int64_t tsStartNs{0};
    std::int64_t tsEndNs{0};
    std::int64_t priceE8{0};
    std::int64_t qtyE8{0};
    std::uint32_t legIndex{0};
    bool sideBuy{true};
    bool openEnded{false};
    std::uint8_t orderType{kStrategyOrderTypeLimit};
};

struct StrategyFillMarker {
    std::int64_t tsNs{0};
    std::int64_t priceE8{0};
    std::int64_t qtyE8{0};
    std::uint32_t legIndex{0};
    bool sideBuy{true};
    bool marketOrder{false};
    bool reduceOnly{false};
    StrategyFillShape shape{StrategyFillShape::BuyUp};
    std::uint8_t fillReason{0};
    std::uint8_t liquidity{0};
    std::int64_t orderQtyE8{0};
    std::int64_t cumulativeFilledQtyE8{0};
    std::int64_t remainingQtyE8{0};
    std::int64_t avgPriceE8{0};
    std::int64_t bookLevelQtyE8{0};
    std::int64_t bookVisibleExecutableQtyE8{0};
    std::int64_t bookConsumedPctE8{0};
    std::int64_t queueAheadBeforeE8{0};
    std::int64_t queueAheadAfterE8{0};
    std::uint32_t chunkIndex{0};
    std::uint32_t chunkCount{0};
    std::int64_t executionQtyE8{0};
    std::int64_t executionAvgPriceE8{0};
    std::int64_t referencePriceE8{0};
    std::int64_t slippageE8{0};
    std::int64_t slippageBpsE8{0};
    std::int64_t executionBookConsumedPctE8{0};
    std::uint64_t orderId{0};
};

struct StrategyRangePoint {
    std::int64_t tsNs{0};
    std::int64_t lowE8{0};
    std::int64_t midE8{0};
    std::int64_t highE8{0};
};

struct StrategySpreadPoint {
    std::int64_t tsNs{0};
    std::int64_t spreadBpsE8{0};
    std::int64_t emaBpsE8{0};
    std::int64_t deviationBpsE8{0};
    std::int64_t costBandBpsE8{0};
    std::int64_t edgeAfterCostBpsE8{0};
    std::uint8_t direction{0};
    std::uint8_t decisionKind{0};
    std::uint8_t reasonRaw{0};
};

struct StrategyOverlayData {
    QString runId{};
    QString strategy{};
    QString sourceSessionPath{};
    std::vector<StrategyOrderSegment> orderSegments{};
    std::vector<StrategyFillMarker> fillMarkers{};
    std::vector<StrategyRangePoint> rangePoints{};
    std::vector<StrategySpreadPoint> spreadPoints{};

    bool empty() const noexcept { return orderSegments.empty() && fillMarkers.empty() && rangePoints.empty() && spreadPoints.empty(); }
};

bool loadStrategyOverlayFromResult(const std::filesystem::path& path,
                                   std::int64_t fallbackRunEndNs,
                                   StrategyOverlayData& out,
                                   std::string& error) noexcept;

}  // namespace hftrec::gui::viewer
