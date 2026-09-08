#pragma once

#include <vector>

#include "../../Runtime/src/Common/Status.hpp"
#include "../../Runtime/src/Corpus/SessionCorpus.hpp"
#include "BookFrame.hpp"

namespace hftrec::lab {

Status sampleGroundTruthBookFrames(const corpus::SessionCorpus& corpus,
                                   std::size_t topLevelsPerSide,
                                   std::vector<BookFrame>& out) noexcept;

}  // namespace hftrec::lab
