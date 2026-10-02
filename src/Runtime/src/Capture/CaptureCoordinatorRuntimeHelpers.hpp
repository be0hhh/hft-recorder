#pragma once

#include "CaptureCoordinator.hpp"
#include "CaptureCoordinatorInternal.hpp"
#include "Bridge/CxetCaptureBridge.hpp"
#include "cxet/Runtime/Reference/ReferenceVenueConfig.hpp"
#include "cxet/Api/History/Candles/CandleHistoryLoader.hpp"
#include "cxet/Api/History/Trades/TradeHistoryLoader.hpp"
#include "cxet/Primitives/Composite/StreamMeta.hpp"
#include "cxet/Primitives/Composite/TieredCandleHistory.hpp"

#include "cxet/Os/TimeDelta.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace hftrec::capture::runtime {

inline constexpr std::uint8_t kRecorderMarketWsLanes = 1u;

inline constexpr std::int64_t kRecordingManifestFlushIntervalNs = 5'000'000'000LL;
inline constexpr std::int64_t kMarketDataLifecyclePollIntervalNs = 250'000'000LL;
inline constexpr std::int64_t kMarketDataStartupFailureGraceNs = 3'000'000'000LL;
inline constexpr std::int64_t kTradesHistoryWarmupMaxSec = 86400;
inline constexpr std::size_t kTradesHistoryWarmupTargetRows = 0u;
inline constexpr std::uint32_t kTradesHistoryWarmupPageLimit = 1000u;
inline constexpr std::uint32_t kDetailedCandlesDefaultLimit = 5000u;
inline constexpr std::uint32_t kDetailedCandlesMaxLimit = 1'000'000u;

struct TradesHistoryWarmupState {
    std::atomic<bool> started{false};
    std::atomic<bool> done{false};
    std::atomic<bool> ok{false};
    std::mutex mutex{};
    std::vector<replay::TradeRow> historyRows{};
    cxet::api::trades::HistoricalTradesResult result{};
    std::string error{};
    std::int64_t requestedStartNs{0};
    std::int64_t requestedEndNs{0};
};

struct TradesHistorySinkContext {
    TradesHistoryWarmupState* state{nullptr};
    std::atomic<std::uint64_t>* tradesCaptureSeq{nullptr};
    std::atomic<std::uint64_t>* ingestSeq{nullptr};
    std::string exchange{};
    std::string market{};
    std::string identitySymbol{};
    std::size_t maxRows{0u};
    bool hitRowLimit{false};
};

struct EventSequenceIds {
    std::uint64_t captureSeq{0u};
    std::uint64_t ingestSeq{0u};
};

void copySymbolFromText(Symbol& out, std::string_view text) noexcept;
std::int64_t candleTierFromTimeframe(std::string_view timeframe) noexcept;
std::string detailedCandlesRelativePath(std::string_view timeframe, bool detailed);
EventSequenceIds nextEventSequenceIds(std::atomic<std::uint64_t>& channelCounter,
                                      std::atomic<std::uint64_t>& ingestCounter) noexcept;
replay::TradeRow makeTradeRow(const cxet_bridge::CapturedTradeRow& trade,
                              std::string_view exchange,
                              std::string_view market,
                              const EventSequenceIds& sequenceIds) noexcept;

replay::TradeRow makeHistoricalTradeRow(const cxet::composite::Trade& trade,
                                        std::string_view exchange,
                                        std::string_view market,
                                        std::string_view identitySymbol,
                                        const EventSequenceIds& sequenceIds);
bool tradeLessByEventTime(const replay::TradeRow& lhs, const replay::TradeRow& rhs) noexcept;
bool sameTradeEvent(const replay::TradeRow& lhs, const replay::TradeRow& rhs) noexcept;
const char* historicalTradeFeedKindName(cxet::api::trades::HistoricalTradeFeedKind kind) noexcept;
const char* historicalTradesStatusName(cxet::api::trades::HistoricalTradesStatus status) noexcept;
bool appendHistoricalTradesToWarmup(void* userData,
                                    const cxet::composite::Trade* rows,
                                    std::size_t rowCount) noexcept;

replay::BookTickerRow makeBookTickerRow(const cxet_bridge::CapturedBookTickerRow& bookTicker,
                                        std::string_view exchange,
                                        std::string_view market,
                                        const EventSequenceIds& sequenceIds) noexcept;





std::vector<replay::PricePair> makePricePairs(const std::vector<cxet_bridge::CapturedLevel>& levels);
std::vector<replay::PricePair> makeOrderbookLevels(const cxet_bridge::CapturedOrderBookRow& depth);
replay::DepthRow makeDepthRow(const cxet_bridge::CapturedOrderBookRow& depth);

replay::SnapshotDocument makeSnapshotDocument(const cxet_bridge::CapturedOrderBookRow& snapshot);
bool sleepCaptureStopAware(const std::atomic<bool>* stopRequested, unsigned delayMs) noexcept;






bool textEqualsAscii(std::string_view lhs, std::string_view rhs) noexcept;
bool detailedCandlesNeedInstrumentMetadata(const CaptureConfig& config) noexcept;

cxet::runtime::reference::ReferenceVenueConfig makeReferenceVenueConfig(const CaptureConfig& config);
bool fetchDetailedCandlesRows(const CaptureConfig& config,
                              std::vector<cxet::composite::Ohlcv>& rows,
                              std::size_t& rowCount,
                              std::string& tfText,
                              std::string& errorText) noexcept;
Status detailedCandlesFetchStatus(std::string_view errorText) noexcept;







std::string candleHistoryStatusText(const cxet::composite::TieredCandleHistory& history);

}  // namespace hftrec::capture::runtime
