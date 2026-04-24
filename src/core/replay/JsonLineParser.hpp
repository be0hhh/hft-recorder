#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "core/common/Status.hpp"
#include "core/replay/EventRows.hpp"

namespace hftrec::replay {

// Parse one JSON line emitted by capture/JsonSerializers.cpp.
// On success, `out` is fully populated and Status::Ok is returned.
// On missing/malformed fields, Status::CorruptData.

Status parseTradeLine(std::string_view line, TradeRow& out) noexcept;
Status parseTradeLine(std::string_view line,
                      TradeRow& out,
                      const std::vector<std::string>& aliases) noexcept;
Status parseBookTickerLine(std::string_view line, BookTickerRow& out) noexcept;
Status parseBookTickerLine(std::string_view line,
                           BookTickerRow& out,
                           const std::vector<std::string>& aliases) noexcept;
Status parseDepthLine(std::string_view line, DepthRow& out) noexcept;
Status parseDepthLine(std::string_view line,
                      DepthRow& out,
                      const std::vector<std::string>& aliases) noexcept;

// Snapshot is the positional JSON array produced by renderSnapshotJson.
Status parseSnapshotDocument(std::string_view doc, SnapshotDocument& out) noexcept;

}  // namespace hftrec::replay
