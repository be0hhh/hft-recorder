#pragma once

#include <string_view>

#include "core/common/Status.hpp"
#include "core/replay/EventRows.hpp"

namespace hftrec::replay {

// Parse one JSON line emitted by capture/JsonSerializers.cpp.
// On success, `out` is fully populated and Status::Ok is returned.
// On missing/malformed fields, Status::CorruptData.

Status parseTradeLine(std::string_view line, TradeRow& out) noexcept;
Status parseBookTickerLine(std::string_view line, BookTickerRow& out) noexcept;
Status parseDepthLine(std::string_view line, DepthRow& out) noexcept;

// Snapshot is a pretty-printed multi-line document produced by
// renderSnapshotJson (single JSON object).
Status parseSnapshotDocument(std::string_view doc, SnapshotDocument& out) noexcept;

}  // namespace hftrec::replay
