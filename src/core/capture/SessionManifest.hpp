#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/common/Integrity.hpp"
#include "core/common/Status.hpp"

namespace hftrec::capture {

inline constexpr std::int32_t kManifestSchemaVersionCurrent = 3;
inline constexpr std::int32_t kCorpusSchemaVersionCurrent = 3;
inline constexpr std::string_view kCaptureContractVersionCurrent =
    "hftrec.captured_arrival_rows_json.v4";
inline constexpr std::string_view kTradesRowSchemaCurrent =
    "cxet_trade_captured_arrival_v1";
inline constexpr std::string_view kLiquidationsRowSchemaCurrent =
    "cxet_liquidation_captured_arrival_v1";
inline constexpr std::string_view kBookTickerRowSchemaCurrent =
    "cxet_bookticker_captured_arrival_v1";
inline constexpr std::string_view kDepthRowSchemaCurrent =
    "cxet_orderbook_tape_rle_captured_arrival_v1";
inline constexpr std::string_view kCandlesRowSchemaCurrent =
    "cxet_candle_captured_arrival_v1";
inline constexpr std::string_view kMarkPriceRowSchemaCurrent =
    "cxet_mark_price_captured_arrival_v1";
inline constexpr std::string_view kIndexPriceRowSchemaCurrent =
    "cxet_index_price_captured_arrival_v1";
inline constexpr std::string_view kFundingRowSchemaCurrent =
    "cxet_funding_captured_arrival_v1";
inline constexpr std::string_view kPriceLimitRowSchemaCurrent =
    "cxet_price_limit_captured_arrival_v1";

struct ChannelRuntimeHealth {
    std::string state{"not_requested"};
    bool required{false};
    std::int64_t firstRowNs{0};
    std::int64_t lastRowNs{0};
    std::uint64_t reconnectCount{0};
    std::uint64_t droppedEventCount{0};
    std::uint64_t unroutableEventCount{0};
    std::string lastError{};
};

struct ArrivalClockSummary {
    std::string boundary{"hft-parser.application-frame-ready"};
    std::string realtimeClock{"CLOCK_REALTIME"};
    std::string monotonicClock{"CLOCK_MONOTONIC"};
    std::uint64_t capturedRows{0};
    std::uint64_t historicalRows{0};
    std::uint64_t unavailableRows{0};
    std::uint64_t realtimeRegressions{0};
    std::uint64_t monotonicNonIncreasing{0};
    std::uint64_t exchangeAheadOfReceive{0};
    std::uint64_t exchangeTimestampMissing{0};
    std::int64_t firstReceiveRealtimeNs{0};
    std::int64_t lastReceiveRealtimeNs{0};
    std::uint64_t firstReceiveMonotonicNs{0};
    std::uint64_t lastReceiveMonotonicNs{0};
};

struct SessionManifest {
    std::string sessionId;
    std::string exchange;
    std::string market;
    std::vector<std::string> symbols;
    std::string storageSymbol;
    std::int32_t manifestSchemaVersion{kManifestSchemaVersionCurrent};
    std::int32_t corpusSchemaVersion{kCorpusSchemaVersionCurrent};
    std::string captureContractVersion{kCaptureContractVersionCurrent};
    std::string sessionStatus{"complete"};
    std::string selectedParentDir;
    std::string instrumentMetadataPath{"instrument_metadata.json"};
    std::string sessionAuditPath{"reports/session_audit.json"};
    std::string loaderDiagnosticsPath{"reports/loader_diagnostics.json"};
    std::string marketDataLaunchPath{"reports/market_data_launch.json"};
    std::int64_t startedAtNs{0};
    std::int64_t endedAtNs{0};
    std::int64_t targetDurationSec{0};
    std::int64_t actualDurationSec{0};
    std::int64_t snapshotIntervalSec{60};
    bool structurallyLoadable{true};
    std::vector<std::string> structuralBlockers;
    bool tradesEnabled{false};
    bool liquidationsEnabled{false};
    bool bookTickerEnabled{false};
    bool orderbookEnabled{false};
    bool candlesEnabled{false};
    bool candles2Enabled{false};
    bool markPriceEnabled{false};
    bool indexPriceEnabled{false};
    bool fundingEnabled{false};
    bool priceLimitEnabled{false};
    bool tradesRequiredWhenEnabled{true};
    bool liquidationsRequiredWhenEnabled{false};
    bool bookTickerRequiredWhenEnabled{true};
    bool orderbookRequiredWhenEnabled{true};
    bool candlesRequiredWhenEnabled{false};
    bool candles2RequiredWhenEnabled{false};
    bool markPriceRequiredWhenEnabled{false};
    bool indexPriceRequiredWhenEnabled{false};
    bool fundingRequiredWhenEnabled{false};
    bool priceLimitRequiredWhenEnabled{false};
    std::string tradesPath{"jsonl/trades.jsonl"};
    std::string liquidationsPath{"jsonl/liquidations.jsonl"};
    std::string bookTickerPath{"jsonl/bookticker.jsonl"};
    std::string depthPath{"jsonl/depth_tape.jsonl"};
    std::string depthSidecarPath{"jsonl/depth_sidecar.jsonl"};
    std::string candlesPath{"jsonl/candles.jsonl"};
    std::string candles2Path{"jsonl/candles2.jsonl"};
    std::string markPricePath{"jsonl/mark_price.jsonl"};
    std::string indexPricePath{"jsonl/index_price.jsonl"};
    std::string fundingPath{"jsonl/funding.jsonl"};
    std::string priceLimitPath{"jsonl/price_limit.jsonl"};
    std::string tradesRowSchema{kTradesRowSchemaCurrent};
    std::string liquidationsRowSchema{kLiquidationsRowSchemaCurrent};
    std::string bookTickerRowSchema{kBookTickerRowSchemaCurrent};
    std::string depthRowSchema{kDepthRowSchemaCurrent};
    std::string candlesRowSchema{kCandlesRowSchemaCurrent};
    std::string candles2RowSchema{kCandlesRowSchemaCurrent};
    std::string markPriceRowSchema{kMarkPriceRowSchemaCurrent};
    std::string indexPriceRowSchema{kIndexPriceRowSchemaCurrent};
    std::string fundingRowSchema{kFundingRowSchemaCurrent};
    std::string priceLimitRowSchema{kPriceLimitRowSchemaCurrent};
    std::vector<std::string> canonicalArtifacts{};
    std::vector<std::string> supportArtifacts{};
    std::uint64_t tradesCount{0};
    std::uint64_t liquidationsCount{0};
    std::uint64_t bookTickerCount{0};
    std::uint64_t markPriceCount{0};
    std::uint64_t indexPriceCount{0};
    std::uint64_t fundingCount{0};
    std::uint64_t priceLimitCount{0};
    std::uint64_t depthCount{0};
    std::uint64_t candlesCount{0};
    std::uint64_t candles2Count{0};
    ChannelRuntimeHealth tradesRuntime{};
    ChannelRuntimeHealth liquidationsRuntime{};
    ChannelRuntimeHealth bookTickerRuntime{};
    ChannelRuntimeHealth depthRuntime{};
    ChannelRuntimeHealth markPriceRuntime{};
    ChannelRuntimeHealth indexPriceRuntime{};
    ChannelRuntimeHealth fundingRuntime{};
    ChannelRuntimeHealth priceLimitRuntime{};
    ArrivalClockSummary arrivalClock{};
    std::int64_t tradesHistoryWarmupSec{0};
    std::int64_t tradesHistoryRequestedStartNs{0};
    std::int64_t tradesHistoryRequestedEndNs{0};
    std::uint64_t tradesHistoryRows{0};
    std::uint64_t tradesHistoryRequests{0};
    std::string tradesHistoryFeedKind{};
    std::string tradesHistoryStatus{};
    SessionHealth sessionHealth{SessionHealth::Clean};
    bool exactReplayEligible{false};
    std::string integrityReportPath{"reports/integrity_report.json"};
    ChannelIntegritySummary tradesIntegrity{};
    ChannelIntegritySummary liquidationsIntegrity{};
    ChannelIntegritySummary bookTickerIntegrity{};
    ChannelIntegritySummary depthIntegrity{};
    ChannelIntegritySummary snapshotIntegrity{};
    std::size_t totalIntegrityIncidents{0};
    IntegritySeverity highestIntegritySeverity{IntegritySeverity::Info};
    std::string warningSummary;
};

std::string renderManifestJson(const SessionManifest& manifest);
Status parseManifestJson(std::string_view document, SessionManifest& manifest) noexcept;
bool isSupportedManifestSchemaVersion(std::int32_t version) noexcept;
bool isSupportedCorpusSchemaVersion(std::int32_t version) noexcept;

}  // namespace hftrec::capture
