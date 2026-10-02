#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <QString>

namespace hftrec::gui::viewer {

struct StrategyIndicatorPoint {
    std::int64_t tsNs{0};
    std::int64_t valueRaw{0};
    std::int64_t auxRaw{0};
    std::uint8_t eventCode{0u};
    std::uint8_t reasonRaw{0u};
    std::uint8_t decisionKind{0u};
    std::uint8_t sideRaw{0u};
};

struct StrategyIndicatorData {
    QString profile{};
    QString title{};
    QString valueLabel{};
    QString auxLabel{};
    QString unit{};
    std::vector<StrategyIndicatorPoint> points{};

    bool empty() const noexcept { return points.empty(); }
};

bool loadStrategyIndicatorFromResult(const std::filesystem::path& path,
                                     StrategyIndicatorData& out,
                                     std::string& error) noexcept;

}  // namespace hftrec::gui::viewer