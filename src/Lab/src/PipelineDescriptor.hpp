#pragma once

#include <string>

#include "../../Runtime/src/Corpus/Dataset/PipelineProfile.hpp"
#include "../../Runtime/src/Corpus/Dataset/StreamFamily.hpp"

namespace hftrec::lab {

struct PipelineDescriptor {
    std::string id;
    std::string representation;
    std::string codec;
    StreamFamily family{StreamFamily::TradeLike};
    PipelineProfile profile{PipelineProfile::Archive};
};

}  // namespace hftrec::lab
