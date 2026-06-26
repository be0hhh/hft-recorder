#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace hftrec::gui::viewer {

struct RateLimitUsagePoint {
    std::int64_t tsNs{0};
    std::uint32_t legIndex{0u};
    std::int64_t minRemainingPctE4{0};
    std::int64_t remaining{0};
    std::int64_t limit{0};
    std::uint8_t bucketKind{0u};
    std::uint64_t intervalNs{0u};
    bool exhausted{false};
};

struct RateLimitUsageData {
    std::vector<RateLimitUsagePoint> points{};
    std::uint32_t legCount{0u};

    bool empty() const noexcept { return points.empty(); }
};

bool loadRateLimitUsageFromResult(const std::filesystem::path& path,
                                  RateLimitUsageData& out,
                                  std::string& error) noexcept;

std::int64_t rateLimitMinRemainingPctE4At(const RateLimitUsageData& usage,
                                          std::int64_t tsNs) noexcept;

}  // namespace hftrec::gui::viewer
