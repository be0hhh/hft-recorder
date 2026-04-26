#pragma once

#include <filesystem>
#include <string>

#include "core/common/Status.hpp"
#include "core/replay/SessionReplay.hpp"

namespace hftrec::replay {

class CxetReplaySessionLoader {
  public:
    Status loadRenderOnce(const std::filesystem::path& sessionDir,
                          SessionReplay& out,
                          std::string& errorDetail) const noexcept;
};

}  // namespace hftrec::replay
