#pragma once

#include "core/capture/SessionManifest.hpp"
#include "core/replay/EventRows.hpp"

#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>

namespace hftrec::test_support {

inline constexpr std::int64_t kReceiveRealtimeBaseNs =
    1'900'000'000'000'000'000LL;
inline constexpr std::uint64_t kReceiveMonotonicBaseNs =
    900'000'000'000'000'000ULL;

struct ChannelCounts {
    std::uint64_t trades{0u};
    std::uint64_t liquidations{0u};
    std::uint64_t bookTickers{0u};
    std::uint64_t markPrices{0u};
    std::uint64_t indexPrices{0u};
    std::uint64_t fundings{0u};
    std::uint64_t priceLimits{0u};
    std::uint64_t depth{0u};
    std::uint64_t candles{0u};
    std::uint64_t candles2{0u};
};

inline std::uint64_t capturedCount(const ChannelCounts& counts) noexcept {
    return counts.trades + counts.liquidations + counts.bookTickers +
        counts.markPrices + counts.indexPrices + counts.fundings +
        counts.priceLimits + counts.depth;
}

inline std::uint64_t historicalCount(const ChannelCounts& counts) noexcept {
    return counts.candles + counts.candles2;
}

inline replay::EventArrival applicationArrival(
    std::uint64_t sequence,
    std::int64_t exchangeTimestampNs) noexcept {
    replay::EventArrival out{};
    out.receiveRealtimeNs = kReceiveRealtimeBaseNs +
        static_cast<std::int64_t>(sequence * 1'000u);
    out.receiveMonotonicNs = kReceiveMonotonicBaseNs + sequence * 1'000u;
    out.producerEpoch = 1u;
    out.sourceGeneration = 1u;
    out.sessionEpoch = 1u;
    out.frameSequence = sequence;
    out.shardSequence = sequence;
    out.sourceId = 1u;
    out.flags = replay::EventArrivalApplicationFrame;
    if (exchangeTimestampNs > out.receiveRealtimeNs)
        out.flags |= replay::EventArrivalExchangeAheadOfReceive;
    return out;
}

inline std::string arrivalTail(std::uint64_t sequence,
                               std::int64_t exchangeTimestampNs) {
    const auto arrival = applicationArrival(sequence, exchangeTimestampNs);
    return std::to_string(arrival.receiveRealtimeNs) + "," +
        std::to_string(arrival.receiveMonotonicNs) + "," +
        std::to_string(arrival.producerEpoch) + "," +
        std::to_string(arrival.sourceGeneration) + "," +
        std::to_string(arrival.sessionEpoch) + "," +
        std::to_string(arrival.frameSequence) + "," +
        std::to_string(arrival.shardSequence) + "," +
        std::to_string(arrival.sourceId) + ",0,0," +
        std::to_string(arrival.flags);
}

inline capture::SessionManifest manifest(
    const ChannelCounts& counts,
    std::string exchange = "binance",
    std::string market = "futures_usd",
    std::string symbol = "BTC_USDT") {
    capture::SessionManifest out{};
    out.sessionId = "captured-arrival-test";
    out.exchange = std::move(exchange);
    out.market = std::move(market);
    out.symbols = {std::move(symbol)};
    out.storageSymbol = out.symbols.front();
    out.sessionStatus = "complete";
    out.startedAtNs = kReceiveRealtimeBaseNs;
    const std::uint64_t captured = capturedCount(counts);
    out.endedAtNs = kReceiveRealtimeBaseNs +
        static_cast<std::int64_t>((captured + 1u) * 1'000u);
    out.structurallyLoadable = true;
    out.sessionHealth = SessionHealth::Clean;
    out.exactReplayEligible = captured != 0u;

    out.tradesEnabled = counts.trades != 0u;
    out.tradesCount = counts.trades;
    out.liquidationsEnabled = counts.liquidations != 0u;
    out.liquidationsCount = counts.liquidations;
    out.bookTickerEnabled = counts.bookTickers != 0u;
    out.bookTickerCount = counts.bookTickers;
    out.markPriceEnabled = counts.markPrices != 0u;
    out.markPriceCount = counts.markPrices;
    out.indexPriceEnabled = counts.indexPrices != 0u;
    out.indexPriceCount = counts.indexPrices;
    out.fundingEnabled = counts.fundings != 0u;
    out.fundingCount = counts.fundings;
    out.priceLimitEnabled = counts.priceLimits != 0u;
    out.priceLimitCount = counts.priceLimits;
    out.orderbookEnabled = counts.depth != 0u;
    out.depthCount = counts.depth;
    out.candlesEnabled = counts.candles != 0u;
    out.candlesCount = counts.candles;
    out.candles2Enabled = counts.candles2 != 0u;
    out.candles2Count = counts.candles2;

    const auto live = [](capture::ChannelRuntimeHealth& health,
                         bool enabled) noexcept {
        if (!enabled) return;
        health.state = "live";
        health.required = true;
    };
    live(out.tradesRuntime, out.tradesEnabled);
    live(out.liquidationsRuntime, out.liquidationsEnabled);
    live(out.bookTickerRuntime, out.bookTickerEnabled);
    live(out.markPriceRuntime, out.markPriceEnabled);
    live(out.indexPriceRuntime, out.indexPriceEnabled);
    live(out.fundingRuntime, out.fundingEnabled);
    live(out.priceLimitRuntime, out.priceLimitEnabled);
    live(out.depthRuntime, out.orderbookEnabled);

    out.arrivalClock.capturedRows = captured;
    out.arrivalClock.historicalRows = historicalCount(counts);
    if (captured != 0u) {
        out.arrivalClock.firstReceiveRealtimeNs = kReceiveRealtimeBaseNs + 1'000;
        out.arrivalClock.lastReceiveRealtimeNs = kReceiveRealtimeBaseNs +
            static_cast<std::int64_t>(captured * 1'000u);
        out.arrivalClock.firstReceiveMonotonicNs = kReceiveMonotonicBaseNs + 1'000u;
        out.arrivalClock.lastReceiveMonotonicNs = kReceiveMonotonicBaseNs +
            captured * 1'000u;
    }
    return out;
}

inline std::string tradeRow(std::int64_t price,
                            std::int64_t qty,
                            std::int64_t side,
                            std::int64_t exchangeTimestampNs,
                            std::uint64_t sequence) {
    return "[" + std::to_string(price) + "," + std::to_string(qty) +
        "," + std::to_string(side) + "," +
        std::to_string(exchangeTimestampNs) + "," +
        std::to_string(sequence) + "," + std::to_string(sequence) +
        "," + std::to_string(sequence) +
        ",0,0,\"BTC_USDT\",\"binance\","
        "\"futures_usd\"," + std::to_string(sequence) + "," +
        std::to_string(sequence) + "," +
        arrivalTail(sequence, exchangeTimestampNs) + "]\n";
}

inline std::string bookTickerRow(std::int64_t bid,
                                 std::int64_t bidQty,
                                 std::int64_t ask,
                                 std::int64_t askQty,
                                 std::int64_t exchangeTimestampNs,
                                 std::uint64_t sequence) {
    return "[" + std::to_string(sequence) + "," + std::to_string(bid) +
        "," + std::to_string(bidQty) + "," + std::to_string(ask) +
        "," + std::to_string(askQty) + "," +
        std::to_string(exchangeTimestampNs) +
        ",\"BTC_USDT\",\"binance\",\"futures_usd\"," +
        std::to_string(sequence) + "," + std::to_string(sequence) + "," +
        arrivalTail(sequence, exchangeTimestampNs) + "]\n";
}

struct DepthLevel {
    std::int64_t price{0};
    std::int64_t qty{0};
    std::int64_t side{0};
};

inline std::uint64_t taggedDepthTimestamp(
    std::int64_t exchangeTimestampNs) noexcept {
    return (1ULL << 63u) |
        static_cast<std::uint64_t>(exchangeTimestampNs);
}

inline std::string depthTapeRow(
    std::int64_t exchangeTimestampNs,
    std::uint64_t sequence,
    std::initializer_list<DepthLevel> levels) {
    std::string out = "[" + std::to_string(sequence) + "," +
        std::to_string(taggedDepthTimestamp(exchangeTimestampNs)) + "," +
        std::to_string(sequence) + "," + std::to_string(sequence) + "," +
        arrivalTail(sequence, exchangeTimestampNs);
    for (const auto& level : levels) {
        out += "," + std::to_string(level.price) + "," +
            std::to_string(level.qty);
    }
    return out + "]\n";
}

inline std::string depthSidecarRow(
    std::int64_t exchangeTimestampNs,
    std::uint64_t sequence,
    std::initializer_list<DepthLevel> levels) {
    std::string out = "[" + std::to_string(sequence) + "," +
        std::to_string(taggedDepthTimestamp(exchangeTimestampNs));
    auto it = levels.begin();
    while (it != levels.end()) {
        const auto side = it->side;
        std::uint64_t count = 0u;
        while (it != levels.end() && it->side == side) {
            ++count;
            ++it;
        }
        out += "," + std::to_string(side) + "," + std::to_string(count);
    }
    return out + "]\n";
}

inline constexpr std::string_view historicalArrivalTail() noexcept {
    return "0,0,0,0,0,0,0,0,0,0,2";
}

inline std::string candleRow(std::int64_t tier,
                             std::int64_t exchangeTimestampNs,
                             std::int64_t price,
                             std::uint64_t sequence) {
    return "[" + std::to_string(tier) + "," +
        std::to_string(exchangeTimestampNs) + "," +
        std::to_string(price) + "," + std::to_string(price) + "," +
        std::to_string(price) + "," + std::to_string(price) +
        ",0,0,1,\"binance\",\"futures_usd\",\"BTC_USDT\",\"1m\","
        "60000000000," + std::to_string(sequence) + "," +
        std::to_string(sequence) + "," +
        std::string{historicalArrivalTail()} + "]\n";
}

}  // namespace hftrec::test_support
