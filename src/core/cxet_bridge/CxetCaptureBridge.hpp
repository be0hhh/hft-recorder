#pragma once

#include "core/common/Status.hpp"

namespace hftrec::cxet_bridge {

class CxetCaptureBridge {
  public:
    Status initialize() noexcept;
};

}  // namespace hftrec::cxet_bridge
