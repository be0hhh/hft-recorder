#pragma once

#include "hftrec/RecorderApi.hpp"
#include "SessionReplay.hpp"

namespace hftrec::detail {

inline bool wants(RecorderChannelMask channels, RecorderChannelMask channel) noexcept {
    return (channels & channel) != 0u;
}

Status openSelectedReplay(const std::filesystem::path& sessionPath,
                          RecorderChannelMask channels,
                          replay::SessionReplay& replay,
                          std::string& error) noexcept;

}  // namespace hftrec::detail
