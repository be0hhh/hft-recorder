#pragma once

#include <string>
#include <cstdint>

namespace cxet {
namespace composite {
struct TradePublic;
struct BookTickerData;
struct OrderBookSnapshot;
}  // namespace composite
}  // namespace cxet

namespace hftrec::capture {

std::string renderTradeJsonLine(const std::string& sessionId,
                                const std::string& exchange,
                                const std::string& market,
                                const cxet::composite::TradePublic& trade,
                                std::uint64_t eventIndex);

std::string renderBookTickerJsonLine(const std::string& sessionId,
                                     const std::string& exchange,
                                     const std::string& market,
                                     const cxet::composite::BookTickerData& bookTicker,
                                     std::uint64_t eventIndex);

std::string renderDepthJsonLine(const std::string& sessionId,
                                const std::string& exchange,
                                const std::string& market,
                                const cxet::composite::OrderBookSnapshot& delta,
                                std::uint64_t eventIndex);

std::string renderSnapshotJson(const std::string& sessionId,
                               const std::string& exchange,
                               const std::string& market,
                               const cxet::composite::OrderBookSnapshot& snapshot,
                               std::uint64_t snapshotIndex);

}  // namespace hftrec::capture
