#pragma once

#include <vector>

#include "core/lab/PipelineResult.hpp"

namespace hftrec::lab {

class RankingEngine {
  public:
    std::vector<PipelineResult> rank(const std::vector<PipelineResult>& input) const;
};

}  // namespace hftrec::lab
