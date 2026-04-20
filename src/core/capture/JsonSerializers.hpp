#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cxet {
namespace composite {
struct TradePublic;
struct BookTickerData;
struct OrderBookSnapshot;
}  // namespace composite
}  // namespace cxet

namespace hftrec::cxet_bridge {
struct CapturedTradeRow;
struct CapturedBookTickerRow;
struct CapturedOrderBookRow;
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
    std::uint64_t anchorUpdateId{0};
    std::uint64_t anchorFirstUpdateId{0};
    bool trustedReplayAnchor{true};
};

std::string renderTradeJsonLine(const cxet::composite::TradePublic& trade,
                                const EventSequenceIds& sequenceIds);

std::string renderTradeJsonLine(const hftrec::cxet_bridge::CapturedTradeRow& trade,
                                const EventSequenceIds& sequenceIds);

std::string renderBookTickerJsonLine(const cxet::composite::BookTickerData& bookTicker,
                                     const std::vector<std::string>& requestedAliases,
                                     const EventSequenceIds& sequenceIds);

std::string renderBookTickerJsonLine(const hftrec::cxet_bridge::CapturedBookTickerRow& bookTicker,
                                     const EventSequenceIds& sequenceIds);

std::string renderDepthJsonLine(const cxet::composite::OrderBookSnapshot& delta,
                                const EventSequenceIds& sequenceIds);

std::string renderDepthJsonLine(const hftrec::cxet_bridge::CapturedOrderBookRow& delta,
                                const EventSequenceIds& sequenceIds);

std::string renderSnapshotJson(const cxet::composite::OrderBookSnapshot& snapshot,
                               const SnapshotProvenance& provenance);

std::string renderSnapshotJson(const hftrec::cxet_bridge::CapturedOrderBookRow& snapshot,
                               const SnapshotProvenance& provenance);

}  // namespace hftrec::capture
