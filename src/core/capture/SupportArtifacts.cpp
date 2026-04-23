#include "core/capture/SupportArtifacts.hpp"

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

}  // namespace hftrec::capture
