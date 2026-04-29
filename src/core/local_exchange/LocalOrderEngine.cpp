#include "core/local_exchange/LocalOrderEngine.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <vector>

#include "canon/Enums.hpp"
#include "composite/level_0/SendWsObject.hpp"
#include "primitives/buf/SmallBuf.hpp"
#include "primitives/buf/Symbol.hpp"
#include "primitives/buf/UnifiedRequestSpec.hpp"

namespace hftrec::local_exchange {
namespace {

constexpr std::uint8_t kSideSell = 0u;
constexpr std::uint8_t kSideBuy = 1u;
constexpr std::int64_t kInitialBalanceRaw = 1000000000000LL;
constexpr std::int64_t kTakerFeeRateE8 = 40000LL;
constexpr std::int64_t kScaleE8 = 100000000LL;

void copyBounded(char* out, std::size_t outSize, const std::string& text) noexcept {
    if (outSize == 0u) return;
    out[0] = '\0';
    std::size_t len = text.size();
    if (len >= outSize) len = outSize - 1u;
    if (len > 0u) std::memcpy(out, text.data(), len);
    out[len] = '\0';
}

std::int64_t envI64OrDefault(const char* name, std::int64_t fallback) noexcept {
    const char* raw = std::getenv(name);
    if (raw == nullptr || raw[0] == '\0') return fallback;
    char* end = nullptr;
    const long long parsed = std::strtoll(raw, &end, 10);
    if (end == raw) return fallback;
    return static_cast<std::int64_t>(parsed);
}

std::int64_t initialBalanceRaw() noexcept {
    return envI64OrDefault("HFTREC_LOCAL_INITIAL_BALANCE_RAW", kInitialBalanceRaw);
}

std::int64_t takerFeeRateE8() noexcept {
    return envI64OrDefault("HFTREC_LOCAL_TAKER_FEE_RATE_E8", kTakerFeeRateE8);
}

std::uint64_t envU64OrDefault(const char* name, std::uint64_t fallback) noexcept {
    const char* raw = std::getenv(name);
    if (raw == nullptr || raw[0] == '\0') return fallback;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(raw, &end, 10);
    if (end == raw) return fallback;
    return static_cast<std::uint64_t>(parsed);
}

}  // namespace

void LocalOrderEngine::reset() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    marketBySymbol_.clear();
    positionBySymbol_.clear();
    pendingOrders_.clear();
    clientOrderToOrderId_.clear();
    cancelRequests_.clear();
    account_ = AccountState{};
    const std::int64_t initialBalance = initialBalanceRaw();
    account_.walletBalanceRaw = initialBalance;
    account_.availableBalanceRaw = initialBalance;
    account_.equityRaw = initialBalance;
    nextOrderSeq_ = 0u;
    nextCancelSeq_ = 0u;
    currentVenueTsNs_ = 0u;
    rngState_ = 88172645463325252ull;
    nextExecSeq_ = 0u;
    acceptedCount_ = 0u;
}

void LocalOrderEngine::setEventSink(execution::IExecutionEventSink* sink) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    eventSink_ = sink;
}

bool LocalOrderEngine::submitOrder(
    const cxet::network::local::hftrecorder::OrderRequestFrame& request,
    cxet::network::local::hftrecorder::OrderAckFrame& ack) noexcept {
    ack = cxet::network::local::hftrecorder::OrderAckFrame{};

    const LocalOrderErrorCode validation = validateRequest_(request);
    if (validation != LocalOrderErrorCode::None) {
        LocalOrder rejected{};
        rejected.exchangeRaw = request.exchangeRaw;
        rejected.marketRaw = request.marketRaw;
        rejected.symbol = makeSymbolKey_(request.symbol);
        rejected.clientOrderId = makeSymbolKey_(request.clientOrderId);
        rejected.status = canon::OrderStatus::Rejected;
        rejected.acceptedTsNs = nowNs_();
        std::lock_guard<std::mutex> lock(mutex_);
        fillAckFromOrder_(rejected, false, validation, ack);
        if (eventSink_ != nullptr) {
            eventSink_->onExecutionEvent(execution::ExecutionEvent{
                .kind = execution::ExecutionEventKind::Reject,
                .symbol = rejected.symbol,
                .orderId = rejected.orderId,
                .clientOrderId = rejected.clientOrderId,
                .execId = rejected.execId,
                .exchangeRaw = rejected.exchangeRaw,
                .marketRaw = rejected.marketRaw,
                .sideRaw = request.sideRaw,
                .typeRaw = request.typeRaw,
                .timeInForceRaw = request.timeInForceRaw,
                .reduceOnly = request.reduceOnly,
                .statusRaw = static_cast<std::uint8_t>(rejected.status),
                .errorCode = static_cast<std::uint32_t>(validation),
                .quantityRaw = request.quantityRaw,
                .priceRaw = request.priceRaw,
                .walletBalanceRaw = account_.walletBalanceRaw,
                .availableBalanceRaw = account_.availableBalanceRaw,
                .equityRaw = account_.equityRaw,
                .tsNs = rejected.acceptedTsNs,
                .success = false,
           });
        }
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const std::uint64_t baseVenueTsNs = currentVenueTsNs_ == 0u ? nowNs_() : currentVenueTsNs_;
    const std::uint64_t delayNs = sampleOrderDelayNsLocked_();
    const std::uint64_t effectiveTsNs = baseVenueTsNs + delayNs;
    if (request.typeRaw == static_cast<std::uint8_t>(canon::OrderType::Cancel)) {
        return cancelOrderLocked_(request, effectiveTsNs, delayNs == 0u, ack);
    }

    LocalOrder order{};
    ++nextOrderSeq_;
    char idBuf[SmallBuf::capacity]{};
    std::snprintf(idBuf, sizeof(idBuf), "hftrec-%llu", static_cast<unsigned long long>(nextOrderSeq_));
    order.orderId = idBuf;
    order.symbol = makeSymbolKey_(request.symbol);
    order.exchangeRaw = request.exchangeRaw;
    order.marketRaw = request.marketRaw;
    order.sideRaw = request.sideRaw;
    order.typeRaw = request.typeRaw;
    order.timeInForceRaw = request.timeInForceRaw;
    order.reduceOnly = request.reduceOnlySet != 0u && request.reduceOnly != 0u;
    order.quantityRaw = request.quantityRaw;
    order.priceRaw = request.priceRaw;
    order.clientOrderId = makeSymbolKey_(request.clientOrderId);
    order.acceptedTsNs = effectiveTsNs;
    order.status = delayNs == 0u ? canon::OrderStatus::Placed : canon::OrderStatus::Pending;

    if (delayNs == 0u) {
        const auto marketIt = marketBySymbol_.find(order.symbol);
        const SymbolMarketData* marketData = marketIt == marketBySymbol_.end() ? nullptr : &marketIt->second;
        LocalOrderErrorCode fillError = LocalOrderErrorCode::None;
        if (order.typeRaw == static_cast<std::uint8_t>(canon::OrderType::Market)) {
            if (!tryFillMarketOrder_(order, marketData, order.acceptedTsNs, fillError)) {
                order.status = canon::OrderStatus::Rejected;
                fillAckFromOrder_(order, false, fillError, ack);
                publishExecutionEventLocked_(order,
                                             execution::ExecutionEventKind::Reject,
                                             false,
                                             fillError);
                return false;
            }
        } else if (order.typeRaw == static_cast<std::uint8_t>(canon::OrderType::Limit)) {
            (void)tryFillImmediateLimitOrder_(order, marketData, order.acceptedTsNs, fillError);
            if (fillError != LocalOrderErrorCode::None) {
                order.status = canon::OrderStatus::Rejected;
                fillAckFromOrder_(order, false, fillError, ack);
                publishExecutionEventLocked_(order,
                                             execution::ExecutionEventKind::Reject,
                                             false,
                                             fillError);
                return false;
            }
        }
    }
    if (delayNs != 0u && order.typeRaw == static_cast<std::uint8_t>(canon::OrderType::Market)) {
        order.waitingForMarketFill = true;
    }

    const bool filledBeforeAck = order.filledQtyRaw > 0;
    ++acceptedCount_;
    fillAckFromOrder_(order, true, LocalOrderErrorCode::None, ack);
    publishExecutionEventLocked_(order,
                                 execution::ExecutionEventKind::Ack,
                                 true,
                                 LocalOrderErrorCode::None);
    if (filledBeforeAck) publishFillLifecycleLocked_(order);
    if (order.status != canon::OrderStatus::Closed) {
        if (!order.clientOrderId.empty()) clientOrderToOrderId_[order.clientOrderId] = order.orderId;
        pendingOrders_.emplace(order.orderId, std::move(order));
    }
    return true;
}
void LocalOrderEngine::onTrade(const cxet_bridge::CapturedTradeRow& trade) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (trade.tsNs > currentVenueTsNs_) currentVenueTsNs_ = trade.tsNs;
    processCancelRequestsLocked_(currentVenueTsNs_);
    auto& market = marketBySymbol_[trade.symbol];
    market.lastTradePriceE8 = trade.priceE8;
    market.lastTradeQtyE8 = trade.qtyE8;
    market.lastTradeSideBuy = trade.sideBuy;
    market.tradeTsNs = trade.tsNs;
    processTradeLocked_(trade);
}

void LocalOrderEngine::onBookTicker(const cxet_bridge::CapturedBookTickerRow& bookTicker) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (bookTicker.tsNs > currentVenueTsNs_) currentVenueTsNs_ = bookTicker.tsNs;
    processCancelRequestsLocked_(currentVenueTsNs_);
    auto& market = marketBySymbol_[bookTicker.symbol];
    market.bidPriceE8 = bookTicker.bidPriceE8;
    market.askPriceE8 = bookTicker.askPriceE8;
    market.bidQtyE8 = bookTicker.bidQtyE8;
    market.askQtyE8 = bookTicker.askQtyE8;
    market.bookTsNs = bookTicker.tsNs;
    processBookTickerLocked_(bookTicker);
}

std::uint64_t LocalOrderEngine::acceptedCount() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return acceptedCount_;
}

std::int64_t LocalOrderEngine::walletBalanceRaw() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return account_.walletBalanceRaw;
}

std::int64_t LocalOrderEngine::positionQtyRaw(const char* symbol) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = positionBySymbol_.find(makeSymbolKey_(symbol));
    return it == positionBySymbol_.end() ? 0 : it->second.qtyRaw;
}

LocalOrderErrorCode LocalOrderEngine::validateRequest_(
    const cxet::network::local::hftrecorder::OrderRequestFrame& request) noexcept {
    if (!cxet::network::local::hftrecorder::isValidRequest(request)) return LocalOrderErrorCode::InvalidRequest;
    if (request.operationRaw != static_cast<std::uint8_t>(cxet::UnifiedRequestSpec::Operation::SendWs)) {
        return LocalOrderErrorCode::InvalidRequest;
    }
    if (request.objectRaw != static_cast<std::uint8_t>(cxet::composite::out::WsSendObject::Order)) {
        return LocalOrderErrorCode::InvalidRequest;
    }
    if (!hasSymbol_(request.symbol)) return LocalOrderErrorCode::MissingSymbol;
    if (request.typeRaw == static_cast<std::uint8_t>(canon::OrderType::Cancel)) {
        if (request.orderId[0] == '\0' && request.origClientOrderId[0] == '\0') return LocalOrderErrorCode::MissingOrderRef;
        return LocalOrderErrorCode::None;
    }
    if (request.quantityRaw <= 0) return LocalOrderErrorCode::MissingQuantity;
    if (request.sideSet == 0u || (request.sideRaw != kSideSell && request.sideRaw != kSideBuy)) {
        return LocalOrderErrorCode::MissingSide;
    }
    if (!isSupportedType_(request.typeRaw)) return LocalOrderErrorCode::UnknownOrderType;
    if (request.typeRaw == static_cast<std::uint8_t>(canon::OrderType::Market)) {
        return request.priceRaw == 0 ? LocalOrderErrorCode::None : LocalOrderErrorCode::UnexpectedPriceForMarket;
    }
    return request.priceRaw > 0 ? LocalOrderErrorCode::None : LocalOrderErrorCode::MissingPrice;
}
bool LocalOrderEngine::isSupportedType_(std::uint8_t typeRaw) noexcept {
    return typeRaw == static_cast<std::uint8_t>(canon::OrderType::Market) ||
           typeRaw == static_cast<std::uint8_t>(canon::OrderType::Limit) ||
           typeRaw == static_cast<std::uint8_t>(canon::OrderType::Stop) ||
           typeRaw == static_cast<std::uint8_t>(canon::OrderType::StopLoss);
}

bool LocalOrderEngine::hasSymbol_(const char* symbol) noexcept {
    return symbol != nullptr && symbol[0] != '\0';
}

std::string LocalOrderEngine::makeSymbolKey_(const char* symbol) {
    if (symbol == nullptr) return {};
    return std::string(symbol, strnlen(symbol, Symbol::capacity));
}

std::uint64_t LocalOrderEngine::nowNs_() noexcept {
    timespec ts{};
    if (::clock_gettime(CLOCK_REALTIME, &ts) != 0) return 0u;
    return static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ull +
           static_cast<std::uint64_t>(ts.tv_nsec);
}

std::int64_t LocalOrderEngine::remainingQtyRaw_(const LocalOrder& order) noexcept {
    if (order.quantityRaw <= order.filledQtyRaw) return 0;
    return order.quantityRaw - order.filledQtyRaw;
}

bool LocalOrderEngine::tryFillMarketOrder_(LocalOrder& order,
                                           const SymbolMarketData* marketData,
                                           std::uint64_t tsNs,
                                           LocalOrderErrorCode& errorCode) noexcept {
    errorCode = LocalOrderErrorCode::None;
    const std::int64_t remainingQty = remainingQtyRaw_(order);
    if (remainingQty <= 0) return false;
    if (marketData == nullptr) {
        errorCode = LocalOrderErrorCode::MissingBookTicker;
        return false;
    }

    std::int64_t fillPriceE8 = 0;
    std::int64_t availableQtyRaw = 0;
    if (order.sideRaw == kSideBuy) {
        fillPriceE8 = marketData->askPriceE8;
        availableQtyRaw = marketData->askQtyE8;
    } else {
        fillPriceE8 = marketData->bidPriceE8;
        availableQtyRaw = marketData->bidQtyE8;
    }
    if (fillPriceE8 <= 0 || availableQtyRaw <= 0) {
        errorCode = LocalOrderErrorCode::MissingBookTicker;
        return false;
    }

    const std::int64_t fillQtyRaw = availableQtyRaw < remainingQty ? availableQtyRaw : remainingQty;
    const LocalOrderErrorCode accept = canAcceptExposureLocked_(order, fillPriceE8, fillQtyRaw);
    if (accept != LocalOrderErrorCode::None) {
        errorCode = accept;
        return false;
    }
    applyFillLocked_(order, fillPriceE8, fillQtyRaw, tsNs);
    if (remainingQtyRaw_(order) > 0) order.waitingForMarketFill = true;
    return true;
}

bool LocalOrderEngine::tryFillImmediateLimitOrder_(LocalOrder& order,
                                                   const SymbolMarketData* marketData,
                                                   std::uint64_t tsNs,
                                                   LocalOrderErrorCode& errorCode) noexcept {
    errorCode = LocalOrderErrorCode::None;
    if (order.typeRaw != static_cast<std::uint8_t>(canon::OrderType::Limit)) return false;
    const std::int64_t remainingQty = remainingQtyRaw_(order);
    if (remainingQty <= 0 || marketData == nullptr) return false;

    std::int64_t fillPriceE8 = 0;
    std::int64_t availableQtyRaw = 0;
    bool marketable = false;
    if (order.sideRaw == kSideBuy) {
        fillPriceE8 = marketData->askPriceE8;
        availableQtyRaw = marketData->askQtyE8;
        marketable = fillPriceE8 > 0 && fillPriceE8 <= order.priceRaw;
    } else {
        fillPriceE8 = marketData->bidPriceE8;
        availableQtyRaw = marketData->bidQtyE8;
        marketable = fillPriceE8 > 0 && fillPriceE8 >= order.priceRaw;
    }
    if (!marketable || availableQtyRaw <= 0) return false;

    const std::int64_t fillQtyRaw = availableQtyRaw < remainingQty ? availableQtyRaw : remainingQty;
    const LocalOrderErrorCode accept = canAcceptExposureLocked_(order, fillPriceE8, fillQtyRaw);
    if (accept != LocalOrderErrorCode::None) {
        errorCode = accept;
        return false;
    }
    applyFillLocked_(order, fillPriceE8, fillQtyRaw, tsNs);
    return true;
}

bool LocalOrderEngine::tryFillRestingLimitOnTrade_(LocalOrder& order,
                                                   const cxet_bridge::CapturedTradeRow& trade) noexcept {
    if (!shouldFillLimitOnTrade_(order, trade)) return false;
    const std::int64_t remainingQty = remainingQtyRaw_(order);
    if (remainingQty <= 0 || trade.qtyE8 <= 0) return false;
    const std::int64_t fillQtyRaw = trade.qtyE8 < remainingQty ? trade.qtyE8 : remainingQty;
    const LocalOrderErrorCode accept = canAcceptExposureLocked_(order, order.priceRaw, fillQtyRaw);
    if (accept != LocalOrderErrorCode::None) return false;
    applyFillLocked_(order, order.priceRaw, fillQtyRaw, trade.tsNs);
    return true;
}

bool LocalOrderEngine::tryFillRestingLimitOnBookTicker_(LocalOrder& order,
                                                        const SymbolMarketData* marketData,
                                                        std::uint64_t tsNs) noexcept {
    if (order.typeRaw != static_cast<std::uint8_t>(canon::OrderType::Limit)) return false;
    const std::int64_t remainingQty = remainingQtyRaw_(order);
    if (remainingQty <= 0 || marketData == nullptr) return false;

    std::int64_t availableQtyRaw = 0;
    bool passThrough = false;
    if (order.sideRaw == kSideBuy) {
        availableQtyRaw = marketData->askQtyE8;
        passThrough = marketData->askPriceE8 > 0 && marketData->askPriceE8 < order.priceRaw;
    } else {
        availableQtyRaw = marketData->bidQtyE8;
        passThrough = marketData->bidPriceE8 > 0 && marketData->bidPriceE8 > order.priceRaw;
    }
    if (!passThrough || availableQtyRaw <= 0) return false;

    const std::int64_t fillQtyRaw = availableQtyRaw < remainingQty ? availableQtyRaw : remainingQty;
    const LocalOrderErrorCode accept = canAcceptExposureLocked_(order, order.priceRaw, fillQtyRaw);
    if (accept != LocalOrderErrorCode::None) return false;
    applyFillLocked_(order, order.priceRaw, fillQtyRaw, tsNs);
    return true;
}

LocalOrderErrorCode LocalOrderEngine::canAcceptExposureLocked_(const LocalOrder& order,
                                                               std::int64_t fillPriceE8,
                                                               std::int64_t fillQtyRaw) const noexcept {
    if (fillQtyRaw <= 0) return LocalOrderErrorCode::MissingQuantity;
    const auto posIt = positionBySymbol_.find(order.symbol);
    const std::int64_t currentQty = posIt == positionBySymbol_.end() ? 0 : posIt->second.qtyRaw;
    const std::int64_t signedQty = order.sideRaw == kSideBuy ? fillQtyRaw : -fillQtyRaw;
    const bool increases = currentQty == 0 || (currentQty > 0 && signedQty > 0) || (currentQty < 0 && signedQty < 0) ||
                           ((currentQty > 0 && signedQty < 0) ? -signedQty > currentQty : (currentQty < 0 && signedQty > 0 ? signedQty > -currentQty : false));
    if (order.reduceOnly && increases) return LocalOrderErrorCode::ReduceOnlyWouldIncrease;
    if (increases) {
        const std::int64_t notional = scaledNotionalRaw_(fillPriceE8, fillQtyRaw);
        if (notional > account_.availableBalanceRaw) return LocalOrderErrorCode::InsufficientBalance;
    }
    return LocalOrderErrorCode::None;
}

std::int64_t LocalOrderEngine::scaledNotionalRaw_(std::int64_t priceE8, std::int64_t qtyE8) noexcept {
    const std::int64_t absPrice = priceE8 < 0 ? -priceE8 : priceE8;
    const std::int64_t absQty = qtyE8 < 0 ? -qtyE8 : qtyE8;
    const __int128 product = static_cast<__int128>(absPrice) * static_cast<__int128>(absQty);
    return static_cast<std::int64_t>(product / kScaleE8);
}

void LocalOrderEngine::applyFillLocked_(LocalOrder& order,
                                        std::int64_t fillPriceE8,
                                        std::int64_t fillQtyRaw,
                                        std::uint64_t tsNs) noexcept {
    if (fillQtyRaw <= 0) return;
    ++nextExecSeq_;
    char idBuf[SmallBuf::capacity]{};
    std::snprintf(idBuf, sizeof(idBuf), "hftfill-%llu", static_cast<unsigned long long>(nextExecSeq_));
    order.execId = idBuf;

    PositionState& pos = positionBySymbol_[order.symbol];
    const std::int64_t signedQty = order.sideRaw == kSideBuy ? fillQtyRaw : -fillQtyRaw;
    const std::int64_t oldQty = pos.qtyRaw;
    const std::int64_t absQty = fillQtyRaw < 0 ? -fillQtyRaw : fillQtyRaw;
    const std::int64_t notional = scaledNotionalRaw_(fillPriceE8, absQty);
    order.fillPriceE8 = fillPriceE8;
    order.realizedPnlRaw = 0;
    order.feeRaw = static_cast<std::int64_t>((static_cast<__int128>(notional) * takerFeeRateE8()) / kScaleE8);
    account_.feeTotalRaw += order.feeRaw;
    account_.walletBalanceRaw -= order.feeRaw;

    if (oldQty == 0 || (oldQty > 0 && signedQty > 0) || (oldQty < 0 && signedQty < 0)) {
        const std::int64_t oldAbs = oldQty < 0 ? -oldQty : oldQty;
        const std::int64_t newAbs = oldAbs + absQty;
        pos.avgEntryPriceE8 = newAbs == 0 ? 0 : ((pos.avgEntryPriceE8 * oldAbs) + (fillPriceE8 * absQty)) / newAbs;
        pos.qtyRaw = oldQty + signedQty;
        account_.availableBalanceRaw -= notional;
    } else {
        const std::int64_t oldAbs = oldQty < 0 ? -oldQty : oldQty;
        const std::int64_t closeQty = absQty < oldAbs ? absQty : oldAbs;
        if (oldQty > 0) order.realizedPnlRaw = scaledNotionalRaw_(fillPriceE8 - pos.avgEntryPriceE8, closeQty);
        else order.realizedPnlRaw = scaledNotionalRaw_(pos.avgEntryPriceE8 - fillPriceE8, closeQty);
        pos.realizedPnlRaw += order.realizedPnlRaw;
        account_.realizedPnlRaw += order.realizedPnlRaw;
        account_.walletBalanceRaw += order.realizedPnlRaw;
        account_.availableBalanceRaw += scaledNotionalRaw_(pos.avgEntryPriceE8, closeQty) + order.realizedPnlRaw;
        pos.qtyRaw = oldQty + signedQty;
        if ((oldQty > 0 && pos.qtyRaw < 0) || (oldQty < 0 && pos.qtyRaw > 0)) {
            pos.avgEntryPriceE8 = fillPriceE8;
        } else if (pos.qtyRaw == 0) {
            pos.avgEntryPriceE8 = 0;
        }
    }
    account_.equityRaw = account_.walletBalanceRaw;
    order.filledQtyRaw += fillQtyRaw;
    if (order.filledQtyRaw > order.quantityRaw) order.filledQtyRaw = order.quantityRaw;
    order.positionQtyRaw = pos.qtyRaw;
    order.avgEntryPriceE8 = pos.avgEntryPriceE8;
    order.fillTsNs = tsNs;
    if (remainingQtyRaw_(order) == 0) {
        order.waitingForMarketFill = false;
        order.status = canon::OrderStatus::Closed;
    } else {
        order.status = canon::OrderStatus::PartiallyFilled;
    }
}

void LocalOrderEngine::publishFillLifecycleLocked_(const LocalOrder& order) const noexcept {
    publishExecutionEventLocked_(order,
                                 execution::ExecutionEventKind::StateChange,
                                 true,
                                 LocalOrderErrorCode::None);
    publishExecutionEventLocked_(order,
                                 execution::ExecutionEventKind::Fill,
                                 true,
                                 LocalOrderErrorCode::None);
    publishExecutionEventLocked_(order,
                                 execution::ExecutionEventKind::PositionChange,
                                 true,
                                 LocalOrderErrorCode::None);
    publishExecutionEventLocked_(order,
                                 execution::ExecutionEventKind::BalanceChange,
                                 true,
                                 LocalOrderErrorCode::None);
}

bool LocalOrderEngine::shouldTriggerOnTrade_(const LocalOrder& order,
                                             const cxet_bridge::CapturedTradeRow& trade) const noexcept {
    if (order.typeRaw == static_cast<std::uint8_t>(canon::OrderType::Stop)) {
        return order.sideRaw == kSideBuy ? trade.priceE8 >= order.priceRaw : trade.priceE8 <= order.priceRaw;
    }
    if (order.typeRaw == static_cast<std::uint8_t>(canon::OrderType::StopLoss)) {
        return order.sideRaw == kSideBuy ? trade.priceE8 >= order.priceRaw : trade.priceE8 <= order.priceRaw;
    }
    return false;
}

bool LocalOrderEngine::shouldFillLimitOnTrade_(const LocalOrder& order,
                                               const cxet_bridge::CapturedTradeRow& trade) const noexcept {
    if (order.typeRaw != static_cast<std::uint8_t>(canon::OrderType::Limit)) return false;
    if (order.sideRaw == kSideBuy) return !trade.sideBuy && trade.priceE8 <= order.priceRaw;
    return trade.sideBuy && trade.priceE8 >= order.priceRaw;
}
void LocalOrderEngine::processTradeLocked_(const cxet_bridge::CapturedTradeRow& trade) noexcept {
    const auto marketIt = marketBySymbol_.find(trade.symbol);
    const SymbolMarketData* marketData = marketIt == marketBySymbol_.end() ? nullptr : &marketIt->second;
    std::vector<std::string> closed{};
    for (auto& entry : pendingOrders_) {
        LocalOrder& order = entry.second;
        if (order.symbol != trade.symbol) continue;
        if (order.acceptedTsNs > trade.tsNs) continue;
        if (order.status == canon::OrderStatus::Pending) {
            order.status = canon::OrderStatus::Placed;
            publishExecutionEventLocked_(order,
                                         execution::ExecutionEventKind::StateChange,
                                         true,
                                         LocalOrderErrorCode::None);
        }
        if (tryFillRestingLimitOnTrade_(order, trade)) {
            publishFillLifecycleLocked_(order);
            if (order.status == canon::OrderStatus::Closed) closed.push_back(entry.first);
            continue;
        }
        if (shouldTriggerOnTrade_(order, trade)) {
            order.status = canon::OrderStatus::Triggered;
            order.waitingForMarketFill = true;
            LocalOrderErrorCode fillError = LocalOrderErrorCode::None;
            if (tryFillMarketOrder_(order, marketData, trade.tsNs, fillError)) {
                publishFillLifecycleLocked_(order);
                if (order.status == canon::OrderStatus::Closed) closed.push_back(entry.first);
            }
        }
    }
    for (const std::string& orderId : closed) {
        const auto it = pendingOrders_.find(orderId);
        if (it != pendingOrders_.end() && !it->second.clientOrderId.empty()) clientOrderToOrderId_.erase(it->second.clientOrderId);
        pendingOrders_.erase(orderId);
    }
}

void LocalOrderEngine::processBookTickerLocked_(const cxet_bridge::CapturedBookTickerRow& bookTicker) noexcept {
    const auto marketIt = marketBySymbol_.find(bookTicker.symbol);
    const SymbolMarketData* marketData = marketIt == marketBySymbol_.end() ? nullptr : &marketIt->second;
    std::vector<std::string> closed{};
    for (auto& entry : pendingOrders_) {
        LocalOrder& order = entry.second;
        if (order.symbol != bookTicker.symbol) continue;
        if (order.acceptedTsNs > bookTicker.tsNs) continue;
        if (order.status == canon::OrderStatus::Pending) {
            order.status = canon::OrderStatus::Placed;
            publishExecutionEventLocked_(order,
                                         execution::ExecutionEventKind::StateChange,
                                         true,
                                         LocalOrderErrorCode::None);
        }

        bool filled = false;
        if (order.waitingForMarketFill) {
            LocalOrderErrorCode fillError = LocalOrderErrorCode::None;
            filled = tryFillMarketOrder_(order, marketData, bookTicker.tsNs, fillError);
        } else {
            filled = tryFillRestingLimitOnBookTicker_(order, marketData, bookTicker.tsNs);
        }
        if (filled) {
            publishFillLifecycleLocked_(order);
            if (order.status == canon::OrderStatus::Closed) closed.push_back(entry.first);
        }
    }
    for (const std::string& orderId : closed) {
        const auto it = pendingOrders_.find(orderId);
        if (it != pendingOrders_.end() && !it->second.clientOrderId.empty()) clientOrderToOrderId_.erase(it->second.clientOrderId);
        pendingOrders_.erase(orderId);
    }
}
std::uint64_t LocalOrderEngine::orderEntryDelayNs_() noexcept {
    return envU64OrDefault("HFTREC_LOCAL_ORDER_BASE_ONE_WAY_NS", 0u);
}

std::uint64_t LocalOrderEngine::orderEntryJitterNs_() noexcept {
    return envU64OrDefault("HFTREC_LOCAL_ORDER_JITTER_NS", 0u);
}

std::uint64_t LocalOrderEngine::sampleOrderDelayNsLocked_() noexcept {
    const std::uint64_t base = orderEntryDelayNs_();
    const std::uint64_t jitter = orderEntryJitterNs_();
    if (jitter == 0u) return base;
    std::uint64_t x = rngState_;
    x ^= x << 13u;
    x ^= x >> 7u;
    x ^= x << 17u;
    rngState_ = x == 0u ? 88172645463325252ull : x;
    return base + (rngState_ % (jitter + 1u));
}

void LocalOrderEngine::processCancelRequestsLocked_(std::uint64_t venueTsNs) noexcept {
    std::vector<CancelRequest> remaining{};
    remaining.reserve(cancelRequests_.size());
    for (const CancelRequest& cancel : cancelRequests_) {
        if (cancel.effectiveTsNs > venueTsNs) {
            remaining.push_back(cancel);
            continue;
        }
        cxet::network::local::hftrecorder::OrderRequestFrame request{};
        request.magic = cxet::network::local::hftrecorder::kFrameMagic;
        request.version = cxet::network::local::hftrecorder::kProtocolVersion;
        request.frameType = static_cast<std::uint16_t>(cxet::network::local::hftrecorder::FrameType::OrderRequest);
        request.operationRaw = static_cast<std::uint8_t>(cxet::UnifiedRequestSpec::Operation::SendWs);
        request.objectRaw = static_cast<std::uint8_t>(cxet::composite::out::WsSendObject::Order);
        request.typeRaw = static_cast<std::uint8_t>(canon::OrderType::Cancel);
        copyBounded(request.orderId, sizeof(request.orderId), cancel.orderId);
        copyBounded(request.origClientOrderId, sizeof(request.origClientOrderId), cancel.origClientOrderId);
        cxet::network::local::hftrecorder::OrderAckFrame ignored{};
        cancelOrderLocked_(request, cancel.effectiveTsNs, true, ignored);
    }
    cancelRequests_.swap(remaining);
}

bool LocalOrderEngine::cancelOrderLocked_(
    const cxet::network::local::hftrecorder::OrderRequestFrame& request,
    std::uint64_t effectiveTsNs,
    bool immediate,
    cxet::network::local::hftrecorder::OrderAckFrame& ack) noexcept {
    std::string orderId = makeSymbolKey_(request.orderId);
    const std::string origClientOrderId = makeSymbolKey_(request.origClientOrderId);
    if (orderId.empty() && !origClientOrderId.empty()) {
        const auto byClientIt = clientOrderToOrderId_.find(origClientOrderId);
        if (byClientIt != clientOrderToOrderId_.end()) orderId = byClientIt->second;
    }
    if (!immediate) {
        ++nextCancelSeq_;
        cancelRequests_.push_back(CancelRequest{.orderId = orderId,
                                                .origClientOrderId = origClientOrderId,
                                                .effectiveTsNs = effectiveTsNs,
                                                .seq = nextCancelSeq_});
        LocalOrder cancelAck{};
        cancelAck.exchangeRaw = request.exchangeRaw;
        cancelAck.marketRaw = request.marketRaw;
        cancelAck.symbol = makeSymbolKey_(request.symbol);
        cancelAck.orderId = orderId;
        cancelAck.clientOrderId = origClientOrderId.empty() ? makeSymbolKey_(request.clientOrderId) : origClientOrderId;
        cancelAck.status = canon::OrderStatus::Canceling;
        cancelAck.acceptedTsNs = effectiveTsNs;
        fillAckFromOrder_(cancelAck, true, LocalOrderErrorCode::None, ack);
        return true;
    }
    const auto orderIt = pendingOrders_.find(orderId);
    if (orderIt == pendingOrders_.end()) {
        LocalOrder rejected{};
        rejected.exchangeRaw = request.exchangeRaw;
        rejected.marketRaw = request.marketRaw;
        rejected.symbol = makeSymbolKey_(request.symbol);
        rejected.orderId = orderId;
        rejected.clientOrderId = origClientOrderId.empty() ? makeSymbolKey_(request.clientOrderId) : origClientOrderId;
        rejected.status = canon::OrderStatus::Rejected;
        rejected.acceptedTsNs = effectiveTsNs;
        fillAckFromOrder_(rejected, false, LocalOrderErrorCode::UnknownOrder, ack);
        publishExecutionEventLocked_(rejected,
                                     execution::ExecutionEventKind::Reject,
                                     false,
                                     LocalOrderErrorCode::UnknownOrder);
        return false;
    }
    LocalOrder canceled = orderIt->second;
    canceled.status = canon::OrderStatus::Canceled;
    canceled.acceptedTsNs = effectiveTsNs;
    fillAckFromOrder_(canceled, true, LocalOrderErrorCode::None, ack);
    publishExecutionEventLocked_(canceled,
                                 execution::ExecutionEventKind::StateChange,
                                 true,
                                 LocalOrderErrorCode::None);
    if (!canceled.clientOrderId.empty()) clientOrderToOrderId_.erase(canceled.clientOrderId);
    pendingOrders_.erase(orderIt);
    return true;
}
void LocalOrderEngine::fillAckFromOrder_(
    const LocalOrder& order,
    bool success,
    LocalOrderErrorCode errorCode,
    cxet::network::local::hftrecorder::OrderAckFrame& ack) const noexcept {
    ack = cxet::network::local::hftrecorder::OrderAckFrame{};
    ack.success = success ? 1u : 0u;
    ack.statusRaw = static_cast<std::uint8_t>(order.status);
    ack.exchangeRaw = order.exchangeRaw;
    ack.marketRaw = order.marketRaw;
    ack.errorCode = static_cast<std::uint32_t>(errorCode);
    ack.tsNs = order.status == canon::OrderStatus::Closed && order.fillTsNs != 0u ? order.fillTsNs : order.acceptedTsNs;
    copyBounded(ack.symbol, sizeof(ack.symbol), order.symbol);
    copyBounded(ack.orderId, sizeof(ack.orderId), order.orderId);
    copyBounded(ack.clientOrderId, sizeof(ack.clientOrderId), order.clientOrderId);
}

void LocalOrderEngine::publishExecutionEventLocked_(
    const LocalOrder& order,
    execution::ExecutionEventKind kind,
    bool success,
    LocalOrderErrorCode errorCode) const noexcept {
    if (eventSink_ == nullptr) return;
    eventSink_->onExecutionEvent(execution::ExecutionEvent{
        .kind = kind,
        .symbol = order.symbol,
        .orderId = order.orderId,
        .clientOrderId = order.clientOrderId,
        .execId = order.execId,
        .exchangeRaw = order.exchangeRaw,
        .marketRaw = order.marketRaw,
        .sideRaw = order.sideRaw,
        .typeRaw = order.typeRaw,
        .timeInForceRaw = order.timeInForceRaw,
        .reduceOnly = static_cast<std::uint8_t>(order.reduceOnly ? 1u : 0u),
        .statusRaw = static_cast<std::uint8_t>(order.status),
        .errorCode = static_cast<std::uint32_t>(errorCode),
        .quantityRaw = order.quantityRaw,
        .priceRaw = order.priceRaw,
        .fillPriceE8 = order.fillPriceE8,
        .filledQtyRaw = order.filledQtyRaw,
        .feeRaw = order.feeRaw,
        .realizedPnlRaw = order.realizedPnlRaw,
        .positionQtyRaw = order.positionQtyRaw,
        .avgEntryPriceE8 = order.avgEntryPriceE8,
        .walletBalanceRaw = account_.walletBalanceRaw,
        .availableBalanceRaw = account_.availableBalanceRaw,
        .equityRaw = account_.equityRaw,
        .tsNs = order.status == canon::OrderStatus::Closed && order.fillTsNs != 0u ? order.fillTsNs : order.acceptedTsNs,
        .success = success,
    });
}

LocalOrderEngine& globalLocalOrderEngine() noexcept {
    static LocalOrderEngine engine{};
    return engine;
}

}  // namespace hftrec::local_exchange
