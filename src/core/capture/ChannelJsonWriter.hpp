#pragma once

#include <filesystem>
#include <fstream>
#include <string>

#include "core/capture/ChannelKind.hpp"
#include "core/common/Status.hpp"

namespace hftrec::capture {

class ChannelJsonWriter {
  public:
    Status open(ChannelKind channel, const std::filesystem::path& sessionDir) noexcept;
    Status writeLine(const std::string& jsonLine) noexcept;
    Status writeJson(const std::string& jsonDocument) noexcept;
    Status close() noexcept;

  private:
    std::ofstream stream_{};
    ChannelKind channel_{ChannelKind::Trades};
};

}  // namespace hftrec::capture
