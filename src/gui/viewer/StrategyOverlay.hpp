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

struct StrategyOrderSegment {
    std::int64_t tsStartNs{0};
    std::int64_t tsEndNs{0};
    std::int64_t priceE8{0};
    std::int64_t qtyE8{0};
    std::uint32_t legIndex{0};
    bool sideBuy{true};
    bool openEnded{false};
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
};

struct StrategyOverlayData {
    QString runId{};
    QString strategy{};
    QString sourceSessionPath{};
    std::vector<StrategyOrderSegment> orderSegments{};
    std::vector<StrategyFillMarker> fillMarkers{};

    bool empty() const noexcept { return orderSegments.empty() && fillMarkers.empty(); }
};

bool loadStrategyOverlayFromResult(const std::filesystem::path& path,
                                   std::int64_t fallbackRunEndNs,
                                   StrategyOverlayData& out,
                                   std::string& error) noexcept;

}  // namespace hftrec::gui::viewer
