#include <gtest/gtest.h>

#include <string>

#include "core/capture/JsonSerializers.hpp"
#include "core/cxet_bridge/CxetCaptureBridge.hpp"
#include "core/replay/EventRows.hpp"
#include "core/replay/JsonLineParser.hpp"

namespace {

using hftrec::Status;
using hftrec::capture::EventSequenceIds;
using hftrec::capture::SnapshotProvenance;
using hftrec::cxet_bridge::CapturedBookTickerRow;
using hftrec::cxet_bridge::CapturedLevel;
using hftrec::cxet_bridge::CapturedOrderBookRow;
using hftrec::cxet_bridge::CapturedTradeRow;
using hftrec::replay::BookTickerRow;
using hftrec::replay::DepthRow;
using hftrec::replay::SnapshotDocument;
using hftrec::replay::TradeRow;
using hftrec::replay::parseBookTickerLine;
using hftrec::replay::parseDepthLine;
using hftrec::replay::parseSnapshotDocument;
using hftrec::replay::parseTradeLine;

TEST(JsonLineParser, TradeLineRoundTrip) {
    CapturedTradeRow ev{};
    ev.symbol = "BTCUSDT";
    ev.priceE8 = 3'000'100'000'000LL;
    ev.qtyE8 = 10'000'000LL;
    ev.quoteQtyE8 = 30'001'000'000LL;
    ev.tsNs = 1'713'168'000'000'000'000ULL;
    ev.side = 1;
    const EventSequenceIds ids{7u, 11u};

    TradeRow row{};
    ASSERT_EQ(parseTradeLine(hftrec::capture::renderTradeJsonLine(ev, ids), row), Status::Ok);
    EXPECT_EQ(row.tsNs, 1'713'168'000'000'000'000LL);
    EXPECT_EQ(row.captureSeq, 7);
    EXPECT_EQ(row.ingestSeq, 11);
    EXPECT_EQ(row.priceE8, 3'000'100'000'000LL);
    EXPECT_EQ(row.qtyE8, 10'000'000LL);
    EXPECT_EQ(row.quoteQtyE8, 30'001'000'000LL);
    EXPECT_EQ(row.sideBuy, 1u);
}

TEST(JsonLineParser, TradeLineSellSide) {
    CapturedTradeRow ev{};
    ev.symbol = "ETHUSDT";
    ev.tradeId = 42ULL;
    ev.tsNs = 1'000'000'000ULL;
    ev.side = 0;
    const EventSequenceIds ids{8u, 12u};

    TradeRow row{};
    ASSERT_EQ(parseTradeLine(hftrec::capture::renderTradeJsonLine(ev, ids), row), Status::Ok);
    EXPECT_EQ(row.sideBuy, 0u);
    EXPECT_EQ(row.captureSeq, 8);
    EXPECT_EQ(row.ingestSeq, 12);
}

TEST(JsonLineParser, BookTickerLineRoundTrip) {
    CapturedBookTickerRow ev{};
    ev.symbol = "ETHUSDT";
    ev.bidPriceE8 = 200'000'000'000LL;
    ev.bidQtyE8 = 50'000'000LL;
    ev.askPriceE8 = 200'010'000'000LL;
    ev.askQtyE8 = 60'000'000LL;
    ev.includeBidQty = true;
    ev.includeAskQty = true;
    ev.tsNs = 1'713'168'000'500'000'000ULL;
    const EventSequenceIds ids{3u, 13u};

    BookTickerRow row{};
    ASSERT_EQ(parseBookTickerLine(hftrec::capture::renderBookTickerJsonLine(ev, ids), row), Status::Ok);
    EXPECT_EQ(row.tsNs, 1'713'168'000'500'000'000LL);
    EXPECT_EQ(row.captureSeq, 3);
    EXPECT_EQ(row.ingestSeq, 13);
    EXPECT_EQ(row.bidPriceE8, 200'000'000'000LL);
    EXPECT_EQ(row.bidQtyE8, 50'000'000LL);
    EXPECT_EQ(row.askPriceE8, 200'010'000'000LL);
    EXPECT_EQ(row.askQtyE8, 60'000'000LL);
}

TEST(JsonLineParser, DepthLineRoundTrip) {
    CapturedOrderBookRow delta{};
    delta.symbol = "BTCUSDT";
    delta.tsNs = 1'713'168'000'750'000'000ULL;
    delta.updateId = 220ULL;
    delta.firstUpdateId = 218ULL;
    delta.bids = {
        CapturedLevel{3'000'000'000'000LL, 25'000'000LL, 0, 0ULL},
        CapturedLevel{2'999'900'000'000LL, 0LL, 0, 0ULL},
    };
    delta.asks = {
        CapturedLevel{3'000'100'000'000LL, 15'000'000LL, 0, 0ULL},
    };
    const EventSequenceIds ids{9u, 14u};

    DepthRow row{};
    ASSERT_EQ(parseDepthLine(hftrec::capture::renderDepthJsonLine(delta, ids), row), Status::Ok);
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
    CapturedOrderBookRow delta{};
    delta.symbol = "BTCUSDT";
    delta.bids = {
        CapturedLevel{100LL, 200LL, 0, 0ULL},
    };
    const EventSequenceIds ids{10u, 15u};

    DepthRow row{};
    ASSERT_EQ(parseDepthLine(hftrec::capture::renderDepthJsonLine(delta, ids), row), Status::Ok);
    EXPECT_EQ(row.bids.size(), 1u);
    EXPECT_EQ(row.asks.size(), 0u);
}

TEST(JsonLineParser, SnapshotDocumentRoundTrip) {
    CapturedOrderBookRow snap{};
    snap.symbol = "BTCUSDT";
    snap.tsNs = 1'713'168'000'000'000'000ULL;
    snap.updateId = 150ULL;
    snap.firstUpdateId = 145ULL;
    snap.bids = {
        CapturedLevel{3'000'000'000'000LL, 100'000'000LL, 0, 0ULL},
    };
    snap.asks = {
        CapturedLevel{3'000'100'000'000LL, 80'000'000LL, 1, 0ULL},
    };
    SnapshotProvenance provenance{};
    provenance.sequence = EventSequenceIds{1u, 2u};
    provenance.sourceTsNs = 1'713'168'000'000'000'000LL;
    provenance.ingestTsNs = 1'713'168'000'000'123'456LL;
    provenance.anchorUpdateId = 150u;
    provenance.anchorFirstUpdateId = 145u;
    provenance.trustedReplayAnchor = true;

    SnapshotDocument parsed{};
    ASSERT_EQ(parseSnapshotDocument(hftrec::capture::renderSnapshotJson(snap, provenance), parsed), Status::Ok);
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
