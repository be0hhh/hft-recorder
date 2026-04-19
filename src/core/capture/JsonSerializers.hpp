#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cxet {
namespace composite {
struct TradePublic;
struct BookTickerData;
struct OrderBookSnapshot;
}  // namespace composite
}  // namespace cxet

namespace hftrec::capture {

std::string renderTradeJsonLine(const cxet::composite::TradePublic& trade);

std::string renderBookTickerJsonLine(const cxet::composite::BookTickerData& bookTicker,
                                     const std::vector<std::string>& requestedAliases);

std::string renderDepthJsonLine(const cxet::composite::OrderBookSnapshot& delta);

std::string renderSnapshotJson(const cxet::composite::OrderBookSnapshot& snapshot);

}  // namespace hftrec::capture
