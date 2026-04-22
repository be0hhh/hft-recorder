#include <gtest/gtest.h>

#include <cstdint>
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
                    std::uint64_t tsNs) {
    hftrec::cxet_bridge::CapturedBookTickerRow row{};
    row.symbol = symbol;
    row.bidPriceE8 = bidPrice;
    row.askPriceE8 = askPrice;
    row.bidQtyE8 = 100000000;
    row.askQtyE8 = 100000000;
    row.tsNs = tsNs;
    hftrec::local_exchange::globalLocalOrderEngine().onBookTicker(row);
}

void pushTrade(const char* symbol,
               std::int64_t price,
               bool sideBuy,
               std::uint64_t tsNs) {
    hftrec::cxet_bridge::CapturedTradeRow row{};
    row.symbol = symbol;
    row.priceE8 = price;
    row.qtyE8 = 100000000;
    row.sideBuy = sideBuy;
    row.tsNs = tsNs;
    hftrec::local_exchange::globalLocalOrderEngine().onTrade(row);
}

class LocalOrderEngineFixture : public ::testing::Test {
  protected:
    void SetUp() override {
        hftrec::local_exchange::globalLocalOrderEngine().reset();
    }

    void TearDown() override {
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
}

TEST_F(LocalOrderEngineFixture, LimitBuyFillsOnlyOnExactSellTrade) {
    cxet::network::local::hftrecorder::OrderAckFrame ack{};
    const auto request = makeRequest(static_cast<std::uint8_t>(canon::OrderType::Limit), 1u, 100000000, 100000000, "btcusdt");

    EXPECT_TRUE(hftrec::local_exchange::globalLocalOrderEngine().submitOrder(request, ack));
    EXPECT_EQ(ack.statusRaw, static_cast<std::uint8_t>(canon::OrderStatus::Placed));

    pushTrade("btcusdt", 100000000, true, 20u);
    EXPECT_EQ(hftrec::local_exchange::globalLocalOrderEngine().acceptedCount(), 1u);

    pushTrade("btcusdt", 100000000, false, 30u);
    EXPECT_EQ(hftrec::local_exchange::globalLocalOrderEngine().acceptedCount(), 1u);
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
    ASSERT_EQ(events.events.size(), 2u);
    EXPECT_EQ(events.events[0].kind, hftrec::execution::ExecutionEventKind::Ack);
    EXPECT_EQ(events.events[1].kind, hftrec::execution::ExecutionEventKind::StateChange);
    EXPECT_EQ(events.events[1].statusRaw, static_cast<std::uint8_t>(canon::OrderStatus::Closed));

    hftrec::local_exchange::globalLocalOrderEngine().setEventSink(nullptr);
}
