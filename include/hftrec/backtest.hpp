#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "hftrec/api.hpp"
#include "hftrec/status.hpp"

namespace hftrec {

struct BacktestRunRequest {
    std::filesystem::path sessionPath{};
    std::string requestId{};
    std::string runId{};
    std::string strategy{};
};

struct BacktestProgress {
    std::uint64_t bucketsDone{0};
    std::uint64_t bucketsTotal{0};
    std::uint64_t eventsDone{0};
    std::uint64_t eventsTotal{0};
    std::uint32_t percent{0};
    std::string stage{};
};

struct BacktestRunResult {
    Status status{Status::Unknown};
    std::string requestId{};
    std::string runId{};
    std::string strategy{};
    std::filesystem::path sessionPath{};
    std::filesystem::path resultPath{};
    std::string error{};
    std::uint64_t trades{0};
    std::uint64_t liquidations{0};
    std::uint64_t bookTickers{0};
    std::uint64_t depths{0};
    std::uint64_t candles{0};
    std::uint64_t events{0};
    std::uint64_t buckets{0};
    std::int64_t firstTsNs{0};
    std::int64_t lastTsNs{0};
    std::uint64_t elapsedNs{0};
};

using BacktestProgressCallback = bool (*)(const BacktestProgress& progress, void* userData) noexcept;

HFTREC_API BacktestRunResult runBacktest(const BacktestRunRequest& request,
                                         BacktestProgressCallback progressCallback = nullptr,
                                         void* userData = nullptr) noexcept;

}  // namespace hftrec