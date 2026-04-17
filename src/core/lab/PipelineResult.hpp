#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

#include "core/validation/ValidationResult.hpp"

namespace hftrec::lab {

struct PipelineResult {
    std::string pipelineId;
    std::size_t inputBytes{0};
    std::size_t outputBytes{0};
    std::uint64_t compressionRatioPpm{1'000'000u};
    std::uint64_t encodeBytesPerSec{0};
    std::uint64_t decodeBytesPerSec{0};
    validation::ValidationResult validation{};
};

}  // namespace hftrec::lab
