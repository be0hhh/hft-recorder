#pragma once

#include "cxet/Runtime/Market/CommitContract.hpp"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "../../../Common/Status.hpp"

namespace cxet {
namespace composite {
struct Trade;
struct BookTicker;
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

struct CapturedBookTickerRow {
    std::uint64_t eventId{0};
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
};

struct CapturedOrderBookRow {
    std::uint64_t eventId{0};
    std::uint64_t tsNs{0};
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
    static CapturedTradeRow captureTrade(const cxet::runtime::market::TradeCommit& trade,
                                         std::string_view symbol);
    static CapturedBookTickerRow captureBookTicker(const cxet::runtime::market::BboCommit& value,
                                                   std::string_view symbol);
    static CapturedOrderBookRow captureOrderBook(const cxet::runtime::market::CommitMetadata& metadata,
                                                  const cxet::api::market::PublicMarketDepthFrame& value);

    static CaptureFailureEvent makeFailure(CaptureFailureKind kind,
                                           std::string channel,
                                           std::string detail,
                                           bool recoverable) noexcept;
};

}  // namespace hftrec::cxet_bridge
