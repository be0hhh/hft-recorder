#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hftrec::replay {

// Plain row types produced by JsonLineParser. Mirrors the fields
// emitted by capture/JsonSerializers.cpp so the viewer sees the same
// values the capture wrote.

enum EventArrivalFlags : std::uint32_t {
    EventArrivalNone = 0u,
    // Both clocks were sampled by hft-parser when the complete application
    // frame/message became available to the parser. This is the only arrival
    // class eligible for strategy-visible captured-delivery replay.
    EventArrivalApplicationFrame = 1u << 0u,
    // REST/archive seed data has venue time but no live receive instant. Such
    // rows may seed indicators, but must never enter the live delivery plane.
    EventArrivalHistoricalBackfill = 1u << 1u,
    // Preserved evidence: CLOCK_REALTIME moved backwards relative to the
    // preceding record in the same parser shard.
    EventArrivalRealtimeRegression = 1u << 2u,
    // Preserved evidence: CLOCK_MONOTONIC did not advance relative to the
    // preceding record in the same parser shard. Exact duplicate instants are
    // ordered by shardSequence/eventOrdinal and are not fabricated forward.
    EventArrivalMonotonicNonIncreasing = 1u << 3u,
    // Preserved evidence: venue time is later than local realtime receive
    // time. This may be exchange clock skew and is not silently clamped in
    // the corpus.
    EventArrivalExchangeAheadOfReceive = 1u << 4u,
    // The venue did not supply a usable exchange timestamp. Arrival replay is
    // still possible, but venue-plane scheduling cannot claim exactness.
    EventArrivalExchangeTimestampMissing = 1u << 5u,
};

inline constexpr std::uint32_t kEventArrivalKnownFlags =
    EventArrivalApplicationFrame | EventArrivalHistoricalBackfill |
    EventArrivalRealtimeRegression | EventArrivalMonotonicNonIncreasing |
    EventArrivalExchangeAheadOfReceive | EventArrivalExchangeTimestampMissing;

struct EventArrival {
    std::int64_t receiveRealtimeNs{0};
    std::uint64_t receiveMonotonicNs{0};
    std::uint64_t producerEpoch{0};
    std::uint64_t sourceGeneration{0};
    std::uint64_t sessionEpoch{0};
    std::uint64_t frameSequence{0};
    // Total order owned by one parser shard. Cross-shard ties remain honest
    // ties and are resolved deterministically by shardId.
    std::uint64_t shardSequence{0};
    std::uint32_t sourceId{0};
    std::uint16_t shardId{0};
    std::uint16_t eventOrdinal{0};
    std::uint32_t flags{EventArrivalNone};
};

[[nodiscard]] inline constexpr bool hasCapturedApplicationArrival(
    const EventArrival& arrival) noexcept {
    return (arrival.flags & EventArrivalApplicationFrame) != 0u &&
        (arrival.flags & EventArrivalHistoricalBackfill) == 0u &&
        (arrival.flags & ~kEventArrivalKnownFlags) == 0u &&
        arrival.receiveRealtimeNs > 0 && arrival.receiveMonotonicNs != 0u &&
        arrival.producerEpoch != 0u && arrival.sourceGeneration != 0u &&
        arrival.sessionEpoch != 0u && arrival.frameSequence != 0u &&
        arrival.shardSequence != 0u && arrival.sourceId != 0u;
}

[[nodiscard]] inline constexpr bool isHistoricalBackfill(
    const EventArrival& arrival) noexcept {
    return (arrival.flags & EventArrivalHistoricalBackfill) != 0u &&
        (arrival.flags & ~(EventArrivalHistoricalBackfill |
                           EventArrivalExchangeTimestampMissing)) == 0u &&
        arrival.receiveRealtimeNs == 0 && arrival.receiveMonotonicNs == 0u &&
        arrival.producerEpoch == 0u && arrival.sourceGeneration == 0u &&
        arrival.sessionEpoch == 0u && arrival.frameSequence == 0u &&
        arrival.shardSequence == 0u && arrival.sourceId == 0u &&
        arrival.shardId == 0u && arrival.eventOrdinal == 0u;
}

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
    EventArrival arrival{};
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
    EventArrival arrival{};
};

struct BookTickerRow {
    std::uint64_t eventId{0};
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
    EventArrival arrival{};
};

struct CandleRow {
    std::int64_t tier{0};  // 1 = M1, 2 = M15, 3 = D1
    std::int64_t tsNs{0};
    std::string exchange{};
    std::string market{};
    std::string symbol{};
    std::string timeframe{};
    std::int64_t durationNs{0};
    std::int64_t openE8{0};
    std::int64_t highE8{0};
    std::int64_t lowE8{0};
    std::int64_t closeE8{0};
    std::int64_t volumeE8{0};
    std::int64_t quoteAmountE8{0};
    bool hasOhlc{false};
    std::int64_t captureSeq{0};
    std::int64_t ingestSeq{0};
    EventArrival arrival{};
};

struct MarkPriceRow {
    std::int64_t tsNs{0};
    std::int64_t markPriceE8{0};
    std::int64_t captureSeq{0};
    std::int64_t ingestSeq{0};
    EventArrival arrival{};
};

struct IndexPriceRow {
    std::int64_t tsNs{0};
    std::int64_t indexPriceE8{0};
    std::int64_t captureSeq{0};
    std::int64_t ingestSeq{0};
    EventArrival arrival{};
};

struct FundingRow {
    std::int64_t tsNs{0};
    std::int64_t fundingRateE8{0};
    std::int64_t fundingTsNs{0};
    std::int64_t nextFundingTsNs{0};
    std::int64_t captureSeq{0};
    std::int64_t ingestSeq{0};
    EventArrival arrival{};
};

struct PriceLimitRow {
    std::int64_t tsNs{0};
    std::int64_t buyLimitE8{0};
    std::int64_t sellLimitE8{0};
    std::uint8_t enabled{0};
    std::int64_t captureSeq{0};
    std::int64_t ingestSeq{0};
    EventArrival arrival{};
};

struct PricePair {
    std::int64_t priceE8{0};
    std::int64_t qtyE8{0};
    std::int64_t side{0};
};

struct DepthRow {
    std::uint64_t eventId{0};
    std::int64_t tsNs{0};
    std::int64_t captureSeq{0};
    std::int64_t ingestSeq{0};
    EventArrival arrival{};
    std::vector<PricePair> levels;
};

struct SnapshotDocument {
    std::int64_t tsNs{0};
    std::vector<PricePair> levels;
};

}  // namespace hftrec::replay
