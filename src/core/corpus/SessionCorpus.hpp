#pragma once

#include <string>
#include <vector>

#include "core/capture/SessionManifest.hpp"

namespace hftrec::corpus {

struct SessionCorpus {
    capture::SessionManifest manifest;
    std::vector<std::string> tradeLines;
    std::vector<std::string> bookTickerLines;
    std::vector<std::string> depthLines;
    std::vector<std::string> snapshotDocuments;
};

}  // namespace hftrec::corpus
