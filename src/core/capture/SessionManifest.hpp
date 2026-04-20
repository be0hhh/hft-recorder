#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "core/common/Integrity.hpp"
#include "core/common/Status.hpp"

namespace hftrec::capture {

inline constexpr std::int32_t kManifestSchemaVersionCurrent = 1;
inline constexpr std::int32_t kCorpusSchemaVersionCurrent = 1;
inline constexpr std::int32_t kManifestSchemaVersionLegacyV0 = 0;
inline constexpr std::int32_t kCorpusSchemaVersionLegacyV0 = 0;

struct SessionManifest {
    std::string sessionId;
    std::string exchange;
    std::string market;
    std::vector<std::string> symbols;
    std::int32_t manifestSchemaVersion{kManifestSchemaVersionCurrent};
    std::int32_t corpusSchemaVersion{kCorpusSchemaVersionCurrent};
    std::string captureContractVersion{"hftrec.cxet_capture.v1"};
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
    bool bookTickerEnabled{false};
    bool orderbookEnabled{false};
    bool tradesRequiredWhenEnabled{true};
    bool bookTickerRequiredWhenEnabled{true};
    bool orderbookRequiredWhenEnabled{true};
    std::string tradesPath{"trades.jsonl"};
    std::string bookTickerPath{"bookticker.jsonl"};
    std::string depthPath{"depth.jsonl"};
    std::string tradesRowSchema{"trade_v1"};
    std::string bookTickerRowSchema{"bookticker_v1"};
    std::string depthRowSchema{"depth_v1"};
    std::string snapshotSchema{"orderbook_snapshot_v1"};
    std::vector<std::string> snapshotFiles{};
    std::vector<std::string> canonicalArtifacts{};
    std::vector<std::string> supportArtifacts{};
    std::uint64_t tradesCount{0};
    std::uint64_t bookTickerCount{0};
    std::uint64_t depthCount{0};
    std::uint64_t snapshotCount{0};
    SessionHealth sessionHealth{SessionHealth::Clean};
    bool exactReplayEligible{false};
    std::string integrityReportPath{"reports/integrity_report.json"};
    ChannelIntegritySummary tradesIntegrity{};
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
bool isLegacyManifest(const SessionManifest& manifest) noexcept;

}  // namespace hftrec::capture
