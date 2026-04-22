#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

#include "core/cxet_bridge/CxetCaptureBridge.hpp"
#include "network/local/hftrecorder/Protocol.hpp"

namespace hftrec::local_exchange {

enum class LocalOrderErrorCode : std::uint32_t {
    None = 0,
    InvalidRequest = 1,
    MissingSymbol = 2,
    MissingQuantity = 3,
    MissingSide = 4,
    UnknownOrderType = 5,
    MissingPrice = 6,
    UnexpectedPriceForMarket = 7,
    MissingBookTicker = 8,
};

class LocalOrderEngine {
  public:
    LocalOrderEngine() = default;

    void reset() noexcept;

    bool submitOrder(const cxet::network::local::hftrecorder::OrderRequestFrame& request,
                     cxet::network::local::hftrecorder::OrderAckFrame& ack) noexcept;

    void onTrade(const cxet_bridge::CapturedTradeRow& trade) noexcept;
    void onBookTicker(const cxet_bridge::CapturedBookTickerRow& bookTicker) noexcept;

    std::uint64_t acceptedCount() const noexcept;

  private:
    struct SymbolMarketData {
        std::int64_t bidPriceE8{0};
        std::int64_t askPriceE8{0};
        std::int64_t bidQtyE8{0};
        std::int64_t askQtyE8{0};
        std::uint64_t bookTsNs{0};
        std::int64_t lastTradePriceE8{0};
        std::int64_t lastTradeQtyE8{0};
        bool lastTradeSideBuy{false};
        std::uint64_t tradeTsNs{0};
    };

    struct LocalOrder {
        std::string orderId{};
        std::string symbol{};
        std::uint8_t exchangeRaw{0};
        std::uint8_t marketRaw{0};
        std::uint8_t sideRaw{0};
        std::uint8_t typeRaw{0};
        std::int64_t quantityRaw{0};
        std::int64_t priceRaw{0};
        std::uint64_t acceptedTsNs{0};
        canon::OrderStatus status{canon::OrderStatus::Unknown};
        bool waitingForMarketFill{false};
        std::int64_t fillPriceE8{0};
        std::uint64_t fillTsNs{0};
    };

    static LocalOrderErrorCode validateRequest_(
        const cxet::network::local::hftrecorder::OrderRequestFrame& request) noexcept;
    static bool isSupportedType_(std::uint8_t typeRaw) noexcept;
    static bool hasSymbol_(const char* symbol) noexcept;
    static std::string makeSymbolKey_(const char* symbol);
    static std::uint64_t nowNs_() noexcept;

    bool tryFillMarketOrder_(LocalOrder& order,
                             const SymbolMarketData* marketData,
                             std::uint64_t tsNs) noexcept;
    bool shouldTriggerOnTrade_(const LocalOrder& order,
                               const cxet_bridge::CapturedTradeRow& trade) const noexcept;
    bool shouldFillLimitOnTrade_(const LocalOrder& order,
                                 const cxet_bridge::CapturedTradeRow& trade) const noexcept;
    void processTradeLocked_(const cxet_bridge::CapturedTradeRow& trade) noexcept;
    void processBookTickerLocked_(const cxet_bridge::CapturedBookTickerRow& bookTicker) noexcept;
    void fillAckFromOrder_(const LocalOrder& order,
                           bool success,
                           LocalOrderErrorCode errorCode,
                           cxet::network::local::hftrecorder::OrderAckFrame& ack) const noexcept;

    mutable std::mutex mutex_{};
    std::unordered_map<std::string, SymbolMarketData> marketBySymbol_{};
    std::unordered_map<std::string, LocalOrder> pendingOrders_{};
    std::uint64_t nextOrderSeq_{0};
    std::uint64_t acceptedCount_{0};
};

LocalOrderEngine& globalLocalOrderEngine() noexcept;

}  // namespace hftrec::local_exchange
