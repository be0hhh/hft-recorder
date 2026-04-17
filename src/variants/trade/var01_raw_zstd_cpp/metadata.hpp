#pragma once

#include "core/dataset/StreamFamily.hpp"

namespace hftrec::variants::trade_var01 {

inline constexpr const char*  kVariantId         = "trade/var01_raw_zstd_cpp";
inline constexpr StreamFamily kTargetFamily      = StreamFamily::TradeLike;
inline constexpr const char*  kRepresentationTag = "raw_event_bytes";
inline constexpr const char*  kCodecTag          = "zstd";
inline constexpr bool         kOnlineFeasible    = true;

}  // namespace hftrec::variants::trade_var01
