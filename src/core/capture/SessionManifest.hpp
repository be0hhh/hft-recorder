#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hftrec::capture {

struct SessionManifest {
    std::string sessionId;
    std::string exchange;
    std::string market;
    std::vector<std::string> symbols;
    std::string selectedParentDir;
    std::int64_t startedAtNs{0};
    std::int64_t endedAtNs{0};
    std::int64_t targetDurationSec{0};
    std::int64_t actualDurationSec{0};
    std::int64_t snapshotIntervalSec{60};
    bool tradesEnabled{false};
    bool bookTickerEnabled{false};
    bool orderbookEnabled{false};
    std::uint64_t tradesCount{0};
    std::uint64_t bookTickerCount{0};
    std::uint64_t depthCount{0};
    std::uint64_t snapshotCount{0};
    std::string warningSummary;
};

std::string renderManifestJson(const SessionManifest& manifest);

}  // namespace hftrec::capture
