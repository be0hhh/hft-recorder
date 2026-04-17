#pragma once

#include <vector>

#include "core/corpus/SessionCorpus.hpp"
#include "core/lab/PipelineDescriptor.hpp"
#include "core/lab/PipelineResult.hpp"

namespace hftrec::lab {

class LabRunner {
  public:
    std::vector<PipelineResult> run(const corpus::SessionCorpus& corpus,
                                    const std::vector<PipelineDescriptor>& pipelines) const;
};

}  // namespace hftrec::lab
