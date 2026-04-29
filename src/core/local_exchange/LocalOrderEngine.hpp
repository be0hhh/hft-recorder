#pragma once

#include <core/execution/ExecutionVenue.hpp>

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

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
    ReduceOnlyWouldIncrease = 9,
    InsufficientBalance = 10,
    MissingOrderRef = 11,
    UnknownOrder = 12,
    UnsupportedOrderFlag = 13,
};

class LocalOrderEngine : public execution::IExecutionVenue {
  public:
    LocalOrderEngine() = default;

    void reset() noexcept;
    void setEventSink(execution::IExecutionEventSink* sink) noexcept override;

    bool submitOrder(const cxet::network::local::hftrecorder::OrderRequestFrame& request,
                     cxet::network::local::hftrecorder::OrderAckFrame& ack) noexcept;

    void onTrade(const cxet_bridge::CapturedTradeRow& trade) noexcept;
    void onBookTicker(const cxet_bridge::CapturedBookTickerRow& bookTicker) noexcept;

    std::uint64_t acceptedCount() const noexcept;
    std::int64_t walletBalanceRaw() const noexcept;
    std::int64_t positionQtyRaw(const char* symbol) const;

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
        std::uint8_t timeInForceRaw{0};
        bool reduceOnly{false};
        std::int64_t quantityRaw{0};
        std::int64_t priceRaw{0};
        std::uint64_t acceptedTsNs{0};
        canon::OrderStatus status{canon::OrderStatus::Unknown};
        bool waitingForMarketFill{false};
        std::int64_t fillPriceE8{0};
        std::int64_t filledQtyRaw{0};
        std::int64_t feeRaw{0};
        std::int64_t realizedPnlRaw{0};
        std::int64_t positionQtyRaw{0};
        std::int64_t avgEntryPriceE8{0};
        std::string clientOrderId{};
        std::string execId{};
        std::uint64_t fillTsNs{0};
    };

    struct CancelRequest {
        std::string orderId{};
        std::string origClientOrderId{};
        std::uint64_t effectiveTsNs{0};
        std::uint64_t seq{0};
    };

    struct PositionState {
        std::int64_t qtyRaw{0};
        std::int64_t avgEntryPriceE8{0};
        std::int64_t realizedPnlRaw{0};
        std::int64_t unrealizedPnlRaw{0};
    };

    struct AccountState {
        std::int64_t walletBalanceRaw{1000000000000LL};
        std::int64_t availableBalanceRaw{1000000000000LL};
        std::int64_t equityRaw{1000000000000LL};
        std::int64_t feeTotalRaw{0};
        std::int64_t realizedPnlRaw{0};
    };

    static LocalOrderErrorCode validateRequest_(
        const cxet::network::local::hftrecorder::OrderRequestFrame& request) noexcept;
    static bool isSupportedType_(std::uint8_t typeRaw) noexcept;
    static bool hasSymbol_(const char* symbol) noexcept;
    static std::string makeSymbolKey_(const char* symbol);
    static std::uint64_t nowNs_() noexcept;
    static std::uint64_t orderEntryDelayNs_() noexcept;
    static std::uint64_t orderEntryJitterNs_() noexcept;

    static std::int64_t remainingQtyRaw_(const LocalOrder& order) noexcept;
    std::int64_t reduceOnlyFillCapLocked_(const LocalOrder& order, std::int64_t desiredQtyRaw) const noexcept;
    bool tryFillMarketOrder_(LocalOrder& order,
                             const SymbolMarketData* marketData,
                             std::uint64_t tsNs,
                             LocalOrderErrorCode& errorCode) noexcept;
    bool tryFillImmediateLimitOrder_(LocalOrder& order,
                                     const SymbolMarketData* marketData,
                                     std::uint64_t tsNs,
                                     LocalOrderErrorCode& errorCode) noexcept;
    bool tryFillRestingLimitOnTrade_(LocalOrder& order,
                                     const cxet_bridge::CapturedTradeRow& trade) noexcept;
    bool tryFillRestingLimitOnBookTicker_(LocalOrder& order,
                                          const SymbolMarketData* marketData,
                                          std::uint64_t tsNs) noexcept;
    LocalOrderErrorCode canAcceptExposureLocked_(const LocalOrder& order,
                                                 std::int64_t fillPriceE8,
                                                 std::int64_t fillQtyRaw) const noexcept;
    LocalOrderErrorCode validateReduceOnlyLocked_(const LocalOrder& order) const noexcept;
    static std::int64_t scaledNotionalRaw_(std::int64_t priceE8, std::int64_t qtyE8) noexcept;
    void applyFillLocked_(LocalOrder& order,
                          std::int64_t fillPriceE8,
                          std::int64_t fillQtyRaw,
                          std::uint64_t tsNs) noexcept;
    void publishFillLifecycleLocked_(const LocalOrder& order) const noexcept;
    bool shouldTriggerOnTrade_(const LocalOrder& order,
                               const cxet_bridge::CapturedTradeRow& trade) const noexcept;
    bool shouldFillLimitOnTrade_(const LocalOrder& order,
                                 const cxet_bridge::CapturedTradeRow& trade) const noexcept;
    void processTradeLocked_(const cxet_bridge::CapturedTradeRow& trade) noexcept;
    void processBookTickerLocked_(const cxet_bridge::CapturedBookTickerRow& bookTicker) noexcept;
    std::uint64_t sampleOrderDelayNsLocked_() noexcept;
    void processCancelRequestsLocked_(std::uint64_t venueTsNs) noexcept;
    bool cancelOrderLocked_(const cxet::network::local::hftrecorder::OrderRequestFrame& request,
                            std::uint64_t effectiveTsNs,
                            bool immediate,
                            cxet::network::local::hftrecorder::OrderAckFrame& ack) noexcept;
    void fillAckFromOrder_(const LocalOrder& order,
                           bool success,
                           LocalOrderErrorCode errorCode,
                           cxet::network::local::hftrecorder::OrderAckFrame& ack) const noexcept;
    void publishExecutionEventLocked_(const LocalOrder& order,
                                      execution::ExecutionEventKind kind,
                                      bool success,
                                      LocalOrderErrorCode errorCode) const noexcept;

    mutable std::mutex mutex_{};
    std::unordered_map<std::string, SymbolMarketData> marketBySymbol_{};
    std::unordered_map<std::string, PositionState> positionBySymbol_{};
    std::unordered_map<std::string, LocalOrder> pendingOrders_{};
    std::unordered_map<std::string, std::string> clientOrderToOrderId_{};
    std::vector<CancelRequest> cancelRequests_{};
    AccountState account_{};
    std::uint64_t nextOrderSeq_{0};
    std::uint64_t nextCancelSeq_{0};
    std::uint64_t currentVenueTsNs_{0};
    std::uint64_t rngState_{88172645463325252ull};
    std::uint64_t nextExecSeq_{0};
    std::uint64_t acceptedCount_{0};
    execution::IExecutionEventSink* eventSink_{nullptr};
};

LocalOrderEngine& globalLocalOrderEngine() noexcept;

}  // namespace hftrec::local_exchange
