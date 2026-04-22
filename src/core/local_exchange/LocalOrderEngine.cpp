#include "core/local_exchange/LocalOrderEngine.hpp"

#include <cstdio>
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

void copyBounded(char* out, std::size_t outSize, const std::string& text) noexcept {
    if (outSize == 0u) return;
    out[0] = '\0';
    std::size_t len = text.size();
    if (len >= outSize) len = outSize - 1u;
    if (len > 0u) std::memcpy(out, text.data(), len);
    out[len] = '\0';
}

}  // namespace

void LocalOrderEngine::reset() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    marketBySymbol_.clear();
    pendingOrders_.clear();
    nextOrderSeq_ = 0u;
    acceptedCount_ = 0u;
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
        rejected.status = canon::OrderStatus::Rejected;
        rejected.acceptedTsNs = nowNs_();
        fillAckFromOrder_(rejected, false, validation, ack);
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
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
    order.quantityRaw = request.quantityRaw;
    order.priceRaw = request.priceRaw;
    order.acceptedTsNs = nowNs_();
    order.status = canon::OrderStatus::Placed;

    const auto marketIt = marketBySymbol_.find(order.symbol);
    const SymbolMarketData* marketData = marketIt == marketBySymbol_.end() ? nullptr : &marketIt->second;
    if (order.typeRaw == static_cast<std::uint8_t>(canon::OrderType::Market)) {
        if (!tryFillMarketOrder_(order, marketData, order.acceptedTsNs)) {
            order.status = canon::OrderStatus::Rejected;
            fillAckFromOrder_(order, false, LocalOrderErrorCode::MissingBookTicker, ack);
            return false;
        }
    }

    ++acceptedCount_;
    fillAckFromOrder_(order, true, LocalOrderErrorCode::None, ack);
    if (order.status != canon::OrderStatus::Closed) {
        pendingOrders_.emplace(order.orderId, std::move(order));
    }
    return true;
}

void LocalOrderEngine::onTrade(const cxet_bridge::CapturedTradeRow& trade) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& market = marketBySymbol_[trade.symbol];
    market.lastTradePriceE8 = trade.priceE8;
    market.lastTradeQtyE8 = trade.qtyE8;
    market.lastTradeSideBuy = trade.sideBuy;
    market.tradeTsNs = trade.tsNs;
    processTradeLocked_(trade);
}

void LocalOrderEngine::onBookTicker(const cxet_bridge::CapturedBookTickerRow& bookTicker) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
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

bool LocalOrderEngine::tryFillMarketOrder_(LocalOrder& order,
                                           const SymbolMarketData* marketData,
                                           std::uint64_t tsNs) noexcept {
    if (marketData == nullptr) return false;
    if (order.sideRaw == kSideBuy) {
        if (marketData->askPriceE8 <= 0) return false;
        order.fillPriceE8 = marketData->askPriceE8;
    } else {
        if (marketData->bidPriceE8 <= 0) return false;
        order.fillPriceE8 = marketData->bidPriceE8;
    }
    order.fillTsNs = tsNs;
    order.waitingForMarketFill = false;
    order.status = canon::OrderStatus::Closed;
    return true;
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
    if (trade.priceE8 != order.priceRaw) return false;
    return order.sideRaw == kSideBuy ? !trade.sideBuy : trade.sideBuy;
}

void LocalOrderEngine::processTradeLocked_(const cxet_bridge::CapturedTradeRow& trade) noexcept {
    const auto marketIt = marketBySymbol_.find(trade.symbol);
    const SymbolMarketData* marketData = marketIt == marketBySymbol_.end() ? nullptr : &marketIt->second;
    std::vector<std::string> closed{};
    for (auto& entry : pendingOrders_) {
        LocalOrder& order = entry.second;
        if (order.symbol != trade.symbol) continue;
        if (shouldFillLimitOnTrade_(order, trade)) {
            order.fillPriceE8 = order.priceRaw;
            order.fillTsNs = trade.tsNs;
            order.status = canon::OrderStatus::Closed;
            closed.push_back(entry.first);
            continue;
        }
        if (shouldTriggerOnTrade_(order, trade)) {
            order.status = canon::OrderStatus::Triggered;
            order.waitingForMarketFill = true;
            if (tryFillMarketOrder_(order, marketData, trade.tsNs)) {
                closed.push_back(entry.first);
            }
        }
    }
    for (const std::string& orderId : closed) {
        pendingOrders_.erase(orderId);
    }
}

void LocalOrderEngine::processBookTickerLocked_(const cxet_bridge::CapturedBookTickerRow& bookTicker) noexcept {
    const auto marketIt = marketBySymbol_.find(bookTicker.symbol);
    const SymbolMarketData* marketData = marketIt == marketBySymbol_.end() ? nullptr : &marketIt->second;
    std::vector<std::string> closed{};
    for (auto& entry : pendingOrders_) {
        LocalOrder& order = entry.second;
        if (order.symbol != bookTicker.symbol || !order.waitingForMarketFill) continue;
        if (tryFillMarketOrder_(order, marketData, bookTicker.tsNs)) {
            closed.push_back(entry.first);
        }
    }
    for (const std::string& orderId : closed) {
        pendingOrders_.erase(orderId);
    }
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
}

LocalOrderEngine& globalLocalOrderEngine() noexcept {
    static LocalOrderEngine engine{};
    return engine;
}

}  // namespace hftrec::local_exchange
