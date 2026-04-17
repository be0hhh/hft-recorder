#pragma once

#include "core/dataset/StreamFamily.hpp"

namespace hftrec::variants::l1_var01 {

inline constexpr const char*  kVariantId         = "l1/var01_raw_lz4_cpp";
inline constexpr StreamFamily kTargetFamily      = StreamFamily::L1BookTicker;
inline constexpr const char*  kRepresentationTag = "raw_event_bytes";
inline constexpr const char*  kCodecTag          = "lz4";
inline constexpr bool         kOnlineFeasible    = true;

}  // namespace hftrec::variants::l1_var01
