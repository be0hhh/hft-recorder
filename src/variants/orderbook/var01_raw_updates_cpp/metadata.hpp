#pragma once

#include "core/dataset/StreamFamily.hpp"

namespace hftrec::variants::orderbook_var01 {

inline constexpr const char*  kVariantId         = "orderbook/var01_raw_updates_cpp";
inline constexpr StreamFamily kTargetFamily      = StreamFamily::OrderbookUpdates;
inline constexpr const char*  kRepresentationTag = "raw_updates";
inline constexpr const char*  kCodecTag          = "identity";
inline constexpr bool         kOnlineFeasible    = true;

}  // namespace hftrec::variants::orderbook_var01
