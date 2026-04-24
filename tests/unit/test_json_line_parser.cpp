#include <gtest/gtest.h>

#include <string>

#include "core/capture/JsonSerializers.hpp"
#include "core/replay/EventRows.hpp"
#include "core/replay/JsonLineParser.hpp"

namespace {

using hftrec::Status;
using hftrec::replay::BookTickerRow;
using hftrec::replay::DepthRow;
using hftrec::replay::PricePair;
using hftrec::replay::SnapshotDocument;
using hftrec::replay::TradeRow;
using hftrec::replay::parseBookTickerLine;
using hftrec::replay::parseDepthLine;
using hftrec::replay::parseSnapshotDocument;
using hftrec::replay::parseTradeLine;

TEST(JsonLineParser, TradeLineRoundTrip) {
    TradeRow ev{};
    ev.symbol = "BTCUSDT";
    ev.exchange = "binance";
    ev.market = "futures_usd";
    ev.priceE8 = 3'000'100'000'000LL;
    ev.qtyE8 = 10'000'000LL;
    ev.quoteQtyE8 = 30'001'000'000LL;
    ev.tsNs = 1'713'168'000'000'000'000LL;
    ev.side = 1;
    ev.captureSeq = 7;
    ev.ingestSeq = 11;

    TradeRow row{};
    ASSERT_EQ(parseTradeLine(hftrec::capture::renderTradeJsonLine(ev), row), Status::Ok);
    EXPECT_EQ(row.tsNs, 1'713'168'000'000'000'000LL);
    EXPECT_EQ(row.captureSeq, 7);
    EXPECT_EQ(row.ingestSeq, 11);
    EXPECT_EQ(row.priceE8, 3'000'100'000'000LL);
    EXPECT_EQ(row.qtyE8, 10'000'000LL);
    EXPECT_EQ(row.quoteQtyE8, 30'001'000'000LL);
    EXPECT_EQ(row.sideBuy, 1u);
    EXPECT_EQ(row.symbol, "BTCUSDT");
    EXPECT_EQ(row.exchange, "binance");
    EXPECT_EQ(row.market, "futures_usd");
}

TEST(JsonLineParser, TradeLineSellSide) {
    TradeRow ev{};
    ev.symbol = "ETHUSDT";
    ev.exchange = "binance";
    ev.market = "spot";
    ev.tradeId = 42ULL;
    ev.tsNs = 1'000'000'000LL;
    ev.side = 0;
    ev.captureSeq = 8;
    ev.ingestSeq = 12;

    TradeRow row{};
    ASSERT_EQ(parseTradeLine(hftrec::capture::renderTradeJsonLine(ev), row), Status::Ok);
    EXPECT_EQ(row.sideBuy, 0u);
    EXPECT_EQ(row.captureSeq, 8);
    EXPECT_EQ(row.ingestSeq, 12);
}

TEST(JsonLineParser, BookTickerLineRoundTrip) {
    BookTickerRow ev{};
    ev.symbol = "ETHUSDT";
    ev.exchange = "binance";
    ev.market = "futures_usd";
    ev.bidPriceE8 = 200'000'000'000LL;
    ev.bidQtyE8 = 50'000'000LL;
    ev.askPriceE8 = 200'010'000'000LL;
    ev.askQtyE8 = 60'000'000LL;
    ev.tsNs = 1'713'168'000'500'000'000LL;
    ev.captureSeq = 3;
    ev.ingestSeq = 13;

    BookTickerRow row{};
    ASSERT_EQ(parseBookTickerLine(hftrec::capture::renderBookTickerJsonLine(ev), row), Status::Ok);
    EXPECT_EQ(row.tsNs, 1'713'168'000'500'000'000LL);
    EXPECT_EQ(row.captureSeq, 3);
    EXPECT_EQ(row.ingestSeq, 13);
    EXPECT_EQ(row.bidPriceE8, 200'000'000'000LL);
    EXPECT_EQ(row.bidQtyE8, 50'000'000LL);
    EXPECT_EQ(row.askPriceE8, 200'010'000'000LL);
    EXPECT_EQ(row.askQtyE8, 60'000'000LL);
    EXPECT_EQ(row.symbol, "ETHUSDT");
}

TEST(JsonLineParser, DepthLineRoundTrip) {
    DepthRow delta{};
    delta.symbol = "BTCUSDT";
    delta.exchange = "binance";
    delta.market = "futures_usd";
    delta.tsNs = 1'713'168'000'750'000'000LL;
    delta.updateId = 220;
    delta.firstUpdateId = 218;
    delta.hasUpdateId = true;
    delta.hasFirstUpdateId = true;
    delta.captureSeq = 9;
    delta.ingestSeq = 14;
    delta.bids = {
        PricePair{3'000'000'000'000LL, 25'000'000LL, 0, 0ULL},
        PricePair{2'999'900'000'000LL, 0LL, 0, 0ULL},
    };
    delta.asks = {
        PricePair{3'000'100'000'000LL, 15'000'000LL, 0, 0ULL},
    };

    DepthRow row{};
    ASSERT_EQ(parseDepthLine(hftrec::capture::renderDepthJsonLine(delta), row), Status::Ok);
    EXPECT_EQ(row.tsNs, 1'713'168'000'750'000'000LL);
    EXPECT_EQ(row.captureSeq, 9);
    EXPECT_EQ(row.ingestSeq, 14);
    EXPECT_EQ(row.updateId, 220);
    EXPECT_EQ(row.firstUpdateId, 218);
    ASSERT_EQ(row.bids.size(), 2u);
    EXPECT_EQ(row.bids[0].priceE8, 3'000'000'000'000LL);
    EXPECT_EQ(row.bids[0].qtyE8, 25'000'000LL);
    EXPECT_EQ(row.bids[1].priceE8, 2'999'900'000'000LL);
    EXPECT_EQ(row.bids[1].qtyE8, 0LL);
    ASSERT_EQ(row.asks.size(), 1u);
    EXPECT_EQ(row.asks[0].priceE8, 3'000'100'000'000LL);
    EXPECT_EQ(row.asks[0].qtyE8, 15'000'000LL);
}

TEST(JsonLineParser, DepthLineEmptyAskArray) {
    DepthRow delta{};
    delta.symbol = "BTCUSDT";
    delta.exchange = "binance";
    delta.market = "futures_usd";
    delta.captureSeq = 10;
    delta.ingestSeq = 15;
    delta.hasUpdateId = true;
    delta.hasFirstUpdateId = true;
    delta.bids = {
        PricePair{100LL, 200LL, 0, 0ULL},
    };

    DepthRow row{};
    ASSERT_EQ(parseDepthLine(hftrec::capture::renderDepthJsonLine(delta), row), Status::Ok);
    EXPECT_EQ(row.bids.size(), 1u);
    EXPECT_EQ(row.asks.size(), 0u);
}

TEST(JsonLineParser, SnapshotDocumentRoundTrip) {
    SnapshotDocument snap{};
    snap.symbol = "BTCUSDT";
    snap.exchange = "binance";
    snap.market = "futures_usd";
    snap.tsNs = 1'713'168'000'000'000'000LL;
    snap.updateId = 150;
    snap.firstUpdateId = 145;
    snap.hasUpdateId = true;
    snap.hasFirstUpdateId = true;
    snap.sourceTsNs = 1'713'168'000'000'000'000LL;
    snap.ingestTsNs = 1'713'168'000'000'123'456LL;
    snap.captureSeq = 1;
    snap.ingestSeq = 2;
    snap.anchorUpdateId = 150;
    snap.anchorFirstUpdateId = 145;
    snap.hasAnchorUpdateId = true;
    snap.hasAnchorFirstUpdateId = true;
    snap.trustedReplayAnchor = 1u;
    snap.snapshotKind = "initial";
    snap.source = "rest_orderbook_snapshot";
    snap.bids = {
        PricePair{3'000'000'000'000LL, 100'000'000LL, 0, 0ULL},
    };
    snap.asks = {
        PricePair{3'000'100'000'000LL, 80'000'000LL, 1, 0ULL},
    };

    SnapshotDocument parsed{};
    ASSERT_EQ(parseSnapshotDocument(hftrec::capture::renderSnapshotJson(snap), parsed), Status::Ok);
    EXPECT_EQ(parsed.tsNs, 1'713'168'000'000'000'000LL);
    EXPECT_EQ(parsed.captureSeq, 1);
    EXPECT_EQ(parsed.ingestSeq, 2);
    EXPECT_EQ(parsed.updateId, 150);
    EXPECT_EQ(parsed.firstUpdateId, 145);
    EXPECT_EQ(parsed.sourceTsNs, 1'713'168'000'000'000'000LL);
    EXPECT_EQ(parsed.ingestTsNs, 1'713'168'000'000'123'456LL);
    EXPECT_EQ(parsed.anchorUpdateId, 150);
    EXPECT_EQ(parsed.anchorFirstUpdateId, 145);
    EXPECT_EQ(parsed.trustedReplayAnchor, 1u);
    ASSERT_EQ(parsed.bids.size(), 1u);
    EXPECT_EQ(parsed.bids[0].priceE8, 3'000'000'000'000LL);
    EXPECT_EQ(parsed.bids[0].qtyE8, 100'000'000LL);
    ASSERT_EQ(parsed.asks.size(), 1u);
    EXPECT_EQ(parsed.asks[0].priceE8, 3'000'100'000'000LL);
    EXPECT_EQ(parsed.asks[0].qtyE8, 80'000'000LL);
}

TEST(JsonLineParser, RejectsObjectShape) {
    TradeRow row{};
    EXPECT_EQ(parseTradeLine({}, row), Status::CorruptData);
}

TEST(JsonLineParser, RejectsShortBookTickerArray) {
    BookTickerRow row{};
    EXPECT_EQ(parseBookTickerLine("[0,456,0,789]", row), Status::CorruptData);
}

TEST(JsonLineParser, RejectsDepthCountMismatch) {
    DepthRow row{};
    EXPECT_EQ(parseDepthLine("[0,11,11,123,2,0,3,5,[[1,2,0,0]],[]]", row), Status::CorruptData);
}

TEST(JsonLineParser, RejectsTradeSideString) {
    TradeRow row{};
    EXPECT_EQ(parseTradeLine("[0,0,2,3,123,0,0,0,0,\"buy\",1,2]", row), Status::CorruptData);
}

TEST(JsonLineParser, RejectsOverflowInteger) {
    TradeRow row{};
    EXPECT_EQ(parseTradeLine("[0,0,9223372036854775808,3,123,0,0,0,0,1,1,2]", row), Status::CorruptData);
}

TEST(JsonLineParser, RejectsLeadingZeroInteger) {
    TradeRow row{};
    EXPECT_EQ(parseTradeLine("[0,0,0123,3,123,0,0,0,0,1,1,2]", row), Status::CorruptData);
}

TEST(JsonLineParser, SnapshotRejectsInvalidTrustedReplayAnchor) {
    const std::string doc = "[0,1,1,123,1,1,1,1,123,124,1,1,2,[[100,2,0,0]],[[101,3,1,0]]]";

    SnapshotDocument parsed{};
    EXPECT_EQ(parseSnapshotDocument(doc, parsed), Status::CorruptData);
}

}  // namespace
