#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/capture/SessionManifest.hpp"
#include "core/corpus/InstrumentMetadata.hpp"
#include "core/corpus/LoadReport.hpp"

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
    std::vector<std::string> depthLines;
    std::string instrumentMetadataDocument;
    std::string sessionAuditDocument;
    std::string integrityReportDocument;
    std::string loaderDiagnosticsDocument;
};

}  // namespace hftrec::corpus
