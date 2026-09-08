#include "SupportArtifacts.hpp"

#include <cstddef>
#include <sstream>
#include <string_view>

#include "../Common/Integrity.hpp"
#include "../Common/JsonString.hpp"

namespace hftrec::capture {

namespace {

const char* healthToString(SessionHealth health) noexcept {
    switch (health) {
        case SessionHealth::Clean: return "clean";
        case SessionHealth::Degraded: return "degraded";
        case SessionHealth::Corrupt: return "corrupt";
    }
    return "unknown";
}

void appendRuntimeHealth(std::ostringstream& out,
                         std::string_view name,
                         bool enabled,
                         std::uint64_t rows,
                         const ChannelRuntimeHealth& health,
                         bool trailingComma) {
    out << "    \"" << name << "\": {\"enabled\": " << (enabled ? "true" : "false")
        << ", \"required\": " << (health.required ? "true" : "false")
        << ", \"state\": " << json::quote(health.state)
        << ", \"rows\": " << rows
        << ", \"first_row_ns\": " << health.firstRowNs
        << ", \"last_row_ns\": " << health.lastRowNs
        << ", \"reconnect_count\": " << health.reconnectCount
        << ", \"dropped_event_count\": " << health.droppedEventCount
        << ", \"unroutable_event_count\": " << health.unroutableEventCount
        << ", \"last_error\": " << json::quote(health.lastError)
        << "}" << (trailingComma ? "," : "") << "\n";
}


}  // namespace

std::string renderSessionAuditJson(const SessionManifest& manifest, std::int64_t generatedAtNs) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"schema_version\": \"hftrec.support_artifact.session_audit.v1\",\n";
    out << "  \"producer\": \"hft-recorder\",\n";
    out << "  \"generated_at_ns\": " << generatedAtNs << ",\n";
    out << "  \"session_id\": " << json::quote(manifest.sessionId) << ",\n";
    out << "  \"manifest_schema_version\": " << manifest.manifestSchemaVersion << ",\n";
    out << "  \"corpus_schema_version\": " << manifest.corpusSchemaVersion << ",\n";
    out << "  \"session_status\": " << json::quote(manifest.sessionStatus) << ",\n";
    out << "  \"session_health\": " << json::quote(healthToString(manifest.sessionHealth)) << ",\n";
    out << "  \"exact_replay_eligible\": " << (manifest.exactReplayEligible ? "true" : "false") << ",\n";
    out << "  \"summary\": " << json::quote("support artifact only; canonical truth remains the JSON session corpus") << '\n';
    out << "}\n";
    return out.str();
}


std::string renderLoaderDiagnosticsJson(const SessionManifest& manifest, std::int64_t generatedAtNs) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"schema_version\": \"hftrec.support_artifact.loader_diagnostics.v1\",\n";
    out << "  \"producer\": \"hft-recorder\",\n";
    out << "  \"generated_at_ns\": " << generatedAtNs << ",\n";
    out << "  \"session_id\": " << json::quote(manifest.sessionId) << ",\n";
    out << "  \"origin\": " << json::quote("capture_finalize_placeholder") << ",\n";
    out << "  \"summary\": " << json::quote("path reserved for loader-produced diagnostics; current file is a placeholder support artifact") << '\n';
    out << "}\n";
    return out.str();
}

std::string renderMarketDataLaunchJson(const SessionManifest& manifest, std::int64_t generatedAtNs) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"schema_version\": \"hftrec.support_artifact.market_data_launch.v3\",\n";
    out << "  \"producer\": \"hft-recorder\",\n";
    out << "  \"generated_at_ns\": " << generatedAtNs << ",\n";
    out << "  \"session_id\": " << json::quote(manifest.sessionId) << ",\n";
    out << "  \"exchange\": " << json::quote(manifest.exchange) << ",\n";
    out << "  \"market\": " << json::quote(manifest.market) << ",\n";
    out << "  \"symbols\": [";
    for (std::size_t i = 0; i < manifest.symbols.size(); ++i) {
        if (i != 0) out << ',';
        out << json::quote(manifest.symbols[i]);
    }
    out << "],\n";
    out << "  \"session_status\": " << json::quote(manifest.sessionStatus) << ",\n";
    out << "  \"warning_summary\": " << json::quote(manifest.warningSummary) << ",\n";
    out << "  \"channels\": {\n";
    appendRuntimeHealth(out, "trades", manifest.tradesEnabled, manifest.tradesCount, manifest.tradesRuntime, true);
    appendRuntimeHealth(out, "liquidations", manifest.liquidationsEnabled, manifest.liquidationsCount, manifest.liquidationsRuntime, true);
    appendRuntimeHealth(out, "bookticker", manifest.bookTickerEnabled, manifest.bookTickerCount, manifest.bookTickerRuntime, true);
    appendRuntimeHealth(out, "orderbook", manifest.orderbookEnabled, manifest.depthCount, manifest.depthRuntime, true);
    appendRuntimeHealth(out, "mark_price", manifest.markPriceEnabled, manifest.markPriceCount, manifest.markPriceRuntime, true);
    appendRuntimeHealth(out, "index_price", manifest.indexPriceEnabled, manifest.indexPriceCount, manifest.indexPriceRuntime, true);
    appendRuntimeHealth(out, "funding", manifest.fundingEnabled, manifest.fundingCount, manifest.fundingRuntime, true);
    appendRuntimeHealth(out, "price_limit", manifest.priceLimitEnabled, manifest.priceLimitCount, manifest.priceLimitRuntime, false);
    out << "  }\n";
    out << "}\n";
    return out.str();
}

}  // namespace hftrec::capture
