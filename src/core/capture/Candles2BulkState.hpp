#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

#include "core/common/Status.hpp"

namespace hftrec::capture {

inline constexpr const char* kCandles2BulkStateRelativePath = "reports/candles2_bulk_state.json";

struct Candles2BulkState {
    std::string status{"pending"};
    std::string exchange{};
    std::string market{};
    std::string symbol{};
    std::string timeframe{};
    std::string candles2Path{};
    std::string compatibilityCandlesPath{};
    std::uint32_t requestedLimit{0};
    std::uint32_t pageLimit{0};
    std::int64_t requestedEndNs{0};
    std::uint64_t cursorEndNs{0};
    std::uint64_t oldestTsNs{0};
    std::uint64_t newestTsNs{0};
    std::uint64_t rowsWritten{0};
    std::uint64_t pagesOk{0};
    std::uint64_t rowsRaw{0};
    std::uint64_t emptyWindowsSkipped{0};
    std::uint64_t callbackStops{0};
    std::string lastError{};
};

std::string renderCandles2BulkStateJson(const Candles2BulkState& state);
Status parseCandles2BulkStateJson(std::string_view document, Candles2BulkState& out) noexcept;
Status writeCandles2BulkStateFile(const std::filesystem::path& sessionDir,
                                  const Candles2BulkState& state,
                                  std::string* error = nullptr) noexcept;

}  // namespace hftrec::capture
