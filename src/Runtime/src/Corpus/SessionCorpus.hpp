#pragma once

#include <optional>
#include <string>
#include <vector>

#include "../Capture/SessionManifest.hpp"
#include "InstrumentMetadata.hpp"
#include "LoadReport.hpp"
#include "../Replay/EventRows.hpp"

namespace hftrec::corpus {

struct SessionCorpus {
    capture::SessionManifest manifest;
    std::optional<InstrumentMetadata> instrumentMetadata;
    LoadReport report;
    std::vector<std::string> tradeLines;
    std::vector<std::string> liquidationLines;
    std::vector<std::string> bookTickerLines;
    std::vector<std::string> markPriceLines;
    std::vector<std::string> indexPriceLines;
    std::vector<std::string> fundingLines;
    std::vector<std::string> priceLimitLines;
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
