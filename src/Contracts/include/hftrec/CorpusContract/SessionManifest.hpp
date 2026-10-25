#pragma once


#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "hftrec/CorpusContract/Integrity.hpp"
#include "hftrec/Status.hpp"

namespace hftrec::capture {

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
    std::string sessionStatus{"complete"};
    std::string selectedParentDir;
    std::string instrumentMetadataPath{"instrument_metadata.json"};
    std::string sessionAuditPath{"reports/session_audit.json"};
    std::string loaderDiagnosticPath{"reports/loader_diagnostic.json"};
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

}  // namespace hftrec::capture
