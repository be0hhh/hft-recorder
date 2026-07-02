#pragma once

#include "core/capture/CaptureCoordinator.hpp"
#include "core/capture/CaptureCoordinatorInternal.hpp"
#include "core/cxet_bridge/CxetCaptureBridge.hpp"
#include "hft_trader/runtime/config/RuntimeConfig.hpp"
#include "hft_trader/runtime/history/candles/CandleHistoryLoader.hpp"
#include "hft_trader/runtime/history/trades/TradeHistoryLoader.hpp"
#include "hft_trader/runtime/market/MarketDataRuntime.hpp"
#include "primitives/composite/OrderBookTapeRuntimeV1.hpp"
#include "primitives/composite/StreamMeta.hpp"
#include "primitives/composite/TieredCandleHistory.hpp"

#include "metrics/Probes.hpp"
#include "probes/TimeDelta.hpp"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace hftrec::capture::runtime {

inline constexpr std::int64_t kRecordingManifestFlushIntervalNs = 5'000'000'000LL;
inline constexpr std::int64_t kMarketDataLifecyclePollIntervalNs = 250'000'000LL;
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
void recordCxetLatencyIfEnabled(cxet::metrics::LatencyProbe& probe,
                                TscTick startTsc,
                                bool captureMetrics) noexcept;

replay::TradeRow makeTradeRow(const cxet_bridge::CapturedTradeRow& trade,
                              std::string_view exchange,
                              std::string_view market,
                              const EventSequenceIds& sequenceIds) noexcept;
replay::LiquidationRow makeLiquidationRow(const cxet_bridge::CapturedLiquidationRow& liquidation,
                                          std::string_view exchange,
                                          std::string_view market,
                                          const EventSequenceIds& sequenceIds) noexcept;
replay::TradeRow makeHistoricalTradeRow(const cxet::composite::TradePublic& trade,
                                        std::string_view exchange,
                                        std::string_view market,
                                        std::string_view identitySymbol,
                                        const EventSequenceIds& sequenceIds);
bool tradeLessByEventTime(const replay::TradeRow& lhs, const replay::TradeRow& rhs) noexcept;
bool sameTradeEvent(const replay::TradeRow& lhs, const replay::TradeRow& rhs) noexcept;
const char* historicalTradeFeedKindName(cxet::api::trades::HistoricalTradeFeedKind kind) noexcept;
const char* historicalTradesStatusName(cxet::api::trades::HistoricalTradesStatus status) noexcept;
bool appendHistoricalTradesToWarmup(void* userData,
                                    const cxet::composite::TradePublic* rows,
                                    std::size_t rowCount) noexcept;

replay::BookTickerRow makeBookTickerRow(const cxet_bridge::CapturedBookTickerRow& bookTicker,
                                        std::string_view exchange,
                                        std::string_view market,
                                        const EventSequenceIds& sequenceIds) noexcept;
replay::MarkPriceRow makeMarkPriceRow(const cxet::composite::MarkPriceRuntimeV1& markPrice) noexcept;
replay::IndexPriceRow makeIndexPriceRow(const cxet::composite::IndexPriceRuntimeV1& indexPrice) noexcept;
replay::FundingRow makeFundingRow(const cxet::composite::FundingRuntimeV1& funding) noexcept;
replay::PriceLimitRow makePriceLimitRow(const cxet::composite::PriceLimitRuntimeV1& priceLimit) noexcept;
bool sameFundingTuple(const replay::FundingRow& lhs, const replay::FundingRow& rhs) noexcept;
std::vector<replay::PricePair> makePricePairs(const std::vector<cxet_bridge::CapturedLevel>& levels);
std::vector<replay::PricePair> makeOrderbookLevels(const cxet_bridge::CapturedOrderBookRow& depth);
replay::DepthRow makeDepthRow(const cxet_bridge::CapturedOrderBookRow& depth);
void normalizeFixedDepthSnapshotDelta(replay::DepthRow& row,
                                      std::vector<replay::PricePair>& previousLevels);
replay::SnapshotDocument makeSnapshotDocument(const cxet_bridge::CapturedOrderBookRow& snapshot);
bool sleepCaptureStopAware(const std::atomic<bool>* stopRequested, unsigned delayMs) noexcept;

const char* publicMarketDataStatusName(cxet::api::market::PublicMarketDataStatus status) noexcept;
std::string marketDataRuntimeDiagnosticText(const hft_trader::runtime::MarketDataRuntime& runtime,
                                            std::string_view scope);
void pollMarketDataLifecycleIfDue(hft_trader::runtime::MarketDataRuntime& runtime,
                                  std::int64_t& nextPollNs,
                                  std::string* diagnosticOut = nullptr,
                                  std::string_view scope = {});
bool textEqualsAscii(std::string_view lhs, std::string_view rhs) noexcept;
bool detailedCandlesNeedInstrumentMetadata(const CaptureConfig& config) noexcept;
bool shouldFetchInitialOrderbookSnapshot(const CaptureConfig& config) noexcept;
hft_trader::runtime::VenueRuntimeConfig makeTraderVenueConfig(const CaptureConfig& config);
bool fetchDetailedCandlesRows(const CaptureConfig& config,
                              std::vector<cxet::composite::Ohlcv>& rows,
                              std::size_t& rowCount,
                              std::string& tfText,
                              std::string& errorText) noexcept;
Status detailedCandlesFetchStatus(std::string_view errorText) noexcept;
bool traderMarketDataRuntimeAbiMatches(std::uint64_t compiledFingerprint,
                                       std::uint64_t linkedFingerprint,
                                       std::string& err) noexcept;
bool linkedTraderMarketDataRuntimeAbiMatches(std::string& err) noexcept;
bool applyTraderMarketDataConfig(hft_trader::runtime::MarketDataRuntime& runtime,
                                 const CaptureConfig& config,
                                 Span<const cxet::api::market::PublicMarketDataStream> streams,
                                 std::string& err) noexcept;
cxet::composite::StreamObjectType streamObjectTypeForRecorder(
    cxet::api::market::PublicMarketDataStream stream) noexcept;
const char* referenceStreamName(cxet::api::market::PublicMarketDataStream stream) noexcept;
const char* marketStreamName(cxet::api::market::PublicMarketDataStream stream) noexcept;
cxet::composite::StreamMeta streamMetaFromTraderEvent(
    const hft_trader::runtime::MarketDataRuntimeEvent& event,
    std::string_view identitySymbol) noexcept;
std::string candleHistoryStatusText(const cxet::composite::TieredCandleHistory& history);

}  // namespace hftrec::capture::runtime
