#include "core/capture/SessionManifest.hpp"

#include <sstream>

namespace hftrec::capture {

namespace {

void appendStringArray(std::ostringstream& out, const std::vector<std::string>& values) {
    out << '[';
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) out << ',';
        out << '"' << values[i] << '"';
    }
    out << ']';
}

const char* boolToString(bool value) noexcept {
    return value ? "true" : "false";
}

}  // namespace

std::string renderManifestJson(const SessionManifest& manifest) {
    std::ostringstream out;
    out << "{\n";
    out << "  \"session_id\": \"" << manifest.sessionId << "\",\n";
    out << "  \"exchange\": \"" << manifest.exchange << "\",\n";
    out << "  \"market\": \"" << manifest.market << "\",\n";
    out << "  \"symbols\": ";
    appendStringArray(out, manifest.symbols);
    out << ",\n";
    out << "  \"selected_parent_dir\": \"" << manifest.selectedParentDir << "\",\n";
    out << "  \"started_at_ns\": " << manifest.startedAtNs << ",\n";
    out << "  \"ended_at_ns\": " << manifest.endedAtNs << ",\n";
    out << "  \"target_duration_sec\": " << manifest.targetDurationSec << ",\n";
    out << "  \"actual_duration_sec\": " << manifest.actualDurationSec << ",\n";
    out << "  \"snapshot_interval_sec\": " << manifest.snapshotIntervalSec << ",\n";
    out << "  \"channel_status\": {\n";
    out << "    \"trades_enabled\": " << boolToString(manifest.tradesEnabled) << ",\n";
    out << "    \"bookticker_enabled\": " << boolToString(manifest.bookTickerEnabled) << ",\n";
    out << "    \"orderbook_enabled\": " << boolToString(manifest.orderbookEnabled) << "\n";
    out << "  },\n";
    out << "  \"event_counts\": {\n";
    out << "    \"trades\": " << manifest.tradesCount << ",\n";
    out << "    \"bookticker\": " << manifest.bookTickerCount << ",\n";
    out << "    \"depth\": " << manifest.depthCount << ",\n";
    out << "    \"snapshot\": " << manifest.snapshotCount << "\n";
    out << "  },\n";
    out << "  \"warning_summary\": \"" << manifest.warningSummary << "\"\n";
    out << "}\n";
    return out.str();
}

}  // namespace hftrec::capture
