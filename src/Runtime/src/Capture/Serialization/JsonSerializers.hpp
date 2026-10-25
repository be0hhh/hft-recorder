#pragma once

#include <cstdint>
#include <string>

namespace hftrec::replay {
struct TradeRow;
struct BookTickerRow;
struct CandleRow;
struct DepthRow;
struct SnapshotDocument;
}

namespace hftrec::capture {

struct EventSequenceIds {
    std::uint64_t captureSeq{0};
    std::uint64_t ingestSeq{0};
};

std::string renderTradeJsonLine(const hftrec::replay::TradeRow& trade);



std::string renderBookTickerJsonLine(const hftrec::replay::BookTickerRow& bookTicker);

std::string renderCandleJsonLine(const hftrec::replay::CandleRow& candle);

std::string renderDepthTapeJsonLine(const hftrec::replay::DepthRow& delta);
std::string renderDepthRleSidecarJsonLine(const hftrec::replay::DepthRow& delta);

std::string renderSnapshotJson(const hftrec::replay::SnapshotDocument& snapshot);

}  // namespace hftrec::capture
