#pragma once

#include "hftrec/RecorderApi.hpp"
#include "SessionReplay.hpp"

namespace hftrec::detail {

RecorderTradeRow convert(const replay::TradeRow& row);
RecorderLiquidationRow convert(const replay::LiquidationRow& row);
RecorderBookTickerRow convert(const replay::BookTickerRow& row);
RecorderCandleRow convert(const replay::CandleRow& row);
RecorderDepthRow convert(const replay::DepthRow& row);
RecorderSnapshotDocument convert(const replay::SnapshotDocument& row);
RecorderEventKind convert(replay::SessionReplay::EventKind kind) noexcept;
bool eventWanted(replay::SessionReplay::EventKind kind, RecorderChannelMask channels) noexcept;

}  // namespace hftrec::detail
