#pragma once

#include <cstdint>
#include <vector>

namespace hftrec::replay {

// Plain row types produced by JsonLineParser. Mirrors the fields
// emitted by capture/JsonSerializers.cpp so the viewer sees the same
// values the capture wrote.

struct TradeRow {
    std::int64_t tsNs{0};
    std::int64_t id{0};
    std::int64_t priceE8{0};
    std::int64_t qtyE8{0};
    std::int64_t eventIndex{0};
    std::uint8_t sideBuy{0};  // 1 = taker-buy, 0 = taker-sell
};

struct BookTickerRow {
    std::int64_t tsNs{0};
    std::int64_t bidPriceE8{0};
    std::int64_t bidQtyE8{0};
    std::int64_t askPriceE8{0};
    std::int64_t askQtyE8{0};
    std::int64_t eventIndex{0};
};

struct PricePair {
    std::int64_t priceE8{0};
    std::int64_t qtyE8{0};
};

struct DepthRow {
    std::int64_t tsNs{0};
    std::int64_t firstUpdateId{0};
    std::int64_t finalUpdateId{0};
    std::int64_t eventIndex{0};
    std::vector<PricePair> bids;
    std::vector<PricePair> asks;
};

struct SnapshotDocument {
    std::int64_t tsNs{0};
    std::int64_t snapshotIndex{0};
    std::vector<PricePair> bids;
    std::vector<PricePair> asks;
};

}  // namespace hftrec::replay
