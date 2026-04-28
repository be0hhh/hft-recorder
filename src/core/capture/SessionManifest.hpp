#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/common/Integrity.hpp"
#include "core/common/Status.hpp"

namespace hftrec::capture {

inline constexpr std::int32_t kManifestSchemaVersionCurrent = 1;
inline constexpr std::int32_t kCorpusSchemaVersionCurrent = 2;

struct SessionManifest {
    std::string sessionId;
    std::string exchange;
    std::string market;
    std::vector<std::string> symbols;
    std::int32_t manifestSchemaVersion{kManifestSchemaVersionCurrent};
    std::int32_t corpusSchemaVersion{kCorpusSchemaVersionCurrent};
    std::string captureContractVersion{"hftrec.strict_canonical_rows_json.v1"};
    std::string sessionStatus{"complete"};
    std::string selectedParentDir;
    std::string instrumentMetadataPath{"instrument_metadata.json"};
    std::string sessionAuditPath{"reports/session_audit.json"};
    std::string loaderDiagnosticsPath{"reports/loader_diagnostics.json"};
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
    bool tradesRequiredWhenEnabled{true};
    bool liquidationsRequiredWhenEnabled{true};
    bool bookTickerRequiredWhenEnabled{true};
    bool orderbookRequiredWhenEnabled{true};
    std::string tradesPath{"jsonl/trades.jsonl"};
    std::string liquidationsPath{"jsonl/liquidations.jsonl"};
    std::string bookTickerPath{"jsonl/bookticker.jsonl"};
    std::string depthPath{"jsonl/depth.jsonl"};
    std::string tradesRowSchema{"cxet_trade_strict_v1"};
    std::string liquidationsRowSchema{"cxet_liquidation_alias_first_v1"};
    std::string bookTickerRowSchema{"cxet_bookticker_strict_v1"};
    std::string depthRowSchema{"cxet_orderbook_flat_levels_v1"};
    std::string snapshotSchema{"cxet_orderbook_snapshot_flat_levels_v1"};
    std::vector<std::string> snapshotFiles{};
    std::vector<std::string> canonicalArtifacts{};
    std::vector<std::string> supportArtifacts{};
    std::uint64_t tradesCount{0};
    std::uint64_t liquidationsCount{0};
    std::uint64_t bookTickerCount{0};
    std::uint64_t depthCount{0};
    std::uint64_t snapshotCount{0};
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
