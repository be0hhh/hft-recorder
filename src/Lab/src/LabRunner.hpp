#pragma once

#include <vector>

#include "../../Runtime/src/Corpus/SessionCorpus.hpp"
#include "PipelineDescriptor.hpp"
#include "PipelineResult.hpp"

namespace hftrec::lab {

class LabRunner {
  public:
    std::vector<PipelineResult> run(const corpus::SessionCorpus& corpus,
                                    const std::vector<PipelineDescriptor>& pipelines) const;
};

}  // namespace hftrec::lab
