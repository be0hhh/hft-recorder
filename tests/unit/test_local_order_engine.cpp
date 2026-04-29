#include <gtest/gtest.h>

#include <cstdint>
#include <cstdlib>
#include <cstring>

#include "canon/Enums.hpp"
#include "canon/PositionAndExchange.hpp"
#include "canon/Subtypes.hpp"
#include "composite/level_0/SendWsObject.hpp"
#include "core/cxet_bridge/CxetCaptureBridge.hpp"
#include "core/local_exchange/LocalOrderEngine.hpp"
#include "network/local/hftrecorder/Protocol.hpp"

#include <core/execution/ExecutionVenue.hpp>

namespace {

constexpr char kBtcUsdt[] = {'b', 't', 'c', 'u', 's', 'd', 't', '\0'};

void setEnv(const char* name, const char* value) {
    ::setenv(name, value, 1);
}

cxet::network::local::hftrecorder::OrderRequestFrame makeRequest(std::uint8_t typeRaw,
                                                                 std::uint8_t sideRaw,
                                                                 std::int64_t qty,
                                                                 std::int64_t price,
                                                                 const char* symbol) {
    cxet::network::local::hftrecorder::OrderRequestFrame request{};
    request.operationRaw = static_cast<std::uint8_t>(cxet::UnifiedRequestSpec::Operation::SendWs);
    request.objectRaw = static_cast<std::uint8_t>(cxet::composite::out::WsSendObject::Order);
    request.exchangeRaw = canon::kExchangeIdHftRecorderLocal.raw;
    request.marketRaw = canon::kMarketTypeFutures.raw;
    request.typeRaw = typeRaw;
    request.sideSet = 1u;
    request.sideRaw = sideRaw;
    request.quantityRaw = qty;
    request.priceRaw = price;
    std::memcpy(request.symbol, symbol, std::strlen(symbol));
    return request;
}

void pushBookTicker(const char* symbol,
                    std::int64_t bidPrice,
                    std::int64_t askPrice,
                    std::uint64_t tsNs,
                    std::int64_t bidQty = 100000000,
                    std::int64_t askQty = 100000000) {
    hftrec::cxet_bridge::CapturedBookTickerRow row{};
    row.symbol = symbol;
    row.bidPriceE8 = bidPrice;
    row.askPriceE8 = askPrice;
    row.bidQtyE8 = bidQty;
    row.askQtyE8 = askQty;
    row.tsNs = tsNs;
    hftrec::local_exchange::globalLocalOrderEngine().onBookTicker(row);
}

void pushTrade(const char* symbol,
               std::int64_t price,
               bool sideBuy,
               std::uint64_t tsNs,
               std::int64_t qty = 100000000) {
    hftrec::cxet_bridge::CapturedTradeRow row{};
    row.symbol = symbol;
    row.priceE8 = price;
    row.qtyE8 = qty;
    row.sideBuy = sideBuy;
    row.tsNs = tsNs;
    hftrec::local_exchange::globalLocalOrderEngine().onTrade(row);
}

class LocalOrderEngineFixture : public ::testing::Test {
  protected:
    void SetUp() override {
        setEnv("HFTREC_LOCAL_ORDER_BASE_ONE_WAY_NS", "0");
        setEnv("HFTREC_LOCAL_ORDER_JITTER_NS", "0");
        hftrec::local_exchange::globalLocalOrderEngine().reset();
    }

    void TearDown() override {
        setEnv("HFTREC_LOCAL_ORDER_BASE_ONE_WAY_NS", "0");
        setEnv("HFTREC_LOCAL_ORDER_JITTER_NS", "0");
        hftrec::local_exchange::globalLocalOrderEngine().reset();
    }
};

}  // namespace

TEST_F(LocalOrderEngineFixture, MarketBuyClosesAtAsk) {
    pushBookTicker("btcusdt", 99000000, 101000000, 10u);

    cxet::network::local::hftrecorder::OrderAckFrame ack{};
    const auto request = makeRequest(static_cast<std::uint8_t>(canon::OrderType::Market), 1u, 100000000, 0, "btcusdt");

    EXPECT_TRUE(hftrec::local_exchange::globalLocalOrderEngine().submitOrder(request, ack));
    EXPECT_EQ(ack.success, 1u);
    EXPECT_EQ(ack.statusRaw, static_cast<std::uint8_t>(canon::OrderStatus::Closed));
    EXPECT_EQ(ack.errorCode, 0u);
    EXPECT_EQ(hftrec::local_exchange::globalLocalOrderEngine().positionQtyRaw("btcusdt"), 100000000);
    EXPECT_LT(hftrec::local_exchange::globalLocalOrderEngine().walletBalanceRaw(), 1000000000000LL);
}

TEST_F(LocalOrderEngineFixture, ReduceOnlyBuyRejectsWithoutShortPosition) {
    pushBookTicker("btcusdt", 99000000, 101000000, 10u);

    cxet::network::local::hftrecorder::OrderAckFrame ack{};
    auto request = makeRequest(static_cast<std::uint8_t>(canon::OrderType::Market), 1u, 100000000, 0, "btcusdt");
    request.reduceOnlySet = 1u;
    request.reduceOnly = 1u;

    EXPECT_FALSE(hftrec::local_exchange::globalLocalOrderEngine().submitOrder(request, ack));
    EXPECT_EQ(ack.success, 0u);
    EXPECT_EQ(ack.statusRaw, static_cast<std::uint8_t>(canon::OrderStatus::Rejected));
    EXPECT_EQ(ack.errorCode, static_cast<std::uint32_t>(hftrec::local_exchange::LocalOrderErrorCode::ReduceOnlyWouldIncrease));
}

TEST_F(LocalOrderEngineFixture, SellClosesLongAndUpdatesPosition) {
    pushBookTicker("btcusdt", 99000000, 101000000, 10u);

    cxet::network::local::hftrecorder::OrderAckFrame ack{};
    const auto buy = makeRequest(static_cast<std::uint8_t>(canon::OrderType::Market), 1u, 100000000, 0, "btcusdt");
    ASSERT_TRUE(hftrec::local_exchange::globalLocalOrderEngine().submitOrder(buy, ack));

    pushBookTicker("btcusdt", 100000000, 102000000, 20u);
    auto sell = makeRequest(static_cast<std::uint8_t>(canon::OrderType::Market), 0u, 100000000, 0, "btcusdt");
    sell.reduceOnlySet = 1u;
    sell.reduceOnly = 1u;
    ASSERT_TRUE(hftrec::local_exchange::globalLocalOrderEngine().submitOrder(sell, ack));

    EXPECT_EQ(hftrec::local_exchange::globalLocalOrderEngine().positionQtyRaw("btcusdt"), 0);
    EXPECT_EQ(ack.statusRaw, static_cast<std::uint8_t>(canon::OrderStatus::Closed));
}

TEST_F(LocalOrderEngineFixture, MarketableLimitBuyFillsImmediatelyAtAsk) {
    pushBookTicker("btcusdt", 99000000, 100000000, 10u);

    cxet::network::local::hftrecorder::OrderAckFrame ack{};
    const auto request = makeRequest(static_cast<std::uint8_t>(canon::OrderType::Limit), 1u, 100000000, 101000000, "btcusdt");

    EXPECT_TRUE(hftrec::local_exchange::globalLocalOrderEngine().submitOrder(request, ack));
    EXPECT_EQ(ack.success, 1u);
    EXPECT_EQ(ack.statusRaw, static_cast<std::uint8_t>(canon::OrderStatus::Closed));
    EXPECT_EQ(hftrec::local_exchange::globalLocalOrderEngine().positionQtyRaw("btcusdt"), 100000000);
}

TEST_F(LocalOrderEngineFixture, RestingLimitBuyDoesNotFillOnQuoteEqualityAlone) {
    cxet::network::local::hftrecorder::OrderAckFrame ack{};
    const auto request = makeRequest(static_cast<std::uint8_t>(canon::OrderType::Limit), 1u, 100000000, 100000000, "btcusdt");

    EXPECT_TRUE(hftrec::local_exchange::globalLocalOrderEngine().submitOrder(request, ack));
    EXPECT_EQ(ack.statusRaw, static_cast<std::uint8_t>(canon::OrderStatus::Placed));

    pushBookTicker("btcusdt", 99000000, 100000000, 20u);
    EXPECT_EQ(hftrec::local_exchange::globalLocalOrderEngine().positionQtyRaw("btcusdt"), 0);
}

TEST_F(LocalOrderEngineFixture, RestingLimitBuyFillsOnSellTradeAtOrBelowLimit) {
    cxet::network::local::hftrecorder::OrderAckFrame ack{};
    const auto request = makeRequest(static_cast<std::uint8_t>(canon::OrderType::Limit), 1u, 100000000, 100000000, "btcusdt");

    EXPECT_TRUE(hftrec::local_exchange::globalLocalOrderEngine().submitOrder(request, ack));
    EXPECT_EQ(ack.statusRaw, static_cast<std::uint8_t>(canon::OrderStatus::Placed));

    pushTrade("btcusdt", 100000000, true, 20u);
    EXPECT_EQ(hftrec::local_exchange::globalLocalOrderEngine().positionQtyRaw("btcusdt"), 0);

    pushTrade("btcusdt", 99000000, false, 30u);
    EXPECT_EQ(hftrec::local_exchange::globalLocalOrderEngine().positionQtyRaw("btcusdt"), 100000000);
}

TEST_F(LocalOrderEngineFixture, RestingLimitSellFillsOnBuyTradeAtOrAboveLimit) {
    cxet::network::local::hftrecorder::OrderAckFrame ack{};
    const auto request = makeRequest(static_cast<std::uint8_t>(canon::OrderType::Limit), 0u, 100000000, 100000000, "btcusdt");

    EXPECT_TRUE(hftrec::local_exchange::globalLocalOrderEngine().submitOrder(request, ack));
    EXPECT_EQ(ack.statusRaw, static_cast<std::uint8_t>(canon::OrderStatus::Placed));

    pushTrade("btcusdt", 99000000, true, 20u);
    EXPECT_EQ(hftrec::local_exchange::globalLocalOrderEngine().positionQtyRaw("btcusdt"), 0);

    pushTrade("btcusdt", 101000000, true, 30u);
    EXPECT_EQ(hftrec::local_exchange::globalLocalOrderEngine().positionQtyRaw("btcusdt"), -100000000);
}

TEST_F(LocalOrderEngineFixture, RestingLimitBuyFillsOnStrictAskPassThrough) {
    cxet::network::local::hftrecorder::OrderAckFrame ack{};
    const auto request = makeRequest(static_cast<std::uint8_t>(canon::OrderType::Limit), 1u, 100000000, 100000000, "btcusdt");

    EXPECT_TRUE(hftrec::local_exchange::globalLocalOrderEngine().submitOrder(request, ack));
    EXPECT_EQ(ack.statusRaw, static_cast<std::uint8_t>(canon::OrderStatus::Placed));

    pushBookTicker("btcusdt", 98000000, 99000000, 20u);
    EXPECT_EQ(hftrec::local_exchange::globalLocalOrderEngine().positionQtyRaw("btcusdt"), 100000000);
}

TEST_F(LocalOrderEngineFixture, PartialLimitFillRemainsOpenAndThenCompletes) {
    pushBookTicker("btcusdt", 99000000, 100000000, 10u, 100000000, 25000000);

    cxet::network::local::hftrecorder::OrderAckFrame ack{};
    const auto request = makeRequest(static_cast<std::uint8_t>(canon::OrderType::Limit), 1u, 100000000, 101000000, "btcusdt");

    ASSERT_TRUE(hftrec::local_exchange::globalLocalOrderEngine().submitOrder(request, ack));
    EXPECT_EQ(ack.statusRaw, static_cast<std::uint8_t>(canon::OrderStatus::PartiallyFilled));
    EXPECT_EQ(hftrec::local_exchange::globalLocalOrderEngine().positionQtyRaw("btcusdt"), 25000000);

    pushTrade("btcusdt", 100000000, false, 20u, 75000000);
    EXPECT_EQ(hftrec::local_exchange::globalLocalOrderEngine().positionQtyRaw("btcusdt"), 100000000);
}

TEST_F(LocalOrderEngineFixture, CancelAfterPartialFillCancelsRemainingQuantity) {
    pushBookTicker("btcusdt", 99000000, 100000000, 10u, 100000000, 25000000);

    cxet::network::local::hftrecorder::OrderAckFrame ack{};
    auto request = makeRequest(static_cast<std::uint8_t>(canon::OrderType::Limit), 1u, 100000000, 101000000, "btcusdt");
    std::memcpy(request.clientOrderId, "cxpartial", 9u);

    ASSERT_TRUE(hftrec::local_exchange::globalLocalOrderEngine().submitOrder(request, ack));
    ASSERT_EQ(ack.statusRaw, static_cast<std::uint8_t>(canon::OrderStatus::PartiallyFilled));
    ASSERT_EQ(hftrec::local_exchange::globalLocalOrderEngine().positionQtyRaw("btcusdt"), 25000000);

    cxet::network::local::hftrecorder::OrderRequestFrame cancel{};
    cancel.operationRaw = static_cast<std::uint8_t>(cxet::UnifiedRequestSpec::Operation::SendWs);
    cancel.objectRaw = static_cast<std::uint8_t>(cxet::composite::out::WsSendObject::Order);
    cancel.exchangeRaw = canon::kExchangeIdHftRecorderLocal.raw;
    cancel.marketRaw = canon::kMarketTypeFutures.raw;
    cancel.typeRaw = static_cast<std::uint8_t>(canon::OrderType::Cancel);
    std::memcpy(cancel.symbol, "btcusdt", 8u);
    std::memcpy(cancel.origClientOrderId, "cxpartial", 9u);

    ASSERT_TRUE(hftrec::local_exchange::globalLocalOrderEngine().submitOrder(cancel, ack));
    EXPECT_EQ(ack.statusRaw, static_cast<std::uint8_t>(canon::OrderStatus::Canceled));

    pushTrade("btcusdt", 100000000, false, 30u, 75000000);
    EXPECT_EQ(hftrec::local_exchange::globalLocalOrderEngine().positionQtyRaw("btcusdt"), 25000000);
}
TEST_F(LocalOrderEngineFixture, DelayedMarketOrderWaitsForVenueTime) {
    setEnv("HFTREC_LOCAL_ORDER_BASE_ONE_WAY_NS", "50");
    setEnv("HFTREC_LOCAL_ORDER_JITTER_NS", "0");
    pushBookTicker("btcusdt", 99000000, 101000000, 100u);

    cxet::network::local::hftrecorder::OrderAckFrame ack{};
    const auto request = makeRequest(static_cast<std::uint8_t>(canon::OrderType::Market), 1u, 100000000, 0, "btcusdt");

    ASSERT_TRUE(hftrec::local_exchange::globalLocalOrderEngine().submitOrder(request, ack));
    EXPECT_EQ(ack.statusRaw, static_cast<std::uint8_t>(canon::OrderStatus::Pending));
    EXPECT_EQ(hftrec::local_exchange::globalLocalOrderEngine().positionQtyRaw("btcusdt"), 0);

    pushBookTicker("btcusdt", 100000000, 102000000, 120u);
    EXPECT_EQ(hftrec::local_exchange::globalLocalOrderEngine().positionQtyRaw("btcusdt"), 0);

    pushBookTicker("btcusdt", 100000000, 102000000, 151u);
    EXPECT_EQ(hftrec::local_exchange::globalLocalOrderEngine().positionQtyRaw("btcusdt"), 100000000);
}
TEST_F(LocalOrderEngineFixture, CancelByOrigClientOrderIdRemovesRestingOrder) {
    cxet::network::local::hftrecorder::OrderAckFrame ack{};
    auto request = makeRequest(static_cast<std::uint8_t>(canon::OrderType::Limit), 1u, 100000000, 100000000, "btcusdt");
    std::memcpy(request.clientOrderId, "cx0001", 7u);

    ASSERT_TRUE(hftrec::local_exchange::globalLocalOrderEngine().submitOrder(request, ack));
    ASSERT_EQ(ack.statusRaw, static_cast<std::uint8_t>(canon::OrderStatus::Placed));

    cxet::network::local::hftrecorder::OrderRequestFrame cancel{};
    cancel.operationRaw = static_cast<std::uint8_t>(cxet::UnifiedRequestSpec::Operation::SendWs);
    cancel.objectRaw = static_cast<std::uint8_t>(cxet::composite::out::WsSendObject::Order);
    cancel.exchangeRaw = canon::kExchangeIdHftRecorderLocal.raw;
    cancel.marketRaw = canon::kMarketTypeFutures.raw;
    cancel.typeRaw = static_cast<std::uint8_t>(canon::OrderType::Cancel);
    std::memcpy(cancel.symbol, "btcusdt", 8u);
    std::memcpy(cancel.origClientOrderId, "cx0001", 7u);

    ASSERT_TRUE(hftrec::local_exchange::globalLocalOrderEngine().submitOrder(cancel, ack));
    EXPECT_EQ(ack.success, 1u);
    EXPECT_EQ(ack.statusRaw, static_cast<std::uint8_t>(canon::OrderStatus::Canceled));
    EXPECT_STREQ(ack.clientOrderId, "cx0001");

    pushTrade("btcusdt", 100000000, false, 30u);
    EXPECT_EQ(hftrec::local_exchange::globalLocalOrderEngine().positionQtyRaw("btcusdt"), 0);
}
TEST_F(LocalOrderEngineFixture, StopBuyTriggersAndClosesOnL1) {
    pushBookTicker("btcusdt", 104000000, 105000000, 10u);

    cxet::network::local::hftrecorder::OrderAckFrame ack{};
    const auto request = makeRequest(static_cast<std::uint8_t>(canon::OrderType::Stop), 1u, 100000000, 110000000, "btcusdt");

    EXPECT_TRUE(hftrec::local_exchange::globalLocalOrderEngine().submitOrder(request, ack));
    EXPECT_EQ(ack.statusRaw, static_cast<std::uint8_t>(canon::OrderStatus::Placed));

    pushTrade("btcusdt", 110000000, true, 20u);
    EXPECT_EQ(hftrec::local_exchange::globalLocalOrderEngine().acceptedCount(), 1u);
}

TEST_F(LocalOrderEngineFixture, StopLossSellWaitsForL1AfterTrigger) {
    cxet::network::local::hftrecorder::OrderAckFrame ack{};
    const auto request = makeRequest(static_cast<std::uint8_t>(canon::OrderType::StopLoss), 0u, 100000000, 95000000, "btcusdt");

    EXPECT_TRUE(hftrec::local_exchange::globalLocalOrderEngine().submitOrder(request, ack));
    EXPECT_EQ(ack.statusRaw, static_cast<std::uint8_t>(canon::OrderStatus::Placed));

    pushTrade("btcusdt", 95000000, false, 20u);
    pushBookTicker("btcusdt", 94900000, 95100000, 30u);
    EXPECT_EQ(hftrec::local_exchange::globalLocalOrderEngine().acceptedCount(), 1u);
}

TEST_F(LocalOrderEngineFixture, RejectsMarketOrderWithoutBookTicker) {
    cxet::network::local::hftrecorder::OrderAckFrame ack{};
    const auto request = makeRequest(static_cast<std::uint8_t>(canon::OrderType::Market), 0u, 100000000, 0, "btcusdt");

    EXPECT_FALSE(hftrec::local_exchange::globalLocalOrderEngine().submitOrder(request, ack));
    EXPECT_EQ(ack.success, 0u);
    EXPECT_EQ(ack.statusRaw, static_cast<std::uint8_t>(canon::OrderStatus::Rejected));
    EXPECT_EQ(ack.errorCode, static_cast<std::uint32_t>(hftrec::local_exchange::LocalOrderErrorCode::MissingBookTicker));
}

TEST_F(LocalOrderEngineFixture, PublishesExecutionEventsThroughVenueSeam) {
    hftrec::execution::LiveExecutionStore store{};
    hftrec::local_exchange::globalLocalOrderEngine().setEventSink(&store);

    pushBookTicker(kBtcUsdt, 99000000, 101000000, 10u);

    cxet::network::local::hftrecorder::OrderAckFrame ack{};
    const auto request = makeRequest(static_cast<std::uint8_t>(canon::OrderType::Market), 1u, 100000000, 0, kBtcUsdt);
    ASSERT_TRUE(hftrec::local_exchange::globalLocalOrderEngine().submitOrder(request, ack));

    const auto events = store.readAll();
    ASSERT_EQ(events.events.size(), 5u);
    EXPECT_EQ(events.events[0].kind, hftrec::execution::ExecutionEventKind::Ack);
    EXPECT_EQ(events.events[1].kind, hftrec::execution::ExecutionEventKind::StateChange);
    EXPECT_EQ(events.events[1].statusRaw, static_cast<std::uint8_t>(canon::OrderStatus::Closed));
    EXPECT_EQ(events.events[2].kind, hftrec::execution::ExecutionEventKind::Fill);
    EXPECT_EQ(events.events[3].kind, hftrec::execution::ExecutionEventKind::PositionChange);
    EXPECT_EQ(events.events[4].kind, hftrec::execution::ExecutionEventKind::BalanceChange);

    hftrec::local_exchange::globalLocalOrderEngine().setEventSink(nullptr);
}
