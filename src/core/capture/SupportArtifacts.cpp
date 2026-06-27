#include "core/capture/SupportArtifacts.hpp"

#include <cstddef>
#include <sstream>

#include "core/common/Integrity.hpp"
#include "core/common/JsonString.hpp"

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
    out << "  \"schema_version\": \"hftrec.support_artifact.market_data_launch.v1\",\n";
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
    out << "    \"trades\": {\"enabled\": " << (manifest.tradesEnabled ? "true" : "false")
        << ", \"rows\": " << manifest.tradesCount << "},\n";
    out << "    \"liquidations\": {\"enabled\": " << (manifest.liquidationsEnabled ? "true" : "false")
        << ", \"rows\": " << manifest.liquidationsCount << "},\n";
    out << "    \"bookticker\": {\"enabled\": " << (manifest.bookTickerEnabled ? "true" : "false")
        << ", \"rows\": " << manifest.bookTickerCount << "},\n";
    out << "    \"orderbook\": {\"enabled\": " << (manifest.orderbookEnabled ? "true" : "false")
        << ", \"rows\": " << manifest.depthCount << "},\n";
    out << "    \"mark_price\": {\"enabled\": " << (manifest.markPriceEnabled ? "true" : "false")
        << ", \"rows\": " << manifest.markPriceCount << "},\n";
    out << "    \"index_price\": {\"enabled\": " << (manifest.indexPriceEnabled ? "true" : "false")
        << ", \"rows\": " << manifest.indexPriceCount << "},\n";
    out << "    \"funding\": {\"enabled\": " << (manifest.fundingEnabled ? "true" : "false")
        << ", \"rows\": " << manifest.fundingCount << "},\n";
    out << "    \"price_limit\": {\"enabled\": " << (manifest.priceLimitEnabled ? "true" : "false")
        << ", \"rows\": " << manifest.priceLimitCount << "}\n";
    out << "  }\n";
    out << "}\n";
    return out.str();
}

}  // namespace hftrec::capture
