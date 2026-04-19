#include <gtest/gtest.h>

#include <string>
#include <vector>

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
    ev.price.raw = 3'000'100'000'000LL;
    ev.amount.raw = 10'000'000LL;
    ev.ts.raw = 1'713'168'000'000'000'000ULL;
    ev.side = Side::Buy();

    const auto line = hftrec::capture::renderTradeJsonLine(ev);

    TradeRow row{};
    ASSERT_EQ(parseTradeLine(line, row), Status::Ok);
    EXPECT_EQ(row.tsNs, 1'713'168'000'000'000'000LL);
    EXPECT_EQ(row.priceE8,   3'000'100'000'000LL);
    EXPECT_EQ(row.qtyE8,     10'000'000LL);
    EXPECT_EQ(row.sideBuy, 1u);
}

TEST(JsonLineParser, TradeLineSellSide) {
    cxet::composite::TradePublic ev{};
    ev.symbol.copyFrom("ETHUSDT");
    ev.id.raw = 42ULL;
    ev.ts.raw = 1'000'000'000ULL;
    ev.side = Side::Sell();

    const auto line = hftrec::capture::renderTradeJsonLine(ev);

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

    const auto line = hftrec::capture::renderBookTickerJsonLine(ev, {"bidQty", "askQty"});

    BookTickerRow row{};
    ASSERT_EQ(parseBookTickerLine(line, row), Status::Ok);
    EXPECT_EQ(row.tsNs, 1'713'168'000'500'000'000LL);
    EXPECT_EQ(row.bidPriceE8, 200'000'000'000LL);
    EXPECT_EQ(row.bidQtyE8,   50'000'000LL);
    EXPECT_EQ(row.askPriceE8, 200'010'000'000LL);
    EXPECT_EQ(row.askQtyE8,   60'000'000LL);
}

TEST(JsonLineParser, BookTickerLineWithoutQtyParses) {
    const std::string line =
        "{\"tsNs\":123,\"bidPriceE8\":456,\"askPriceE8\":789}";

    BookTickerRow row{};
    ASSERT_EQ(parseBookTickerLine(line, row), Status::Ok);
    EXPECT_EQ(row.tsNs, 123);
    EXPECT_EQ(row.bidPriceE8, 456);
    EXPECT_EQ(row.askPriceE8, 789);
    EXPECT_EQ(row.bidQtyE8, 0);
    EXPECT_EQ(row.askQtyE8, 0);
}

TEST(JsonLineParser, DepthLineRoundTrip) {
    cxet::composite::OrderBookSnapshot delta{};
    delta.symbol.copyFrom("BTCUSDT");
    delta.ts.raw = 1'713'168'000'750'000'000ULL;
    delta.updateId.raw = 220ULL;
    delta.firstUpdateId.raw = 218ULL;
    delta.bidCount.raw = 2u;
    delta.bids[0].price.raw  = 3'000'000'000'000LL;
    delta.bids[0].amount.raw = 25'000'000LL;
    delta.bids[1].price.raw  = 2'999'900'000'000LL;
    delta.bids[1].amount.raw = 0LL;
    delta.askCount.raw = 1u;
    delta.asks[0].price.raw  = 3'000'100'000'000LL;
    delta.asks[0].amount.raw = 15'000'000LL;

    const auto line = hftrec::capture::renderDepthJsonLine(delta);

    DepthRow row{};
    ASSERT_EQ(parseDepthLine(line, row), Status::Ok);
    EXPECT_EQ(row.tsNs, 1'713'168'000'750'000'000LL);
    EXPECT_EQ(row.updateId, 220);
    EXPECT_EQ(row.firstUpdateId, 218);
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

    const auto line = hftrec::capture::renderDepthJsonLine(delta);
    DepthRow row{};
    ASSERT_EQ(parseDepthLine(line, row), Status::Ok);
    EXPECT_EQ(row.bids.size(), 1u);
    EXPECT_EQ(row.asks.size(), 0u);
}

TEST(JsonLineParser, SnapshotDocumentRoundTrip) {
    cxet::composite::OrderBookSnapshot snap{};
    snap.symbol.copyFrom("BTCUSDT");
    snap.ts.raw = 1'713'168'000'000'000'000ULL;
    snap.updateId.raw = 150ULL;
    snap.firstUpdateId.raw = 145ULL;
    snap.bidCount.raw = 1u;
    snap.bids[0].price.raw = 3'000'000'000'000LL;
    snap.bids[0].amount.raw = 100'000'000LL;
    snap.askCount.raw = 1u;
    snap.asks[0].price.raw = 3'000'100'000'000LL;
    snap.asks[0].amount.raw = 80'000'000LL;

    const auto doc = hftrec::capture::renderSnapshotJson(snap);

    SnapshotDocument parsed{};
    ASSERT_EQ(parseSnapshotDocument(doc, parsed), Status::Ok);
    EXPECT_EQ(parsed.tsNs, 1'713'168'000'000'000'000LL);
    EXPECT_EQ(parsed.updateId, 150);
    EXPECT_EQ(parsed.firstUpdateId, 145);
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

TEST(JsonLineParser, DepthLineWithoutIdsStillParsesForBackwardCompatibility) {
    const std::string line =
        "{\"tsNs\":123,\"bids\":[{\"price_i64\":1,\"qty_i64\":2}],\"asks\":[]}";

    DepthRow row{};
    ASSERT_EQ(parseDepthLine(line, row), Status::Ok);
    EXPECT_EQ(row.updateId, 0);
    EXPECT_EQ(row.firstUpdateId, 0);
}

TEST(JsonLineParser, TradeLineHandlesEscapedStringsAndUnknownFields) {
    const std::string line =
        "{"
        "\"meta\":{\"ignored\":true,\"nested\":[1,2,{\"x\":\"y\"}]},"
        "\"tsNs\":123,"
        "\"priceE8\":789,"
        "\"qtyE8\":11,"
        "\"sideBuy\":1"
        "}";

    TradeRow row{};
    ASSERT_EQ(parseTradeLine(line, row), Status::Ok);
    EXPECT_EQ(row.tsNs, 123);
    EXPECT_EQ(row.priceE8, 789);
    EXPECT_EQ(row.qtyE8, 11);
    EXPECT_EQ(row.sideBuy, 1u);
}

TEST(JsonLineParser, CorruptJsonReturnsCorrupt) {
    const std::string line =
        "{\"tsNs\":123,\"priceE8\":2,\"qtyE8\":3,\"sideBuy\":\"buy\"}";
    TradeRow row{};
    EXPECT_EQ(parseTradeLine(line, row), Status::CorruptData);
}

}  // namespace
