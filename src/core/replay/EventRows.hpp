#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hftrec::replay {

// Plain row types produced by JsonLineParser. Mirrors the fields
// emitted by capture/JsonSerializers.cpp so the viewer sees the same
// values the capture wrote.

struct TradeRow {
    std::uint64_t tradeId{0};
    std::uint64_t firstTradeId{0};
    std::uint64_t lastTradeId{0};
    std::string symbol{};
    std::string exchange{};
    std::string market{};
    std::int64_t tsNs{0};
    std::int64_t captureSeq{0};
    std::int64_t ingestSeq{0};
    std::int64_t priceE8{0};
    std::int64_t qtyE8{0};
    std::int64_t quoteQtyE8{0};
    std::int64_t side{0};
    std::uint8_t isBuyerMaker{0};
    std::uint8_t sideBuy{0};  // 1 = taker-buy, 0 = taker-sell
};

struct LiquidationRow {
    std::string symbol{};
    std::string exchange{};
    std::string market{};
    std::int64_t tsNs{0};
    std::int64_t captureSeq{0};
    std::int64_t ingestSeq{0};
    std::int64_t priceE8{0};
    std::int64_t qtyE8{0};
    std::int64_t avgPriceE8{0};
    std::int64_t filledQtyE8{0};
    std::int64_t side{0};
    std::uint8_t sideBuy{0};
    std::int64_t orderType{0};
    std::int64_t timeInForce{0};
    std::int64_t status{0};
    std::int64_t sourceMode{0};
};

struct BookTickerRow {
    std::string symbol{};
    std::string exchange{};
    std::string market{};
    std::int64_t tsNs{0};
    std::int64_t captureSeq{0};
    std::int64_t ingestSeq{0};
    std::int64_t bidPriceE8{0};
    std::int64_t bidQtyE8{0};
    std::int64_t askPriceE8{0};
    std::int64_t askQtyE8{0};
};

struct PricePair {
    std::int64_t priceE8{0};
    std::int64_t qtyE8{0};
    std::int64_t side{0};
    std::uint64_t levelId{0};
};

struct DepthRow {
    std::string symbol{};
    std::string exchange{};
    std::string market{};
    std::int64_t tsNs{0};
    std::int64_t captureSeq{0};
    std::int64_t ingestSeq{0};
    bool hasUpdateId{false};
    bool hasFirstUpdateId{false};
    std::int64_t updateId{0};
    std::int64_t firstUpdateId{0};
    std::vector<PricePair> bids;
    std::vector<PricePair> asks;
};

struct SnapshotDocument {
    std::int64_t tsNs{0};
    std::int64_t captureSeq{0};
    std::int64_t ingestSeq{0};
    bool hasUpdateId{false};
    bool hasFirstUpdateId{false};
    std::int64_t updateId{0};
    std::int64_t firstUpdateId{0};
    std::string snapshotKind{};
    std::string source{};
    std::string exchange{};
    std::string market{};
    std::string symbol{};
    std::int64_t sourceTsNs{0};
    std::int64_t ingestTsNs{0};
    bool hasAnchorUpdateId{false};
    bool hasAnchorFirstUpdateId{false};
    std::int64_t anchorUpdateId{0};
    std::int64_t anchorFirstUpdateId{0};
    std::uint8_t trustedReplayAnchor{1};
    std::vector<PricePair> bids;
    std::vector<PricePair> asks;
};

}  // namespace hftrec::replay
