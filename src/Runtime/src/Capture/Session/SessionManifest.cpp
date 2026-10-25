#include "SessionManifest.hpp"

#include <cstddef>
#include <sstream>
#include <string>
#include <utility>

#include "ChannelKind.hpp"
#include "../../Common/JsonString.hpp"
#include "../../Common/MiniJsonParser.hpp"

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
    if (parser.peek('}')) return false;
    unsigned seen = 0u;
    std::string key;
    do {
        if (!parser.parseKey(key)) return false;
        if (key == "session_id") {
            seen |= 1u << 0u;
            if (!parser.parseString(manifest.sessionId)) return false;
        } else if (key == "exchange") {
            seen |= 1u << 1u;
            if (!parser.parseString(manifest.exchange)) return false;
        } else if (key == "market") {
            seen |= 1u << 2u;
            if (!parser.parseString(manifest.market)) return false;
        } else if (key == "symbols") {
            seen |= 1u << 3u;
            if (!parseStringArray(parser, manifest.symbols)) return false;
        } else if (key == "route_symbols") {
            if (!parser.skipValue()) return false;
        } else if (key == "storage_symbol") {
            if (!parser.parseString(manifest.storageSymbol)) return false;
        } else {
            if (!parser.skipValue()) return false;
        }
        if (parser.peek('}')) break;
    } while (parser.parseComma());
    return seen == 15u && parser.parseObjectEnd();
}


bool parseCaptureObject(JsonParser& parser, SessionManifest& manifest) noexcept {
    if (!parser.parseObjectStart()) return false;
    if (parser.peek('}')) return false;
    unsigned seen = 0u;
    std::string key;
    do {
        if (!parser.parseKey(key)) return false;
        if (key == "selected_parent_dir") {
            seen |= 1u << 0u;
            if (!parser.parseString(manifest.selectedParentDir)) return false;
        } else if (key == "started_at_ns") {
            seen |= 1u << 1u;
            if (!parseInt64Field(parser, manifest.startedAtNs)) return false;
        } else if (key == "ended_at_ns") {
            seen |= 1u << 2u;
            if (!parseInt64Field(parser, manifest.endedAtNs)) return false;
        } else if (key == "target_duration_sec") {
            seen |= 1u << 3u;
            if (!parseInt64Field(parser, manifest.targetDurationSec)) return false;
        } else if (key == "actual_duration_sec") {
            seen |= 1u << 4u;
            if (!parseInt64Field(parser, manifest.actualDurationSec)) return false;
        } else if (key == "snapshot_interval_sec") {
            seen |= 1u << 5u;
            if (!parseInt64Field(parser, manifest.snapshotIntervalSec)) return false;
        } else {
            if (!parser.skipValue()) return false;
        }
        if (parser.peek('}')) break;
    } while (parser.parseComma());
    return seen == 63u && parser.parseObjectEnd();
}

bool parseArrivalClockObject(JsonParser& parser,
                             ArrivalClockSummary& summary) noexcept {
    if (!parser.parseObjectStart()) return false;
    if (parser.peek('}')) return false;
    std::uint16_t seen = 0u;
    std::string key;
    do {
        if (!parser.parseKey(key)) return false;
        if (key == "boundary") {
            if (!parser.parseString(summary.boundary)) return false;
            seen |= std::uint16_t{1u} << 0u;
        } else if (key == "realtime_clock") {
            if (!parser.parseString(summary.realtimeClock)) return false;
            seen |= std::uint16_t{1u} << 1u;
        } else if (key == "monotonic_clock") {
            if (!parser.parseString(summary.monotonicClock)) return false;
            seen |= std::uint16_t{1u} << 2u;
        } else if (key == "captured_rows") {
            if (!parseUint64Field(parser, summary.capturedRows)) return false;
            seen |= std::uint16_t{1u} << 3u;
        } else if (key == "historical_rows") {
            if (!parseUint64Field(parser, summary.historicalRows)) return false;
            seen |= std::uint16_t{1u} << 4u;
        } else if (key == "unavailable_rows") {
            if (!parseUint64Field(parser, summary.unavailableRows)) return false;
            seen |= std::uint16_t{1u} << 5u;
        } else if (key == "realtime_regressions") {
            if (!parseUint64Field(parser, summary.realtimeRegressions)) return false;
            seen |= std::uint16_t{1u} << 6u;
        } else if (key == "monotonic_non_increasing") {
            if (!parseUint64Field(parser, summary.monotonicNonIncreasing)) return false;
            seen |= std::uint16_t{1u} << 7u;
        } else if (key == "exchange_ahead_of_receive") {
            if (!parseUint64Field(parser, summary.exchangeAheadOfReceive)) return false;
            seen |= std::uint16_t{1u} << 8u;
        } else if (key == "exchange_timestamp_missing") {
            if (!parseUint64Field(parser, summary.exchangeTimestampMissing)) return false;
            seen |= std::uint16_t{1u} << 9u;
        } else if (key == "first_receive_realtime_ns") {
            if (!parseInt64Field(parser, summary.firstReceiveRealtimeNs)) return false;
            seen |= std::uint16_t{1u} << 10u;
        } else if (key == "last_receive_realtime_ns") {
            if (!parseInt64Field(parser, summary.lastReceiveRealtimeNs)) return false;
            seen |= std::uint16_t{1u} << 11u;
        } else if (key == "first_receive_monotonic_ns") {
            if (!parseUint64Field(parser, summary.firstReceiveMonotonicNs)) return false;
            seen |= std::uint16_t{1u} << 12u;
        } else if (key == "last_receive_monotonic_ns") {
            if (!parseUint64Field(parser, summary.lastReceiveMonotonicNs)) return false;
            seen |= std::uint16_t{1u} << 13u;
        } else if (!parser.skipValue()) {
            return false;
        }
        if (parser.peek('}')) break;
    } while (parser.parseComma());
    constexpr std::uint16_t required = (std::uint16_t{1u} << 14u) - 1u;
    return seen == required && parser.parseObjectEnd();
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
                        std::uint64_t& count,
                        std::string* sidecarPath = nullptr) noexcept {
    if (!parser.parseObjectStart()) return false;
    if (parser.peek('}')) return false;
    std::uint16_t seen = 0u;
    std::string key;
    do {
        if (!parser.parseKey(key)) return false;
        if (key == "enabled") {
            seen |= std::uint16_t{1u} << 0u;
            if (!parser.parseBool(enabled)) return false;
        } else if (key == "required_when_enabled") {
            seen |= std::uint16_t{1u} << 1u;
            if (!parser.parseBool(requiredWhenEnabled)) return false;
        } else if (key == "path") {
            seen |= std::uint16_t{1u} << 2u;
            if (!parser.parseString(path)) return false;
        } else if (key == "sidecar_path") {
            seen |= std::uint16_t{1u} << 4u;
            if (sidecarPath == nullptr) {
                if (!parser.skipValue()) return false;
            } else if (!parser.parseString(*sidecarPath)) {
                return false;
            }
        } else if (key == "declared_event_count") {
            seen |= std::uint16_t{1u} << 3u;
            if (!parseUint64Field(parser, count)) return false;
        } else {
            if (!parser.skipValue()) return false;
        }
        if (parser.peek('}')) break;
    } while (parser.parseComma());
    const std::uint16_t required = sidecarPath == nullptr ? 15u : 31u;
    return (seen & required) == required && parser.parseObjectEnd();
}

bool parseTradesChannelObject(JsonParser& parser, SessionManifest& manifest) noexcept {
    if (!parser.parseObjectStart()) return false;
    if (parser.peek('}')) return false;
    std::uint16_t seen = 0u;
    std::string key;
    do {
        if (!parser.parseKey(key)) return false;
        if (key == "enabled") {
            seen |= std::uint16_t{1u} << 0u;
            if (!parser.parseBool(manifest.tradesEnabled)) return false;
        } else if (key == "required_when_enabled") {
            seen |= std::uint16_t{1u} << 1u;
            if (!parser.parseBool(manifest.tradesRequiredWhenEnabled)) return false;
        } else if (key == "path") {
            seen |= std::uint16_t{1u} << 2u;
            if (!parser.parseString(manifest.tradesPath)) return false;
        } else if (key == "declared_event_count") {
            seen |= std::uint16_t{1u} << 3u;
            if (!parseUint64Field(parser, manifest.tradesCount)) return false;
        } else if (key == "history_warmup_sec") {
            if (!parseInt64Field(parser, manifest.tradesHistoryWarmupSec)) return false;
        } else if (key == "history_requested_start_ns") {
            if (!parseInt64Field(parser, manifest.tradesHistoryRequestedStartNs)) return false;
        } else if (key == "history_requested_end_ns") {
            if (!parseInt64Field(parser, manifest.tradesHistoryRequestedEndNs)) return false;
        } else if (key == "history_rows") {
            if (!parseUint64Field(parser, manifest.tradesHistoryRows)) return false;
        } else if (key == "history_requests") {
            if (!parseUint64Field(parser, manifest.tradesHistoryRequests)) return false;
        } else if (key == "history_feed_kind") {
            if (!parser.parseString(manifest.tradesHistoryFeedKind)) return false;
        } else if (key == "history_status") {
            if (!parser.parseString(manifest.tradesHistoryStatus)) return false;
        } else {
            if (!parser.skipValue()) return false;
        }
        if (parser.peek('}')) break;
    } while (parser.parseComma());
    constexpr std::uint16_t required = (std::uint16_t{1u} << 4u) - 1u;
    return seen == required && parser.parseObjectEnd();
}


bool parseChannelsObject(JsonParser& parser, SessionManifest& manifest) noexcept {
    if (!parser.parseObjectStart()) return false;
    if (parser.peek('}')) return false;
    std::uint16_t seen = 0u;
    std::string key;
    do {
        if (!parser.parseKey(key)) return false;
        if (key == "trades") {
            seen |= std::uint16_t{1u} << 0u;
            if (!parseTradesChannelObject(parser, manifest)) {
                return false;
            }
        } else if (key == "liquidations") {
            seen |= std::uint16_t{1u} << 1u;
            if (!parseChannelObject(parser,
                                    manifest.liquidationsEnabled,
                                    manifest.liquidationsRequiredWhenEnabled,
                                    manifest.liquidationsPath,
                                    manifest.liquidationsCount)) {
                return false;
            }
        } else if (key == "bookticker") {
            seen |= std::uint16_t{1u} << 2u;
            if (!parseChannelObject(parser,
                                    manifest.bookTickerEnabled,
                                    manifest.bookTickerRequiredWhenEnabled,
                                    manifest.bookTickerPath,
                                    manifest.bookTickerCount)) {
                return false;
            }
        } else if (key == "depth") {
            seen |= std::uint16_t{1u} << 3u;
            if (!parseChannelObject(parser,
                                    manifest.orderbookEnabled,
                                    manifest.orderbookRequiredWhenEnabled,
                                    manifest.depthPath,
                                    manifest.depthCount,
                                    &manifest.depthSidecarPath)) {
                return false;
            }
        } else if (key == "candles") {
            seen |= std::uint16_t{1u} << 4u;
            if (!parseChannelObject(parser,
                                    manifest.candlesEnabled,
                                    manifest.candlesRequiredWhenEnabled,
                                    manifest.candlesPath,
                                    manifest.candlesCount)) {
                return false;
            }
        } else if (key == "candles2") {
            seen |= std::uint16_t{1u} << 5u;
            if (!parseChannelObject(parser,
                                    manifest.candles2Enabled,
                                    manifest.candles2RequiredWhenEnabled,
                                    manifest.candles2Path,
                                    manifest.candles2Count)) {
                return false;
            }
        } else if (key == "mark_price") {
            seen |= std::uint16_t{1u} << 6u;
            if (!parseChannelObject(parser,
                                    manifest.markPriceEnabled,
                                    manifest.markPriceRequiredWhenEnabled,
                                    manifest.markPricePath,
                                    manifest.markPriceCount)) {
                return false;
            }
        } else if (key == "index_price") {
            seen |= std::uint16_t{1u} << 7u;
            if (!parseChannelObject(parser,
                                    manifest.indexPriceEnabled,
                                    manifest.indexPriceRequiredWhenEnabled,
                                    manifest.indexPricePath,
                                    manifest.indexPriceCount)) {
                return false;
            }
        } else if (key == "funding") {
            seen |= std::uint16_t{1u} << 8u;
            if (!parseChannelObject(parser,
                                    manifest.fundingEnabled,
                                    manifest.fundingRequiredWhenEnabled,
                                    manifest.fundingPath,
                                    manifest.fundingCount)) {
                return false;
            }
        } else if (key == "price_limit") {
            seen |= std::uint16_t{1u} << 9u;
            if (!parseChannelObject(parser,
                                    manifest.priceLimitEnabled,
                                    manifest.priceLimitRequiredWhenEnabled,
                                    manifest.priceLimitPath,
                                    manifest.priceLimitCount)) {
                return false;
            }
        } else {
            if (!parser.skipValue()) return false;
        }
        if (parser.peek('}')) break;
    } while (parser.parseComma());
    constexpr std::uint16_t required = (std::uint16_t{1u} << 10u) - 1u;
    return seen == required && parser.parseObjectEnd();
}

bool parseChannelRuntimeHealthObject(JsonParser& parser, ChannelRuntimeHealth& health) noexcept {
    if (!parser.parseObjectStart()) return false;
    if (parser.peek('}')) return parser.parseObjectEnd();
    std::string key;
    do {
        if (!parser.parseKey(key)) return false;
        if (key == "state") {
            if (!parser.parseString(health.state)) return false;
        } else if (key == "required") {
            if (!parser.parseBool(health.required)) return false;
        } else if (key == "first_row_ns") {
            if (!parseInt64Field(parser, health.firstRowNs)) return false;
        } else if (key == "last_row_ns") {
            if (!parseInt64Field(parser, health.lastRowNs)) return false;
        } else if (key == "reconnect_count") {
            if (!parseUint64Field(parser, health.reconnectCount)) return false;
        } else if (key == "dropped_event_count") {
            if (!parseUint64Field(parser, health.droppedEventCount)) return false;
        } else if (key == "unroutable_event_count") {
            if (!parseUint64Field(parser, health.unroutableEventCount)) return false;
        } else if (key == "last_error") {
            if (!parser.parseString(health.lastError)) return false;
        } else if (!parser.skipValue()) {
            return false;
        }
        if (parser.peek('}')) break;
    } while (parser.parseComma());
    return parser.parseObjectEnd();
}

bool parseRuntimeHealthObject(JsonParser& parser, SessionManifest& manifest) noexcept {
    if (!parser.parseObjectStart()) return false;
    if (parser.peek('}')) return parser.parseObjectEnd();
    std::string key;
    do {
        if (!parser.parseKey(key)) return false;
        ChannelRuntimeHealth* health = nullptr;
        if (key == "trades") health = &manifest.tradesRuntime;
        else if (key == "liquidations") health = &manifest.liquidationsRuntime;
        else if (key == "bookticker") health = &manifest.bookTickerRuntime;
        else if (key == "depth") health = &manifest.depthRuntime;
        else if (key == "mark_price") health = &manifest.markPriceRuntime;
        else if (key == "index_price") health = &manifest.indexPriceRuntime;
        else if (key == "funding") health = &manifest.fundingRuntime;
        else if (key == "price_limit") health = &manifest.priceLimitRuntime;
        if (health) {
            if (!parseChannelRuntimeHealthObject(parser, *health)) return false;
        } else if (!parser.skipValue()) {
            return false;
        }
        if (parser.peek('}')) break;
    } while (parser.parseComma());
    return parser.parseObjectEnd();
}


bool parseArtifactsObject(JsonParser& parser, SessionManifest& manifest) noexcept {
    if (!parser.parseObjectStart()) return false;
    if (parser.peek('}')) return false;
    std::uint16_t seen = 0u;
    std::string key;
    do {
        if (!parser.parseKey(key)) return false;
        if (key == "canonical") {
            if (!parseStringArray(parser, manifest.canonicalArtifacts)) return false;
        } else if (key == "support") {
            if (!parseStringArray(parser, manifest.supportArtifacts)) return false;
        } else if (key == "instrument_metadata_path") {
            seen |= std::uint16_t{1u} << 0u;
            if (!parser.parseString(manifest.instrumentMetadataPath)) return false;
        } else if (key == "session_audit_path") {
            seen |= std::uint16_t{1u} << 1u;
            if (!parser.parseString(manifest.sessionAuditPath)) return false;
        } else if (key == "loader_diagnostic_path") {
            seen |= std::uint16_t{1u} << 2u;
            if (!parser.parseString(manifest.loaderDiagnosticPath)) return false;
        } else if (key == "market_data_launch_path") {
            seen |= std::uint16_t{1u} << 3u;
            if (!parser.parseString(manifest.marketDataLaunchPath)) return false;
        } else {
            if (!parser.skipValue()) return false;
        }
        if (parser.peek('}')) break;
    } while (parser.parseComma());
    constexpr std::uint16_t required = (std::uint16_t{1u} << 4u) - 1u;
    return seen == required && parser.parseObjectEnd();
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
        } else if (key == "liquidations") {
            if (!parseChannelIntegrityObject(parser, manifest.liquidationsIntegrity)) return false;
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



void populateCanonicalArtifacts(SessionManifest& manifest) {
    manifest.canonicalArtifacts.clear();
    manifest.canonicalArtifacts.push_back("manifest.json");
    if (!manifest.instrumentMetadataPath.empty()) manifest.canonicalArtifacts.push_back(manifest.instrumentMetadataPath);
    if (manifest.tradesEnabled && !manifest.tradesPath.empty()) manifest.canonicalArtifacts.push_back(manifest.tradesPath);
    if (manifest.liquidationsEnabled && !manifest.liquidationsPath.empty()) manifest.canonicalArtifacts.push_back(manifest.liquidationsPath);
    if (manifest.bookTickerEnabled && !manifest.bookTickerPath.empty()) manifest.canonicalArtifacts.push_back(manifest.bookTickerPath);
    if (manifest.orderbookEnabled && !manifest.depthPath.empty()) {
        manifest.canonicalArtifacts.push_back(manifest.depthPath);
        if (!manifest.depthSidecarPath.empty()) {
            manifest.canonicalArtifacts.push_back(manifest.depthSidecarPath);
        }
    }
    if (manifest.candlesEnabled && !manifest.candlesPath.empty()) manifest.canonicalArtifacts.push_back(manifest.candlesPath);
    if (manifest.candles2Enabled && !manifest.candles2Path.empty()) manifest.canonicalArtifacts.push_back(manifest.candles2Path);
    if (manifest.markPriceEnabled && !manifest.markPricePath.empty()) manifest.canonicalArtifacts.push_back(manifest.markPricePath);
    if (manifest.indexPriceEnabled && !manifest.indexPricePath.empty()) manifest.canonicalArtifacts.push_back(manifest.indexPricePath);
    if (manifest.fundingEnabled && !manifest.fundingPath.empty()) manifest.canonicalArtifacts.push_back(manifest.fundingPath);
    if (manifest.priceLimitEnabled && !manifest.priceLimitPath.empty()) manifest.canonicalArtifacts.push_back(manifest.priceLimitPath);
}

void populateSupportArtifacts(SessionManifest& manifest) {
    manifest.supportArtifacts.clear();
    if (!manifest.sessionAuditPath.empty()) manifest.supportArtifacts.push_back(manifest.sessionAuditPath);
    if (!manifest.integrityReportPath.empty()) manifest.supportArtifacts.push_back(manifest.integrityReportPath);
    if (!manifest.loaderDiagnosticPath.empty()) manifest.supportArtifacts.push_back(manifest.loaderDiagnosticPath);
    if (!manifest.marketDataLaunchPath.empty()) manifest.supportArtifacts.push_back(manifest.marketDataLaunchPath);
}

bool validateStructurally(SessionManifest& manifest) {
    manifest.structuralBlockers.clear();
    if (manifest.sessionId.empty()) manifest.structuralBlockers.push_back("missing session_id");
    if (manifest.exchange.empty()) manifest.structuralBlockers.push_back("missing exchange");
    if (manifest.market.empty()) manifest.structuralBlockers.push_back("missing market");
    if (manifest.symbols.empty()) manifest.structuralBlockers.push_back("missing symbols");
    if (manifest.tradesEnabled && manifest.tradesRequiredWhenEnabled && manifest.tradesPath.empty()) {
        manifest.structuralBlockers.push_back("missing trades path");
    }
    if (manifest.liquidationsEnabled && manifest.liquidationsRequiredWhenEnabled && manifest.liquidationsPath.empty()) {
        manifest.structuralBlockers.push_back("missing liquidations path");
    }
    if (manifest.bookTickerEnabled && manifest.bookTickerRequiredWhenEnabled && manifest.bookTickerPath.empty()) {
        manifest.structuralBlockers.push_back("missing bookticker path");
    }
    if (manifest.orderbookEnabled && manifest.depthPath.empty()) {
        manifest.structuralBlockers.push_back("missing depth path");
    }
    if (manifest.orderbookEnabled && manifest.depthSidecarPath.empty()) {
        manifest.structuralBlockers.push_back("missing depth sidecar path");
    }
    if (manifest.candlesEnabled && manifest.candlesRequiredWhenEnabled && manifest.candlesPath.empty()) {
        manifest.structuralBlockers.push_back("missing candles path");
    }
    if (manifest.candles2Enabled && manifest.candles2RequiredWhenEnabled && manifest.candles2Path.empty()) {
        manifest.structuralBlockers.push_back("missing candles2 path");
    }
    if (manifest.markPriceEnabled && manifest.markPriceRequiredWhenEnabled && manifest.markPricePath.empty()) {
        manifest.structuralBlockers.push_back("missing mark_price path");
    }
    if (manifest.indexPriceEnabled && manifest.indexPriceRequiredWhenEnabled && manifest.indexPricePath.empty()) {
        manifest.structuralBlockers.push_back("missing index_price path");
    }
    if (manifest.fundingEnabled && manifest.fundingRequiredWhenEnabled && manifest.fundingPath.empty()) {
        manifest.structuralBlockers.push_back("missing funding path");
    }
    if (manifest.priceLimitEnabled && manifest.priceLimitRequiredWhenEnabled && manifest.priceLimitPath.empty()) {
        manifest.structuralBlockers.push_back("missing price_limit path");
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

void appendChannelRuntimeHealth(std::ostringstream& out,
                                std::string_view name,
                                const ChannelRuntimeHealth& health,
                                bool trailingComma) {
    out << "    \"" << name << "\": {\n";
    out << "      \"state\": " << json::quote(health.state) << ",\n";
    out << "      \"required\": " << boolToString(health.required) << ",\n";
    out << "      \"first_row_ns\": " << health.firstRowNs << ",\n";
    out << "      \"last_row_ns\": " << health.lastRowNs << ",\n";
    out << "      \"reconnect_count\": " << health.reconnectCount << ",\n";
    out << "      \"dropped_event_count\": " << health.droppedEventCount << ",\n";
    out << "      \"unroutable_event_count\": " << health.unroutableEventCount << ",\n";
    out << "      \"last_error\": " << json::quote(health.lastError) << "\n";
    out << "    }" << (trailingComma ? "," : "") << "\n";
}

}  // namespace

std::string renderManifestJson(const SessionManifest& manifest) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"session_status\": " << json::quote(manifest.sessionStatus) << ",\n";
    out << "  \"identity\": {\n";
    out << "    \"session_id\": " << json::quote(manifest.sessionId) << ",\n";
    out << "    \"exchange\": " << json::quote(manifest.exchange) << ",\n";
    out << "    \"market\": " << json::quote(manifest.market) << ",\n";
    out << "    \"symbols\": ";
    appendStringArray(out, manifest.symbols);
    out << ",\n";
    out << "    \"storage_symbol\": " << json::quote(manifest.storageSymbol) << "\n";
    out << "  },\n";
    out << "  \"capture\": {\n";
    out << "    \"selected_parent_dir\": " << json::quote(manifest.selectedParentDir) << ",\n";
    out << "    \"started_at_ns\": " << manifest.startedAtNs << ",\n";
    out << "    \"ended_at_ns\": " << manifest.endedAtNs << ",\n";
    out << "    \"target_duration_sec\": " << manifest.targetDurationSec << ",\n";
    out << "    \"actual_duration_sec\": " << manifest.actualDurationSec << ",\n";
    out << "    \"snapshot_interval_sec\": " << manifest.snapshotIntervalSec << "\n";
    out << "  },\n";
    out << "  \"arrival_clock\": {\n";
    out << "    \"boundary\": " << json::quote(manifest.arrivalClock.boundary) << ",\n";
    out << "    \"realtime_clock\": " << json::quote(manifest.arrivalClock.realtimeClock) << ",\n";
    out << "    \"monotonic_clock\": " << json::quote(manifest.arrivalClock.monotonicClock) << ",\n";
    out << "    \"captured_rows\": " << manifest.arrivalClock.capturedRows << ",\n";
    out << "    \"historical_rows\": " << manifest.arrivalClock.historicalRows << ",\n";
    out << "    \"unavailable_rows\": " << manifest.arrivalClock.unavailableRows << ",\n";
    out << "    \"realtime_regressions\": " << manifest.arrivalClock.realtimeRegressions << ",\n";
    out << "    \"monotonic_non_increasing\": " << manifest.arrivalClock.monotonicNonIncreasing << ",\n";
    out << "    \"exchange_ahead_of_receive\": " << manifest.arrivalClock.exchangeAheadOfReceive << ",\n";
    out << "    \"exchange_timestamp_missing\": " << manifest.arrivalClock.exchangeTimestampMissing << ",\n";
    out << "    \"first_receive_realtime_ns\": " << manifest.arrivalClock.firstReceiveRealtimeNs << ",\n";
    out << "    \"last_receive_realtime_ns\": " << manifest.arrivalClock.lastReceiveRealtimeNs << ",\n";
    out << "    \"first_receive_monotonic_ns\": " << manifest.arrivalClock.firstReceiveMonotonicNs << ",\n";
    out << "    \"last_receive_monotonic_ns\": " << manifest.arrivalClock.lastReceiveMonotonicNs << "\n";
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
    out << "      \"declared_event_count\": " << manifest.tradesCount << ",\n";
    out << "      \"history_warmup_sec\": " << manifest.tradesHistoryWarmupSec << ",\n";
    out << "      \"history_requested_start_ns\": " << manifest.tradesHistoryRequestedStartNs << ",\n";
    out << "      \"history_requested_end_ns\": " << manifest.tradesHistoryRequestedEndNs << ",\n";
    out << "      \"history_rows\": " << manifest.tradesHistoryRows << ",\n";
    out << "      \"history_requests\": " << manifest.tradesHistoryRequests << ",\n";
    out << "      \"history_feed_kind\": " << json::quote(manifest.tradesHistoryFeedKind) << ",\n";
    out << "      \"history_status\": " << json::quote(manifest.tradesHistoryStatus) << "\n";
    out << "    },\n";
    out << "    \"liquidations\": {\n";
    out << "      \"enabled\": " << boolToString(manifest.liquidationsEnabled) << ",\n";
    out << "      \"required_when_enabled\": " << boolToString(manifest.liquidationsRequiredWhenEnabled) << ",\n";
    out << "      \"path\": " << json::quote(manifest.liquidationsPath) << ",\n";
    out << "      \"declared_event_count\": " << manifest.liquidationsCount << "\n";
    out << "    },\n";
    out << "    \"bookticker\": {\n";
    out << "      \"enabled\": " << boolToString(manifest.bookTickerEnabled) << ",\n";
    out << "      \"required_when_enabled\": " << boolToString(manifest.bookTickerRequiredWhenEnabled) << ",\n";
    out << "      \"path\": " << json::quote(manifest.bookTickerPath) << ",\n";
    out << "      \"declared_event_count\": " << manifest.bookTickerCount << "\n";
    out << "    },\n";
    out << "    \"depth\": {\n";
    out << "      \"enabled\": " << boolToString(manifest.orderbookEnabled) << ",\n";
    out << "      \"required_when_enabled\": " << boolToString(manifest.orderbookRequiredWhenEnabled) << ",\n";
    out << "      \"path\": " << json::quote(manifest.depthPath) << ",\n";
    out << "      \"sidecar_path\": " << json::quote(manifest.depthSidecarPath) << ",\n";
    out << "      \"declared_event_count\": " << manifest.depthCount << "\n";
    out << "    },\n";
    out << "    \"candles\": {\n";
    out << "      \"enabled\": " << boolToString(manifest.candlesEnabled) << ",\n";
    out << "      \"required_when_enabled\": " << boolToString(manifest.candlesRequiredWhenEnabled) << ",\n";
    out << "      \"path\": " << json::quote(manifest.candlesPath) << ",\n";
    out << "      \"declared_event_count\": " << manifest.candlesCount << "\n";
    out << "    },\n";
    out << "    \"candles2\": {\n";
    out << "      \"enabled\": " << boolToString(manifest.candles2Enabled) << ",\n";
    out << "      \"required_when_enabled\": " << boolToString(manifest.candles2RequiredWhenEnabled) << ",\n";
    out << "      \"path\": " << json::quote(manifest.candles2Path) << ",\n";
    out << "      \"declared_event_count\": " << manifest.candles2Count << "\n";
    out << "    },\n";
    out << "    \"mark_price\": {\n";
    out << "      \"enabled\": " << boolToString(manifest.markPriceEnabled) << ",\n";
    out << "      \"required_when_enabled\": " << boolToString(manifest.markPriceRequiredWhenEnabled) << ",\n";
    out << "      \"path\": " << json::quote(manifest.markPricePath) << ",\n";
    out << "      \"declared_event_count\": " << manifest.markPriceCount << "\n";
    out << "    },\n";
    out << "    \"index_price\": {\n";
    out << "      \"enabled\": " << boolToString(manifest.indexPriceEnabled) << ",\n";
    out << "      \"required_when_enabled\": " << boolToString(manifest.indexPriceRequiredWhenEnabled) << ",\n";
    out << "      \"path\": " << json::quote(manifest.indexPricePath) << ",\n";
    out << "      \"declared_event_count\": " << manifest.indexPriceCount << "\n";
    out << "    },\n";
    out << "    \"funding\": {\n";
    out << "      \"enabled\": " << boolToString(manifest.fundingEnabled) << ",\n";
    out << "      \"required_when_enabled\": " << boolToString(manifest.fundingRequiredWhenEnabled) << ",\n";
    out << "      \"path\": " << json::quote(manifest.fundingPath) << ",\n";
    out << "      \"declared_event_count\": " << manifest.fundingCount << "\n";
    out << "    },\n";
    out << "    \"price_limit\": {\n";
    out << "      \"enabled\": " << boolToString(manifest.priceLimitEnabled) << ",\n";
    out << "      \"required_when_enabled\": " << boolToString(manifest.priceLimitRequiredWhenEnabled) << ",\n";
    out << "      \"path\": " << json::quote(manifest.priceLimitPath) << ",\n";
    out << "      \"declared_event_count\": " << manifest.priceLimitCount << "\n";
    out << "    }\n";
    out << "  },\n";
    out << "  \"runtime_health\": {\n";
    appendChannelRuntimeHealth(out, "trades", manifest.tradesRuntime, true);
    appendChannelRuntimeHealth(out, "liquidations", manifest.liquidationsRuntime, true);
    appendChannelRuntimeHealth(out, "bookticker", manifest.bookTickerRuntime, true);
    appendChannelRuntimeHealth(out, "depth", manifest.depthRuntime, true);
    appendChannelRuntimeHealth(out, "mark_price", manifest.markPriceRuntime, true);
    appendChannelRuntimeHealth(out, "index_price", manifest.indexPriceRuntime, true);
    appendChannelRuntimeHealth(out, "funding", manifest.fundingRuntime, true);
    appendChannelRuntimeHealth(out, "price_limit", manifest.priceLimitRuntime, false);
    out << "  },\n";
    out << "  \"artifacts\": {\n";
    out << "    \"instrument_metadata_path\": " << json::quote(manifest.instrumentMetadataPath) << ",\n";
    out << "    \"session_audit_path\": " << json::quote(manifest.sessionAuditPath) << ",\n";
    out << "    \"loader_diagnostic_path\": " << json::quote(manifest.loaderDiagnosticPath) << ",\n";
    out << "    \"market_data_launch_path\": " << json::quote(manifest.marketDataLaunchPath) << ",\n";
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
    appendChannelIntegrity(out, "liquidations", manifest.liquidationsIntegrity, true);
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
    bool identitySeen = false;
    bool captureSeen = false;
    bool channelsSeen = false;
    bool artifactsSeen = false;
    bool sessionStatusSeen = false;
    bool arrivalClockSeen = false;
    JsonParser parser{document};
    if (!parser.parseObjectStart()) return Status::CorruptData;
    if (!parser.peek('}')) {
        std::string key;
        do {
            if (!parser.parseKey(key)) return Status::CorruptData;
            if (key == "session_status") {
                if (!parser.parseString(parsed.sessionStatus)) return Status::CorruptData;
                sessionStatusSeen = true;
            } else if (key == "identity") {
                if (!parseIdentityObject(parser, parsed)) return Status::CorruptData;
                identitySeen = true;
            } else if (key == "capture") {
                if (!parseCaptureObject(parser, parsed)) return Status::CorruptData;
                captureSeen = true;
            } else if (key == "arrival_clock") {
                if (!parseArrivalClockObject(parser, parsed.arrivalClock)) return Status::CorruptData;
                arrivalClockSeen = true;
            } else if (key == "replay") {
                if (!parseReplayObject(parser, parsed)) return Status::CorruptData;
            } else if (key == "channels") {
                if (!parseChannelsObject(parser, parsed)) return Status::CorruptData;
                channelsSeen = true;
            } else if (key == "runtime_health") {
                if (!parseRuntimeHealthObject(parser, parsed)) return Status::CorruptData;
            } else if (key == "snapshots") {
                if (!parser.skipValue()) return Status::CorruptData;
            } else if (key == "artifacts") {
                if (!parseArtifactsObject(parser, parsed)) return Status::CorruptData;
                artifactsSeen = true;
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
            } else {
                if (!parser.skipValue()) return Status::CorruptData;
            }
            if (parser.peek('}')) break;
        } while (parser.parseComma());
    }
    if (!parser.parseObjectEnd() || !parser.finish()) return Status::CorruptData;
    if (!identitySeen || !captureSeen || !channelsSeen || !artifactsSeen ||
        !sessionStatusSeen || parsed.sessionStatus.empty() ||
        !arrivalClockSeen ||
        parsed.arrivalClock.boundary != "hft-parser.application-frame-ready" ||
        parsed.arrivalClock.realtimeClock != "CLOCK_REALTIME" ||
        parsed.arrivalClock.monotonicClock != "CLOCK_MONOTONIC") {
        return Status::CorruptData;
    }

    populateCanonicalArtifacts(parsed);
    populateSupportArtifacts(parsed);
    validateStructurally(parsed);
    manifest = std::move(parsed);
    return Status::Ok;
}

}  // namespace hftrec::capture
