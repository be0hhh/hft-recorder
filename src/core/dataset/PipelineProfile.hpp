#pragma once

#include <cstdint>
#include <string_view>

// Pipeline profile — which compression regime applies (doc/OVERVIEW.md).

namespace hftrec {

enum class PipelineProfile : std::uint8_t {
    OnlineRecording = 1,  // hard real-time path, hot during live capture
    Archive         = 2,  // offline post-processing, maximum ratio
    Replay          = 3,  // fast decode-only, random access
};

inline constexpr std::string_view pipelineProfileToString(PipelineProfile p) noexcept {
    switch (p) {
        case PipelineProfile::OnlineRecording: return "online_recording";
        case PipelineProfile::Archive:         return "archive";
        case PipelineProfile::Replay:          return "replay";
    }
    return "unknown";
}

}  // namespace hftrec
