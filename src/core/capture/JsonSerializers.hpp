#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hftrec::replay {
struct TradeRow;
struct BookTickerRow;
struct LiquidationRow;
struct DepthRow;
struct SnapshotDocument;
}

namespace hftrec::capture {

struct EventSequenceIds {
    std::uint64_t captureSeq{0};
    std::uint64_t ingestSeq{0};
};

std::string renderTradeJsonLine(const hftrec::replay::TradeRow& trade);
std::string renderTradeJsonLine(const hftrec::replay::TradeRow& trade,
                                const std::vector<std::string>& aliases);

std::string renderLiquidationJsonLine(const hftrec::replay::LiquidationRow& liquidation);
std::string renderLiquidationJsonLine(const hftrec::replay::LiquidationRow& liquidation,
                                      const std::vector<std::string>& aliases);

std::string renderBookTickerJsonLine(const hftrec::replay::BookTickerRow& bookTicker);
std::string renderBookTickerJsonLine(const hftrec::replay::BookTickerRow& bookTicker,
                                     const std::vector<std::string>& aliases);

std::string renderDepthJsonLine(const hftrec::replay::DepthRow& delta);
std::string renderDepthJsonLine(const hftrec::replay::DepthRow& delta,
                                const std::vector<std::string>& aliases);

std::string renderSnapshotJson(const hftrec::replay::SnapshotDocument& snapshot);

}  // namespace hftrec::capture
