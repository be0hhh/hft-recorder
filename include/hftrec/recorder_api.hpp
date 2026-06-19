#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "hftrec/api.hpp"
#include "hftrec/status.hpp"

namespace hftrec {

using RecorderChannelMask = std::uint32_t;

inline constexpr RecorderChannelMask RecorderChannel_None = 0u;
inline constexpr RecorderChannelMask RecorderChannel_Trades = 1u << 0u;
inline constexpr RecorderChannelMask RecorderChannel_Liquidations = 1u << 1u;
inline constexpr RecorderChannelMask RecorderChannel_BookTicker = 1u << 2u;
inline constexpr RecorderChannelMask RecorderChannel_Depth = 1u << 3u;
inline constexpr RecorderChannelMask RecorderChannel_Candles = 1u << 4u;
inline constexpr RecorderChannelMask RecorderChannel_Snapshot = 1u << 5u;
inline constexpr RecorderChannelMask RecorderChannel_AllMarketData =
    RecorderChannel_Trades |
    RecorderChannel_Liquidations |
    RecorderChannel_BookTicker |
    RecorderChannel_Depth |
    RecorderChannel_Candles |
    RecorderChannel_Snapshot;

struct RecorderLoadRequest {
    std::filesystem::path primarySessionPath{};
    std::filesystem::path secondarySessionPath{};
    RecorderChannelMask channels{RecorderChannel_AllMarketData};
    bool buildTimeline{true};
};

struct RecorderSessionSummary {
    Status status{Status::Unknown};
    std::filesystem::path sessionPath{};
    std::string error{};
    std::uint64_t trades{0};
    std::uint64_t liquidations{0};
    std::uint64_t bookTickers{0};
    std::uint64_t depths{0};
    std::uint64_t candles{0};
    std::uint64_t snapshots{0};
    std::uint64_t timelineEvents{0};
    std::uint64_t buckets{0};
    std::int64_t firstTsNs{0};
    std::int64_t lastTsNs{0};
};

struct RecorderTradeRow {
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
    std::uint8_t sideBuy{0};
};

struct RecorderLiquidationRow {
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

struct RecorderBookTickerRow {
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

struct RecorderCandleRow {
    std::int64_t tier{0};
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
};

struct RecorderPriceLevel {
    std::int64_t priceE8{0};
    std::int64_t qtyE8{0};
    std::int64_t side{0};
};

struct RecorderDepthRow {
    std::int64_t tsNs{0};
    std::vector<RecorderPriceLevel> levels{};
};

struct RecorderSnapshotDocument {
    std::int64_t tsNs{0};
    std::vector<RecorderPriceLevel> levels{};
};

enum class RecorderEventKind : std::uint8_t {
    Depth = 0,
    Trade = 1,
    Liquidation = 2,
    BookTicker = 3,
};

struct RecorderTimelineEvent {
    std::int64_t tsNs{0};
    std::int64_t ingestSeq{0};
    std::uint32_t rowIndex{0};
    RecorderEventKind kind{RecorderEventKind::Depth};
};

struct RecorderSession {
    RecorderSessionSummary info{};
    std::vector<RecorderTradeRow> tradeRows{};
    std::vector<RecorderLiquidationRow> liquidationRows{};
    std::vector<RecorderBookTickerRow> bookTickerRows{};
    std::vector<RecorderDepthRow> depthRows{};
    std::vector<RecorderCandleRow> candleRows{};
    RecorderSnapshotDocument snapshot{};
    bool hasSnapshot{false};
    std::vector<RecorderTimelineEvent> timelineRows{};

    void clear() noexcept;
    const RecorderSessionSummary& summary() const noexcept { return info; }
    const std::vector<RecorderTradeRow>& trades() const noexcept { return tradeRows; }
    const std::vector<RecorderLiquidationRow>& liquidations() const noexcept { return liquidationRows; }
    const std::vector<RecorderBookTickerRow>& bookTickers() const noexcept { return bookTickerRows; }
    const std::vector<RecorderDepthRow>& depths() const noexcept { return depthRows; }
    const std::vector<RecorderCandleRow>& candles() const noexcept { return candleRows; }
    const std::vector<RecorderTimelineEvent>& timeline() const noexcept { return timelineRows; }
};

struct RecorderSessionSet {
    RecorderSession primarySession{};
    RecorderSession secondarySession{};
    bool secondaryPresent{false};

    void clear() noexcept;
    bool hasSecondary() const noexcept { return secondaryPresent; }
    const RecorderSession& primary() const noexcept { return primarySession; }
    const RecorderSession& secondary() const noexcept { return secondarySession; }
};

HFTREC_API Status loadRecorderSession(const std::filesystem::path& sessionPath,
                                      RecorderChannelMask channels,
                                      bool buildTimeline,
                                      RecorderSession& out) noexcept;

HFTREC_API Status loadRecorderSessions(const RecorderLoadRequest& request,
                                       RecorderSessionSet& out) noexcept;

inline Status loadRecorderSession(const std::filesystem::path& sessionPath,
                                  RecorderSession& out) noexcept {
    return loadRecorderSession(sessionPath, RecorderChannel_AllMarketData, true, out);
}

inline Status loadRecorderSession(const std::filesystem::path& sessionPath,
                                  RecorderChannelMask channels,
                                  RecorderSession& out) noexcept {
    return loadRecorderSession(sessionPath, channels, true, out);
}

inline Status loadRecorderSessions(const std::filesystem::path& primarySessionPath,
                                   const std::filesystem::path& secondarySessionPath,
                                   RecorderChannelMask channels,
                                   bool buildTimeline,
                                   RecorderSessionSet& out) noexcept {
    RecorderLoadRequest request{};
    request.primarySessionPath = primarySessionPath;
    request.secondarySessionPath = secondarySessionPath;
    request.channels = channels;
    request.buildTimeline = buildTimeline;
    return loadRecorderSessions(request, out);
}

inline Status loadRecorderSessions(const std::filesystem::path& primarySessionPath,
                                   const std::filesystem::path& secondarySessionPath,
                                   RecorderChannelMask channels,
                                   RecorderSessionSet& out) noexcept {
    return loadRecorderSessions(primarySessionPath, secondarySessionPath, channels, true, out);
}

inline Status loadRecorderSessions(const std::filesystem::path& primarySessionPath,
                                   const std::filesystem::path& secondarySessionPath,
                                   RecorderSessionSet& out) noexcept {
    return loadRecorderSessions(primarySessionPath,
                                secondarySessionPath,
                                RecorderChannel_AllMarketData,
                                true,
                                out);
}

}  // namespace hftrec
