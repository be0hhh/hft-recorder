#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "../Capture/Session/SessionManifest.hpp"
#include "InstrumentMetadata.hpp"
#include "LoadReport.hpp"
#include "../Replay/EventRows.hpp"

namespace hftrec::corpus {

struct SessionCorpus {
    capture::SessionManifest manifest;
    std::optional<InstrumentMetadata> instrumentMetadata;
    LoadReport report;
    // Only counts survive format admission for retired channels and non-FINAM
    // candle archives; no row payload, string or replay event is retained.
    std::array<std::uint64_t, 7u> omittedArtifactRows{};
    std::vector<std::string> tradeLines;
    std::vector<std::string> bookTickerLines;
    std::vector<std::string> candleLines;
    std::vector<std::string> candle2Lines;
    // Depth is decoded exactly once from the paired current tape package.
    // Keeping the typed row preserves captured arrival clocks and sequence
    // evidence; flattening it back to the retired depth.jsonl shape would
    // silently discard that evidence.
    std::vector<replay::DepthRow> depthRows;
    std::vector<std::string> depthTapeLines;
    std::vector<std::string> depthSidecarLines;
    std::string instrumentMetadataDocument;
    std::string sessionAuditDocument;
    std::string integrityReportDocument;
    std::string loaderDiagnosticsDocument;
};

}  // namespace hftrec::corpus
