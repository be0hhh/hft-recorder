#include "core/lab/RankingEngine.hpp"

#include <algorithm>

namespace hftrec::lab {

std::vector<PipelineResult> RankingEngine::rank(const std::vector<PipelineResult>& input) const {
    auto sorted = input;
    std::sort(sorted.begin(), sorted.end(), [](const PipelineResult& lhs, const PipelineResult& rhs) {
        if (lhs.supported != rhs.supported) {
            return lhs.supported && !rhs.supported;
        }
        if (lhs.validation.accuracyPpm != rhs.validation.accuracyPpm) {
            return lhs.validation.accuracyPpm > rhs.validation.accuracyPpm;
        }
        if (lhs.compressionRatioPpm != rhs.compressionRatioPpm) {
            return lhs.compressionRatioPpm < rhs.compressionRatioPpm;
        }
        if (lhs.decodeBytesPerSec != rhs.decodeBytesPerSec) {
            return lhs.decodeBytesPerSec > rhs.decodeBytesPerSec;
        }
        return lhs.encodeBytesPerSec > rhs.encodeBytesPerSec;
    });
    return sorted;
}

}  // namespace hftrec::lab
