#pragma once

#include <string>

#include "core/dataset/PipelineProfile.hpp"
#include "core/dataset/StreamFamily.hpp"

namespace hftrec::lab {

struct PipelineDescriptor {
    std::string id;
    std::string representation;
    std::string codec;
    StreamFamily family{StreamFamily::TradeLike};
    PipelineProfile profile{PipelineProfile::Archive};
};

}  // namespace hftrec::lab
