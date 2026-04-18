#include <gtest/gtest.h>

#include <string>

#include "core/capture/JsonSerializers.hpp"
#include "core/replay/EventRows.hpp"
#include "core/replay/JsonLineParser.hpp"
#include "primitives/composite/BookTickerData.hpp"
#include "primitives/composite/OrderBookSnapshot.hpp"
#include "primitives/composite/Trade.hpp"

namespace {

using hftrec::Status;
using hftrec::isOk;
using hftrec::replay::parseBookTickerLine;
using hftrec::replay::parseDepthLine;
using hftrec::replay::parseSnapshotDocument;
using hftrec::replay::parseTradeLine;
using hftrec::replay::BookTickerRow;
using hftrec::replay::DepthRow;
using hftrec::replay::SnapshotDocument;
using hftrec::replay::TradeRow;

TEST(JsonLineParser, TradeLineRoundTrip) {
    cxet::composite::TradePublic ev{};
    ev.symbol.copyFrom("BTCUSDT");
    ev.id.raw = 5'123'456ULL;
    ev.price.raw = 3'000'100'000'000LL;
    ev.amount.raw = 10'000'000LL;
    ev.ts.raw = 1'713'168'000'000'000'000ULL;
    ev.side = Side::Buy();

    const auto line = hftrec::capture::renderTradeJsonLine("s", "binance", "futures_usd", ev, 7);

    TradeRow row{};
    ASSERT_EQ(parseTradeLine(line, row), Status::Ok);
    EXPECT_EQ(row.tsNs, 1'713'168'000'000'000'000LL);
    EXPECT_EQ(row.id,        5'123'456LL);
    EXPECT_EQ(row.priceE8,   3'000'100'000'000LL);
    EXPECT_EQ(row.qtyE8,     10'000'000LL);
    EXPECT_EQ(row.eventIndex, 7LL);
    EXPECT_EQ(row.sideBuy, 1u);
}

TEST(JsonLineParser, TradeLineSellSide) {
    cxet::composite::TradePublic ev{};
    ev.symbol.copyFrom("ETHUSDT");
    ev.id.raw = 42ULL;
    ev.ts.raw = 1'000'000'000ULL;
    ev.side = Side::Sell();

    const auto line = hftrec::capture::renderTradeJsonLine("s", "e", "m", ev, 1);

    TradeRow row{};
    ASSERT_EQ(parseTradeLine(line, row), Status::Ok);
    EXPECT_EQ(row.sideBuy, 0u);
}

TEST(JsonLineParser, BookTickerLineRoundTrip) {
    cxet::composite::BookTickerData ev{};
    ev.symbol.copyFrom("ETHUSDT");
    ev.bidPrice.raw = 200'000'000'000LL;
    ev.bidAmount.raw = 50'000'000LL;
    ev.askPrice.raw = 200'010'000'000LL;
    ev.askAmount.raw = 60'000'000LL;
    ev.ts.raw = 1'713'168'000'500'000'000ULL;

    const auto line = hftrec::capture::renderBookTickerJsonLine("s", "e", "m", ev, 3);

    BookTickerRow row{};
    ASSERT_EQ(parseBookTickerLine(line, row), Status::Ok);
    EXPECT_EQ(row.tsNs, 1'713'168'000'500'000'000LL);
    EXPECT_EQ(row.bidPriceE8, 200'000'000'000LL);
    EXPECT_EQ(row.bidQtyE8,   50'000'000LL);
    EXPECT_EQ(row.askPriceE8, 200'010'000'000LL);
    EXPECT_EQ(row.askQtyE8,   60'000'000LL);
    EXPECT_EQ(row.eventIndex, 3LL);
}

TEST(JsonLineParser, DepthLineRoundTrip) {
    cxet::composite::OrderBookSnapshot delta{};
    delta.symbol.copyFrom("BTCUSDT");
    delta.ts.raw = 1'713'168'000'750'000'000ULL;
    delta.firstUpdateId.raw = 100ULL;
    delta.updateId.raw = 102ULL;
    delta.bidCount.raw = 2u;
    delta.bids[0].price.raw  = 3'000'000'000'000LL;
    delta.bids[0].amount.raw = 25'000'000LL;
    delta.bids[1].price.raw  = 2'999'900'000'000LL;
    delta.bids[1].amount.raw = 0LL;
    delta.askCount.raw = 1u;
    delta.asks[0].price.raw  = 3'000'100'000'000LL;
    delta.asks[0].amount.raw = 15'000'000LL;

    const auto line = hftrec::capture::renderDepthJsonLine("s", "e", "m", delta, 42);

    DepthRow row{};
    ASSERT_EQ(parseDepthLine(line, row), Status::Ok);
    EXPECT_EQ(row.tsNs, 1'713'168'000'750'000'000LL);
    EXPECT_EQ(row.firstUpdateId, 100LL);
    EXPECT_EQ(row.finalUpdateId, 102LL);
    EXPECT_EQ(row.eventIndex, 42LL);
    ASSERT_EQ(row.bids.size(), 2u);
    EXPECT_EQ(row.bids[0].priceE8, 3'000'000'000'000LL);
    EXPECT_EQ(row.bids[0].qtyE8,   25'000'000LL);
    EXPECT_EQ(row.bids[1].priceE8, 2'999'900'000'000LL);
    EXPECT_EQ(row.bids[1].qtyE8,   0LL);
    ASSERT_EQ(row.asks.size(), 1u);
    EXPECT_EQ(row.asks[0].priceE8, 3'000'100'000'000LL);
    EXPECT_EQ(row.asks[0].qtyE8,   15'000'000LL);
}

TEST(JsonLineParser, DepthLineEmptyAskArray) {
    cxet::composite::OrderBookSnapshot delta{};
    delta.symbol.copyFrom("BTCUSDT");
    delta.bidCount.raw = 1u;
    delta.bids[0].price.raw  = 100LL;
    delta.bids[0].amount.raw = 200LL;
    delta.askCount.raw = 0u;

    const auto line = hftrec::capture::renderDepthJsonLine("s", "e", "m", delta, 0);
    DepthRow row{};
    ASSERT_EQ(parseDepthLine(line, row), Status::Ok);
    EXPECT_EQ(row.bids.size(), 1u);
    EXPECT_EQ(row.asks.size(), 0u);
}

TEST(JsonLineParser, SnapshotDocumentRoundTrip) {
    cxet::composite::OrderBookSnapshot snap{};
    snap.symbol.copyFrom("BTCUSDT");
    snap.ts.raw = 1'713'168'000'000'000'000ULL;
    snap.bidCount.raw = 1u;
    snap.bids[0].price.raw = 3'000'000'000'000LL;
    snap.bids[0].amount.raw = 100'000'000LL;
    snap.askCount.raw = 1u;
    snap.asks[0].price.raw = 3'000'100'000'000LL;
    snap.asks[0].amount.raw = 80'000'000LL;

    const auto doc = hftrec::capture::renderSnapshotJson("s", "e", "m", snap, 7);

    SnapshotDocument parsed{};
    ASSERT_EQ(parseSnapshotDocument(doc, parsed), Status::Ok);
    EXPECT_EQ(parsed.tsNs, 1'713'168'000'000'000'000LL);
    EXPECT_EQ(parsed.snapshotIndex, 7LL);
    ASSERT_EQ(parsed.bids.size(), 1u);
    EXPECT_EQ(parsed.bids[0].priceE8, 3'000'000'000'000LL);
    EXPECT_EQ(parsed.bids[0].qtyE8,   100'000'000LL);
    ASSERT_EQ(parsed.asks.size(), 1u);
    EXPECT_EQ(parsed.asks[0].priceE8, 3'000'100'000'000LL);
    EXPECT_EQ(parsed.asks[0].qtyE8,   80'000'000LL);
}

TEST(JsonLineParser, MissingFieldReturnsCorrupt) {
    const std::string bad = "{\"foo\":1,\"bar\":2}";
    TradeRow row{};
    EXPECT_EQ(parseTradeLine(bad, row), Status::CorruptData);
}

}  // namespace
