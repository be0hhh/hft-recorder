#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

#include "core/validation/ValidationResult.hpp"

namespace hftrec::lab {

struct GroundTruthSummary {
    std::size_t framesTotal{0};
    std::size_t framesWithBookTicker{0};
    std::size_t framesWithOrderbook{0};
    std::size_t maxBidLevelsSeen{0};
    std::size_t maxAskLevelsSeen{0};
    std::size_t sampledTopLevelsPerSide{0};
};

struct PipelineResult {
    std::string pipelineId;
    std::size_t inputBytes{0};
    std::size_t outputBytes{0};
    std::uint64_t compressionRatioPpm{1'000'000u};
    std::uint64_t encodeBytesPerSec{0};
    std::uint64_t decodeBytesPerSec{0};
    GroundTruthSummary groundTruth{};
    validation::ValidationResult validation{};
};

}  // namespace hftrec::lab
