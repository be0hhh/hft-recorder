#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hftrec::replay {
struct TradeRow;
struct BookTickerRow;
struct DepthRow;
struct SnapshotDocument;
}

namespace hftrec::capture {

struct EventSequenceIds {
    std::uint64_t captureSeq{0};
    std::uint64_t ingestSeq{0};
};

struct SnapshotProvenance {
    EventSequenceIds sequence{};
    std::string snapshotKind{"initial"};
    std::string source{"unknown"};
    std::string exchange{};
    std::string market{};
    std::string symbol{};
    std::int64_t sourceTsNs{0};
    std::int64_t ingestTsNs{0};
    bool hasAnchorUpdateId{false};
    bool hasAnchorFirstUpdateId{false};
    std::uint64_t anchorUpdateId{0};
    std::uint64_t anchorFirstUpdateId{0};
    bool trustedReplayAnchor{true};
};

std::string renderTradeJsonLine(const hftrec::replay::TradeRow& trade);
std::string renderTradeJsonLine(const hftrec::replay::TradeRow& trade,
                                const std::vector<std::string>& aliases);

std::string renderBookTickerJsonLine(const hftrec::replay::BookTickerRow& bookTicker);
std::string renderBookTickerJsonLine(const hftrec::replay::BookTickerRow& bookTicker,
                                     const std::vector<std::string>& aliases);

std::string renderDepthJsonLine(const hftrec::replay::DepthRow& delta);
std::string renderDepthJsonLine(const hftrec::replay::DepthRow& delta,
                                const std::vector<std::string>& aliases);

std::string renderSnapshotJson(const hftrec::replay::SnapshotDocument& snapshot);

}  // namespace hftrec::capture
