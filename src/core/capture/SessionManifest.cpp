#include "core/capture/SessionManifest.hpp"

#include <cstddef>
#include <limits>
#include <sstream>
#include <string>
#include <utility>

#include "core/common/JsonString.hpp"
#include "core/common/MiniJsonParser.hpp"

namespace hftrec::capture {

namespace {

using JsonParser = hftrec::json::MiniJsonParser;

void appendStringArray(std::ostringstream& out, const std::vector<std::string>& values) {
    out << '[';
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) out << ',';
        out << json::quote(values[i]);
    }
    out << ']';
}

const char* boolToString(bool value) noexcept {
    return value ? "true" : "false";
}

bool parseStringArray(JsonParser& parser, std::vector<std::string>& out) noexcept {
    out.clear();
    if (!parser.parseArrayStart()) return false;
    if (parser.peek(']')) return parser.parseArrayEnd();
    do {
        std::string value;
        if (!parser.parseString(value)) return false;
        out.push_back(std::move(value));
        if (parser.peek(']')) break;
    } while (parser.parseComma());
    return parser.parseArrayEnd();
}

bool parseInt32Field(JsonParser& parser, std::int32_t& out) noexcept {
    std::int64_t value = 0;
    if (!parser.parseInt64(value)) return false;
    if (value < std::numeric_limits<std::int32_t>::min() || value > std::numeric_limits<std::int32_t>::max()) {
        return false;
    }
    out = static_cast<std::int32_t>(value);
    return true;
}

bool parseUint64Field(JsonParser& parser, std::uint64_t& out) noexcept {
    std::int64_t value = 0;
    if (!parser.parseInt64(value) || value < 0) return false;
    out = static_cast<std::uint64_t>(value);
    return true;
}

bool parseInt64Field(JsonParser& parser, std::int64_t& out) noexcept {
    return parser.parseInt64(out);
}

bool parseChannelHealthStateString(std::string_view value, ChannelHealthState& out) noexcept {
    if (value == "not_captured") out = ChannelHealthState::NotCaptured;
    else if (value == "missing") out = ChannelHealthState::Missing;
    else if (value == "clean") out = ChannelHealthState::Clean;
    else if (value == "degraded") out = ChannelHealthState::Degraded;
    else if (value == "corrupt") out = ChannelHealthState::Corrupt;
    else return false;
    return true;
}

bool parseSessionHealthString(std::string_view value, SessionHealth& out) noexcept {
    if (value == "clean") out = SessionHealth::Clean;
    else if (value == "degraded") out = SessionHealth::Degraded;
    else if (value == "corrupt") out = SessionHealth::Corrupt;
    else return false;
    return true;
}

bool parseSeverityString(std::string_view value, IntegritySeverity& out) noexcept {
    if (value == "info") out = IntegritySeverity::Info;
    else if (value == "warning") out = IntegritySeverity::Warning;
    else if (value == "error") out = IntegritySeverity::Error;
    else return false;
    return true;
}

bool parseIdentityObject(JsonParser& parser, SessionManifest& manifest) noexcept {
    if (!parser.parseObjectStart()) return false;
    if (parser.peek('}')) return parser.parseObjectEnd();
    std::string key;
    do {
        if (!parser.parseKey(key)) return false;
        if (key == "session_id") {
            if (!parser.parseString(manifest.sessionId)) return false;
        } else if (key == "exchange") {
            if (!parser.parseString(manifest.exchange)) return false;
        } else if (key == "market") {
            if (!parser.parseString(manifest.market)) return false;
        } else if (key == "symbols") {
            if (!parseStringArray(parser, manifest.symbols)) return false;
        } else {
            if (!parser.skipValue()) return false;
        }
        if (parser.peek('}')) break;
    } while (parser.parseComma());
    return parser.parseObjectEnd();
}


bool parseCaptureObject(JsonParser& parser, SessionManifest& manifest) noexcept {
    if (!parser.parseObjectStart()) return false;
    if (parser.peek('}')) return parser.parseObjectEnd();
    std::string key;
    do {
        if (!parser.parseKey(key)) return false;
        if (key == "selected_parent_dir") {
            if (!parser.parseString(manifest.selectedParentDir)) return false;
        } else if (key == "started_at_ns") {
            if (!parseInt64Field(parser, manifest.startedAtNs)) return false;
        } else if (key == "ended_at_ns") {
            if (!parseInt64Field(parser, manifest.endedAtNs)) return false;
        } else if (key == "target_duration_sec") {
            if (!parseInt64Field(parser, manifest.targetDurationSec)) return false;
        } else if (key == "actual_duration_sec") {
            if (!parseInt64Field(parser, manifest.actualDurationSec)) return false;
        } else if (key == "snapshot_interval_sec") {
            if (!parseInt64Field(parser, manifest.snapshotIntervalSec)) return false;
        } else {
            if (!parser.skipValue()) return false;
        }
        if (parser.peek('}')) break;
    } while (parser.parseComma());
    return parser.parseObjectEnd();
}


bool parseReplayObject(JsonParser& parser, SessionManifest& manifest) noexcept {
    if (!parser.parseObjectStart()) return false;
    if (parser.peek('}')) return parser.parseObjectEnd();
    std::string key;
    do {
        if (!parser.parseKey(key)) return false;
        if (key == "structurally_loadable") {
            if (!parser.parseBool(manifest.structurallyLoadable)) return false;
        } else if (key == "structural_blockers") {
            if (!parseStringArray(parser, manifest.structuralBlockers)) return false;
        } else {
            if (!parser.skipValue()) return false;
        }
        if (parser.peek('}')) break;
    } while (parser.parseComma());
    return parser.parseObjectEnd();
}


bool parseChannelObject(JsonParser& parser,
                        bool& enabled,
                        bool& requiredWhenEnabled,
                        std::string& path,
                        std::string& rowSchema,
                        std::uint64_t& count) noexcept {
    if (!parser.parseObjectStart()) return false;
    if (parser.peek('}')) return parser.parseObjectEnd();
    std::string key;
    do {
        if (!parser.parseKey(key)) return false;
        if (key == "enabled") {
            if (!parser.parseBool(enabled)) return false;
        } else if (key == "required_when_enabled") {
            if (!parser.parseBool(requiredWhenEnabled)) return false;
        } else if (key == "path") {
            if (!parser.parseString(path)) return false;
        } else if (key == "row_schema") {
            if (!parser.parseString(rowSchema)) return false;
        } else if (key == "declared_event_count") {
            if (!parseUint64Field(parser, count)) return false;
        } else {
            if (!parser.skipValue()) return false;
        }
        if (parser.peek('}')) break;
    } while (parser.parseComma());
    return parser.parseObjectEnd();
}


bool parseChannelsObject(JsonParser& parser, SessionManifest& manifest) noexcept {
    if (!parser.parseObjectStart()) return false;
    if (parser.peek('}')) return parser.parseObjectEnd();
    std::string key;
    do {
        if (!parser.parseKey(key)) return false;
        if (key == "trades") {
            if (!parseChannelObject(parser,
                                    manifest.tradesEnabled,
                                    manifest.tradesRequiredWhenEnabled,
                                    manifest.tradesPath,
                                    manifest.tradesRowSchema,
                                    manifest.tradesCount)) {
                return false;
            }
        } else if (key == "bookticker") {
            if (!parseChannelObject(parser,
                                    manifest.bookTickerEnabled,
                                    manifest.bookTickerRequiredWhenEnabled,
                                    manifest.bookTickerPath,
                                    manifest.bookTickerRowSchema,
                                    manifest.bookTickerCount)) {
                return false;
            }
        } else if (key == "depth") {
            if (!parseChannelObject(parser,
                                    manifest.orderbookEnabled,
                                    manifest.orderbookRequiredWhenEnabled,
                                    manifest.depthPath,
                                    manifest.depthRowSchema,
                                    manifest.depthCount)) {
                return false;
            }
        } else {
            if (!parser.skipValue()) return false;
        }
        if (parser.peek('}')) break;
    } while (parser.parseComma());
    return parser.parseObjectEnd();
}


bool parseSnapshotsObject(JsonParser& parser, SessionManifest& manifest) noexcept {
    if (!parser.parseObjectStart()) return false;
    if (parser.peek('}')) return parser.parseObjectEnd();
    std::string key;
    do {
        if (!parser.parseKey(key)) return false;
        if (key == "schema") {
            if (!parser.parseString(manifest.snapshotSchema)) return false;
        } else if (key == "files") {
            if (!parseStringArray(parser, manifest.snapshotFiles)) return false;
        } else if (key == "declared_snapshot_count") {
            if (!parseUint64Field(parser, manifest.snapshotCount)) return false;
        } else {
            if (!parser.skipValue()) return false;
        }
        if (parser.peek('}')) break;
    } while (parser.parseComma());
    return parser.parseObjectEnd();
}


bool parseArtifactsObject(JsonParser& parser, SessionManifest& manifest) noexcept {
    if (!parser.parseObjectStart()) return false;
    if (parser.peek('}')) return parser.parseObjectEnd();
    std::string key;
    do {
        if (!parser.parseKey(key)) return false;
        if (key == "canonical") {
            if (!parseStringArray(parser, manifest.canonicalArtifacts)) return false;
        } else if (key == "support") {
            if (!parseStringArray(parser, manifest.supportArtifacts)) return false;
        } else if (key == "instrument_metadata_path") {
            if (!parser.parseString(manifest.instrumentMetadataPath)) return false;
        } else if (key == "session_audit_path") {
            if (!parser.parseString(manifest.sessionAuditPath)) return false;
        } else if (key == "loader_diagnostics_path") {
            if (!parser.parseString(manifest.loaderDiagnosticsPath)) return false;
        } else {
            if (!parser.skipValue()) return false;
        }
        if (parser.peek('}')) break;
    } while (parser.parseComma());
    return parser.parseObjectEnd();
}


bool parseSummaryObject(JsonParser& parser, SessionManifest& manifest) noexcept {
    if (!parser.parseObjectStart()) return false;
    if (parser.peek('}')) return parser.parseObjectEnd();
    std::string key;
    do {
        if (!parser.parseKey(key)) return false;
        if (key == "warning_summary") {
            if (!parser.parseString(manifest.warningSummary)) return false;
        } else {
            if (!parser.skipValue()) return false;
        }
        if (parser.peek('}')) break;
    } while (parser.parseComma());
    return parser.parseObjectEnd();
}


bool parseChannelIntegrityObject(JsonParser& parser, ChannelIntegritySummary& summary) noexcept {
    if (!parser.parseObjectStart()) return false;
    if (parser.peek('}')) return parser.parseObjectEnd();
    std::string key;
    do {
        if (!parser.parseKey(key)) return false;
        if (key == "state") {
            std::string state;
            if (!parser.parseString(state) || !parseChannelHealthStateString(state, summary.state)) return false;
        } else if (key == "exact_replay_eligible") {
            if (!parser.parseBool(summary.exactReplayEligible)) return false;
        } else if (key == "incident_count") {
            std::uint64_t value = 0;
            if (!parseUint64Field(parser, value)) return false;
            summary.incidentCount = static_cast<std::size_t>(value);
        } else if (key == "gap_count") {
            std::uint64_t value = 0;
            if (!parseUint64Field(parser, value)) return false;
            summary.gapCount = static_cast<std::size_t>(value);
        } else if (key == "parse_error_count") {
            std::uint64_t value = 0;
            if (!parseUint64Field(parser, value)) return false;
            summary.parseErrorCount = static_cast<std::size_t>(value);
        } else if (key == "highest_severity") {
            std::string severity;
            if (!parser.parseString(severity) || !parseSeverityString(severity, summary.highestSeverity)) return false;
        } else if (key == "reason_code") {
            if (!parser.parseString(summary.reasonCode)) return false;
        } else if (key == "reason_text") {
            if (!parser.parseString(summary.reasonText)) return false;
        } else {
            if (!parser.skipValue()) return false;
        }
        if (parser.peek('}')) break;
    } while (parser.parseComma());
    return parser.parseObjectEnd();
}


bool parseIntegritySummaryObject(JsonParser& parser, SessionManifest& manifest) noexcept {
    if (!parser.parseObjectStart()) return false;
    if (parser.peek('}')) return parser.parseObjectEnd();
    std::string key;
    do {
        if (!parser.parseKey(key)) return false;
        if (key == "total_incidents") {
            std::uint64_t value = 0;
            if (!parseUint64Field(parser, value)) return false;
            manifest.totalIntegrityIncidents = static_cast<std::size_t>(value);
        } else if (key == "highest_severity") {
            std::string severity;
            if (!parser.parseString(severity) || !parseSeverityString(severity, manifest.highestIntegritySeverity)) return false;
        } else {
            if (!parser.skipValue()) return false;
        }
        if (parser.peek('}')) break;
    } while (parser.parseComma());
    return parser.parseObjectEnd();
}


bool parseChannelIntegrityGroupObject(JsonParser& parser, SessionManifest& manifest) noexcept {
    if (!parser.parseObjectStart()) return false;
    if (parser.peek('}')) return parser.parseObjectEnd();
    std::string key;
    do {
        if (!parser.parseKey(key)) return false;
        if (key == "trades") {
            if (!parseChannelIntegrityObject(parser, manifest.tradesIntegrity)) return false;
        } else if (key == "bookticker") {
            if (!parseChannelIntegrityObject(parser, manifest.bookTickerIntegrity)) return false;
        } else if (key == "depth") {
            if (!parseChannelIntegrityObject(parser, manifest.depthIntegrity)) return false;
        } else if (key == "snapshot") {
            if (!parseChannelIntegrityObject(parser, manifest.snapshotIntegrity)) return false;
        } else {
            if (!parser.skipValue()) return false;
        }
        if (parser.peek('}')) break;
    } while (parser.parseComma());
    return parser.parseObjectEnd();
}



bool isSupportedCaptureContractVersion(std::string_view version) noexcept {
    return version == "hftrec.cxet_prefix_json.v2";
}

bool isSupportedTradesRowSchema(std::string_view schema) noexcept {
    return schema == "cxet_trade_prefix_v2";
}

bool isSupportedBookTickerRowSchema(std::string_view schema) noexcept {
    return schema == "cxet_bookticker_prefix_v2";
}

bool isSupportedDepthRowSchema(std::string_view schema) noexcept {
    return schema == "cxet_orderbook_prefix_v2";
}

bool isSupportedSnapshotSchema(std::string_view schema) noexcept {
    return schema == "cxet_orderbook_snapshot_prefix_v2";
}

void populateCanonicalArtifacts(SessionManifest& manifest) {
    manifest.canonicalArtifacts.clear();
    manifest.canonicalArtifacts.push_back("manifest.json");
    if (!manifest.instrumentMetadataPath.empty()) manifest.canonicalArtifacts.push_back(manifest.instrumentMetadataPath);
    if (manifest.tradesEnabled && !manifest.tradesPath.empty()) manifest.canonicalArtifacts.push_back(manifest.tradesPath);
    if (manifest.bookTickerEnabled && !manifest.bookTickerPath.empty()) manifest.canonicalArtifacts.push_back(manifest.bookTickerPath);
    if (manifest.orderbookEnabled && !manifest.depthPath.empty()) manifest.canonicalArtifacts.push_back(manifest.depthPath);
    for (const auto& snapshotFile : manifest.snapshotFiles) {
        manifest.canonicalArtifacts.push_back(snapshotFile);
    }
}

void populateSupportArtifacts(SessionManifest& manifest) {
    manifest.supportArtifacts.clear();
    if (!manifest.sessionAuditPath.empty()) manifest.supportArtifacts.push_back(manifest.sessionAuditPath);
    if (!manifest.integrityReportPath.empty()) manifest.supportArtifacts.push_back(manifest.integrityReportPath);
    if (!manifest.loaderDiagnosticsPath.empty()) manifest.supportArtifacts.push_back(manifest.loaderDiagnosticsPath);
}

bool validateStructurally(SessionManifest& manifest) {
    manifest.structuralBlockers.clear();
    if (!isSupportedManifestSchemaVersion(manifest.manifestSchemaVersion)) {
        manifest.structuralBlockers.push_back("unsupported manifest schema version");
    }
    if (!isSupportedCorpusSchemaVersion(manifest.corpusSchemaVersion)) {
        manifest.structuralBlockers.push_back("unsupported corpus schema version");
    }
    if (!isSupportedCaptureContractVersion(manifest.captureContractVersion)) {
        manifest.structuralBlockers.push_back("unsupported capture contract version");
    }
    if (manifest.sessionId.empty()) manifest.structuralBlockers.push_back("missing session_id");
    if (manifest.exchange.empty()) manifest.structuralBlockers.push_back("missing exchange");
    if (manifest.market.empty()) manifest.structuralBlockers.push_back("missing market");
    if (manifest.symbols.empty()) manifest.structuralBlockers.push_back("missing symbols");
    if (manifest.tradesEnabled && manifest.tradesRequiredWhenEnabled && manifest.tradesPath.empty()) {
        manifest.structuralBlockers.push_back("missing trades path");
    }
    if (manifest.tradesEnabled && !isSupportedTradesRowSchema(manifest.tradesRowSchema)) {
        manifest.structuralBlockers.push_back("unsupported trades row schema");
    }
    if (manifest.bookTickerEnabled && manifest.bookTickerRequiredWhenEnabled && manifest.bookTickerPath.empty()) {
        manifest.structuralBlockers.push_back("missing bookticker path");
    }
    if (manifest.bookTickerEnabled && !isSupportedBookTickerRowSchema(manifest.bookTickerRowSchema)) {
        manifest.structuralBlockers.push_back("unsupported bookticker row schema");
    }
    if (manifest.orderbookEnabled && manifest.orderbookRequiredWhenEnabled && manifest.depthPath.empty()) {
        manifest.structuralBlockers.push_back("missing depth path");
    }
    if (manifest.orderbookEnabled && !isSupportedDepthRowSchema(manifest.depthRowSchema)) {
        manifest.structuralBlockers.push_back("unsupported depth row schema");
    }
    if (manifest.snapshotCount > 0u && manifest.snapshotFiles.empty()) {
        manifest.structuralBlockers.push_back("missing snapshot file inventory");
    }
    if (manifest.snapshotCount > 0u && !isSupportedSnapshotSchema(manifest.snapshotSchema)) {
        manifest.structuralBlockers.push_back("unsupported snapshot schema");
    }
    manifest.structurallyLoadable = manifest.structuralBlockers.empty();
    return manifest.structurallyLoadable;
}

void appendChannelIntegrity(std::ostringstream& out,
                            std::string_view name,
                            const ChannelIntegritySummary& summary,
                            bool trailingComma) {
    out << "    \"" << name << "\": {\n";
    out << "      \"state\": " << json::quote(std::string{toString(summary.state)}) << ",\n";
    out << "      \"exact_replay_eligible\": " << boolToString(summary.exactReplayEligible) << ",\n";
    out << "      \"incident_count\": " << summary.incidentCount << ",\n";
    out << "      \"gap_count\": " << summary.gapCount << ",\n";
    out << "      \"parse_error_count\": " << summary.parseErrorCount << ",\n";
    out << "      \"highest_severity\": " << json::quote(std::string{toString(summary.highestSeverity)}) << ",\n";
    out << "      \"reason_code\": " << json::quote(summary.reasonCode) << ",\n";
    out << "      \"reason_text\": " << json::quote(summary.reasonText) << "\n";
    out << "    }" << (trailingComma ? "," : "") << "\n";
}

}  // namespace

std::string renderManifestJson(const SessionManifest& manifest) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"manifest_schema_version\": " << manifest.manifestSchemaVersion << ",\n";
    out << "  \"corpus_schema_version\": " << manifest.corpusSchemaVersion << ",\n";
    out << "  \"capture_contract_version\": " << json::quote(manifest.captureContractVersion) << ",\n";
    out << "  \"session_status\": " << json::quote(manifest.sessionStatus) << ",\n";
    out << "  \"identity\": {\n";
    out << "    \"session_id\": " << json::quote(manifest.sessionId) << ",\n";
    out << "    \"exchange\": " << json::quote(manifest.exchange) << ",\n";
    out << "    \"market\": " << json::quote(manifest.market) << ",\n";
    out << "    \"symbols\": ";
    appendStringArray(out, manifest.symbols);
    out << "\n  },\n";
    out << "  \"capture\": {\n";
    out << "    \"selected_parent_dir\": " << json::quote(manifest.selectedParentDir) << ",\n";
    out << "    \"started_at_ns\": " << manifest.startedAtNs << ",\n";
    out << "    \"ended_at_ns\": " << manifest.endedAtNs << ",\n";
    out << "    \"target_duration_sec\": " << manifest.targetDurationSec << ",\n";
    out << "    \"actual_duration_sec\": " << manifest.actualDurationSec << ",\n";
    out << "    \"snapshot_interval_sec\": " << manifest.snapshotIntervalSec << "\n";
    out << "  },\n";
    out << "  \"replay\": {\n";
    out << "    \"structurally_loadable\": " << boolToString(manifest.structurallyLoadable) << ",\n";
    out << "    \"structural_blockers\": ";
    appendStringArray(out, manifest.structuralBlockers);
    out << "\n  },\n";
    out << "  \"channels\": {\n";
    out << "    \"trades\": {\n";
    out << "      \"enabled\": " << boolToString(manifest.tradesEnabled) << ",\n";
    out << "      \"required_when_enabled\": " << boolToString(manifest.tradesRequiredWhenEnabled) << ",\n";
    out << "      \"path\": " << json::quote(manifest.tradesPath) << ",\n";
    out << "      \"row_schema\": " << json::quote(manifest.tradesRowSchema) << ",\n";
    out << "      \"declared_event_count\": " << manifest.tradesCount << "\n";
    out << "    },\n";
    out << "    \"bookticker\": {\n";
    out << "      \"enabled\": " << boolToString(manifest.bookTickerEnabled) << ",\n";
    out << "      \"required_when_enabled\": " << boolToString(manifest.bookTickerRequiredWhenEnabled) << ",\n";
    out << "      \"path\": " << json::quote(manifest.bookTickerPath) << ",\n";
    out << "      \"row_schema\": " << json::quote(manifest.bookTickerRowSchema) << ",\n";
    out << "      \"declared_event_count\": " << manifest.bookTickerCount << "\n";
    out << "    },\n";
    out << "    \"depth\": {\n";
    out << "      \"enabled\": " << boolToString(manifest.orderbookEnabled) << ",\n";
    out << "      \"required_when_enabled\": " << boolToString(manifest.orderbookRequiredWhenEnabled) << ",\n";
    out << "      \"path\": " << json::quote(manifest.depthPath) << ",\n";
    out << "      \"row_schema\": " << json::quote(manifest.depthRowSchema) << ",\n";
    out << "      \"declared_event_count\": " << manifest.depthCount << "\n";
    out << "    }\n";
    out << "  },\n";
    out << "  \"snapshots\": {\n";
    out << "    \"schema\": " << json::quote(manifest.snapshotSchema) << ",\n";
    out << "    \"files\": ";
    appendStringArray(out, manifest.snapshotFiles);
    out << ",\n";
    out << "    \"declared_snapshot_count\": " << manifest.snapshotCount << "\n";
    out << "  },\n";
    out << "  \"artifacts\": {\n";
    out << "    \"instrument_metadata_path\": " << json::quote(manifest.instrumentMetadataPath) << ",\n";
    out << "    \"session_audit_path\": " << json::quote(manifest.sessionAuditPath) << ",\n";
    out << "    \"loader_diagnostics_path\": " << json::quote(manifest.loaderDiagnosticsPath) << ",\n";
    out << "    \"canonical\": ";
    appendStringArray(out, manifest.canonicalArtifacts);
    out << ",\n";
    out << "    \"support\": ";
    appendStringArray(out, manifest.supportArtifacts);
    out << "\n  },\n";
    out << "  \"integrity\": {\n";
    out << "    \"session_health\": " << json::quote(std::string{toString(manifest.sessionHealth)}) << ",\n";
    out << "    \"exact_replay_eligible\": " << boolToString(manifest.exactReplayEligible) << ",\n";
    out << "    \"report_path\": " << json::quote(manifest.integrityReportPath) << ",\n";
    out << "    \"summary\": {\n";
    out << "      \"total_incidents\": " << manifest.totalIntegrityIncidents << ",\n";
    out << "      \"highest_severity\": " << json::quote(std::string{toString(manifest.highestIntegritySeverity)}) << "\n";
    out << "    },\n";
    out << "    \"channels\": {\n";
    appendChannelIntegrity(out, "trades", manifest.tradesIntegrity, true);
    appendChannelIntegrity(out, "bookticker", manifest.bookTickerIntegrity, true);
    appendChannelIntegrity(out, "depth", manifest.depthIntegrity, true);
    appendChannelIntegrity(out, "snapshot", manifest.snapshotIntegrity, false);
    out << "    }\n";
    out << "  },\n";
    out << "  \"summary\": {\n";
    out << "    \"warning_summary\": " << json::quote(manifest.warningSummary) << "\n";
    out << "  }\n";
    out << "}\n";
    return out.str();
}

Status parseManifestJson(std::string_view document, SessionManifest& manifest) noexcept {
    SessionManifest parsed{};
    JsonParser parser{document};
    if (!parser.parseObjectStart()) return Status::CorruptData;
    if (!parser.peek('}')) {
        std::string key;
        do {
            if (!parser.parseKey(key)) return Status::CorruptData;
            if (key == "manifest_schema_version") {
                if (!parseInt32Field(parser, parsed.manifestSchemaVersion)) return Status::CorruptData;
            } else if (key == "corpus_schema_version") {
                if (!parseInt32Field(parser, parsed.corpusSchemaVersion)) return Status::CorruptData;
            } else if (key == "capture_contract_version") {
                if (!parser.parseString(parsed.captureContractVersion)) return Status::CorruptData;
            } else if (key == "session_status") {
                if (!parser.parseString(parsed.sessionStatus)) return Status::CorruptData;
            } else if (key == "identity") {
                if (!parseIdentityObject(parser, parsed)) return Status::CorruptData;
            } else if (key == "capture") {
                if (!parseCaptureObject(parser, parsed)) return Status::CorruptData;
            } else if (key == "replay") {
                if (!parseReplayObject(parser, parsed)) return Status::CorruptData;
            } else if (key == "channels") {
                if (!parseChannelsObject(parser, parsed)) return Status::CorruptData;
            } else if (key == "snapshots") {
                if (!parseSnapshotsObject(parser, parsed)) return Status::CorruptData;
            } else if (key == "artifacts") {
                if (!parseArtifactsObject(parser, parsed)) return Status::CorruptData;
            } else if (key == "integrity") {
                if (!parser.parseObjectStart()) return Status::CorruptData;
                if (!parser.peek('}')) {
                    std::string nestedKey;
                    do {
                        if (!parser.parseKey(nestedKey)) return Status::CorruptData;
                        if (nestedKey == "session_health") {
                            std::string sessionHealth;
                            if (!parser.parseString(sessionHealth)
                                || !parseSessionHealthString(sessionHealth, parsed.sessionHealth)) {
                                return Status::CorruptData;
                            }
                        } else if (nestedKey == "exact_replay_eligible") {
                            if (!parser.parseBool(parsed.exactReplayEligible)) return Status::CorruptData;
                        } else if (nestedKey == "report_path") {
                            if (!parser.parseString(parsed.integrityReportPath)) return Status::CorruptData;
                        } else if (nestedKey == "summary") {
                            if (!parseIntegritySummaryObject(parser, parsed)) return Status::CorruptData;
                        } else if (nestedKey == "channels") {
                            if (!parseChannelIntegrityGroupObject(parser, parsed)) return Status::CorruptData;
                        } else {
                            if (!parser.skipValue()) return Status::CorruptData;
                        }
                        if (parser.peek('}')) break;
                    } while (parser.parseComma());
                }
                if (!parser.parseObjectEnd()) return Status::CorruptData;
            } else if (key == "summary") {
                if (!parseSummaryObject(parser, parsed)) return Status::CorruptData;
            } else if (key == "session_id"
                    || key == "exchange"
                    || key == "market"
                    || key == "symbols"
                    || key == "selected_parent_dir"
                    || key == "started_at_ns"
                    || key == "ended_at_ns"
                    || key == "target_duration_sec"
                    || key == "actual_duration_sec"
                    || key == "snapshot_interval_sec"
                    || key == "channel_status"
                    || key == "event_counts"
                    || key == "warning_summary") {
                return Status::CorruptData;
            } else {
                if (!parser.skipValue()) return Status::CorruptData;
            }
            if (parser.peek('}')) break;
        } while (parser.parseComma());
    }
    if (!parser.parseObjectEnd() || !parser.finish()) return Status::CorruptData;

    if (parsed.sessionStatus.empty()) parsed.sessionStatus = "complete";
    populateCanonicalArtifacts(parsed);
    populateSupportArtifacts(parsed);
    validateStructurally(parsed);
    manifest = std::move(parsed);
    return Status::Ok;
}

bool isSupportedManifestSchemaVersion(std::int32_t version) noexcept {
    return version == kManifestSchemaVersionCurrent;
}

bool isSupportedCorpusSchemaVersion(std::int32_t version) noexcept {
    return version == kCorpusSchemaVersionCurrent;
}

}  // namespace hftrec::capture
