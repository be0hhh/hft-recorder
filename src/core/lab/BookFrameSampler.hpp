#pragma once

#include <vector>

#include "core/common/Status.hpp"
#include "core/corpus/SessionCorpus.hpp"
#include "core/lab/BookFrame.hpp"

namespace hftrec::lab {

Status sampleGroundTruthBookFrames(const corpus::SessionCorpus& corpus,
                                   std::size_t topLevelsPerSide,
                                   std::vector<BookFrame>& out) noexcept;

}  // namespace hftrec::lab
