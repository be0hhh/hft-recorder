#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/common/Status.hpp"

namespace cxet {
namespace composite {
struct TradeRuntimeV1;
struct BookTickerRuntimeV1;
struct OrderBookDeltaRuntimeV1;
struct OrderBookSnapshot;
struct LiquidationEvent;
struct StreamMeta;
}  // namespace composite
}  // namespace cxet

namespace hftrec::cxet_bridge {

struct CapturedTradeRow {
    std::string symbol{};
    std::uint64_t exchangeId{0};
    std::uint64_t tradeId{0};
    std::uint64_t tsNs{0};
    std::int64_t priceE8{0};
    std::int64_t qtyE8{0};
    std::uint64_t firstTradeId{0};
    std::uint64_t lastTradeId{0};
    std::int64_t quoteQtyE8{0};
    std::int64_t side{0};
    bool isBuyerMaker{false};
    bool sideBuy{false};
};

struct CapturedLiquidationRow {
    std::string symbol{};
    std::uint64_t exchangeId{0};
    std::uint64_t tsNs{0};
    std::int64_t priceE8{0};
    std::int64_t qtyE8{0};
    std::int64_t avgPriceE8{0};
    std::int64_t filledQtyE8{0};
    std::int64_t side{0};
    bool sideBuy{false};
    std::int64_t orderType{0};
    std::int64_t timeInForce{0};
    std::int64_t status{0};
    std::int64_t sourceMode{0};
};

struct CapturedBookTickerRow {
    std::string symbol{};
    std::uint64_t exchangeId{0};
    std::uint64_t tsNs{0};
    std::int64_t bidPriceE8{0};
    std::int64_t askPriceE8{0};
    std::int64_t bidQtyE8{0};
    std::int64_t askQtyE8{0};
    bool includeBidQty{false};
    bool includeAskQty{false};
};

struct CapturedLevel {
    std::int64_t priceI64{0};
    std::int64_t qtyI64{0};
    std::int64_t side{0};
    std::uint64_t levelId{0};
};

struct CapturedOrderBookRow {
    std::string symbol{};
    std::uint64_t exchangeId{0};
    std::uint64_t tsNs{0};
    bool hasUpdateId{false};
    bool hasFirstUpdateId{false};
    std::uint64_t updateId{0};
    std::uint64_t firstUpdateId{0};
    std::vector<CapturedLevel> bids{};
    std::vector<CapturedLevel> asks{};
};

enum class CaptureFailureKind : std::uint8_t {
    SubscribeFailed = 1,
    SnapshotFetchFailed = 2,
    WriteFailed = 3,
};

struct CaptureFailureEvent {
    CaptureFailureKind kind{CaptureFailureKind::SubscribeFailed};
    std::string channel{};
    std::string detail{};
    bool recoverable{false};
};

class CxetCaptureBridge {
  public:
    Status initialize() noexcept;
    static CapturedTradeRow captureTrade(const cxet::composite::TradeRuntimeV1& trade,
                                         const cxet::composite::StreamMeta& meta);
    static CapturedBookTickerRow captureBookTicker(const cxet::composite::BookTickerRuntimeV1& bookTicker,
                                                   const cxet::composite::StreamMeta& meta);
    static CapturedLiquidationRow captureLiquidation(const cxet::composite::LiquidationEvent& event);
    static CapturedOrderBookRow captureOrderBook(const cxet::composite::OrderBookSnapshot& snapshot);
    static CapturedOrderBookRow captureOrderBook(const cxet::composite::OrderBookDeltaRuntimeV1& delta,
                                                 const cxet::composite::StreamMeta& meta);
    static CaptureFailureEvent makeFailure(CaptureFailureKind kind,
                                           std::string channel,
                                           std::string detail,
                                           bool recoverable) noexcept;
};

}  // namespace hftrec::cxet_bridge
