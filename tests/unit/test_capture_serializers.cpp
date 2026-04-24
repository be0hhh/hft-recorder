#include <gtest/gtest.h>

#include <string>

#include "core/capture/JsonSerializers.hpp"
#include "core/replay/EventRows.hpp"

namespace {

using hftrec::capture::renderBookTickerJsonLine;
using hftrec::capture::renderDepthJsonLine;
using hftrec::capture::renderSnapshotJson;
using hftrec::capture::renderTradeJsonLine;
using hftrec::replay::BookTickerRow;
using hftrec::replay::DepthRow;
using hftrec::replay::PricePair;
using hftrec::replay::SnapshotDocument;
using hftrec::replay::TradeRow;

TEST(CaptureSerializers, TradeLineContainsKeyFields) {
    TradeRow ev{};
    ev.symbol = "BTCUSDT";
    ev.exchange = "binance";
    ev.market = "futures_usd";
    ev.priceE8 = 3'000'100'000'000LL;
    ev.qtyE8 = 10'000'000LL;
    ev.quoteQtyE8 = 30'001'000'000LL;
    ev.side = 1;
    ev.tsNs = 1'713'168'000'000'000'000LL;
    ev.captureSeq = 7;
    ev.ingestSeq = 11;

    EXPECT_EQ(renderTradeJsonLine(ev), "[3000100000000,10000000,1,1713168000000000000,0,0,0,0,30001000000,\"BTCUSDT\",\"binance\",\"futures_usd\",7,11]");
}

TEST(CaptureSerializers, BookTickerLineContainsKeyFields) {
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
    ev.ingestSeq = 12;

    EXPECT_EQ(renderBookTickerJsonLine(ev), "[200000000000,50000000,200010000000,60000000,1713168000500000000,\"ETHUSDT\",\"binance\",\"futures_usd\",3,12]");
}

TEST(CaptureSerializers, DepthDeltaLineContainsLevelArrays) {
    DepthRow delta{};
    delta.symbol = "BTCUSDT";
    delta.exchange = "binance";
    delta.market = "futures_usd";
    delta.tsNs = 1'713'168'000'750'000'000LL;
    delta.updateId = 120;
    delta.firstUpdateId = 118;
    delta.hasUpdateId = true;
    delta.hasFirstUpdateId = true;
    delta.captureSeq = 5;
    delta.ingestSeq = 14;
    delta.bids = { PricePair{3'000'000'000'000LL, 25'000'000LL, 0, 0ULL} };
    delta.asks = { PricePair{3'000'100'000'000LL, 15'000'000LL, 0, 0ULL} };

    const auto line = renderDepthJsonLine(delta);
    EXPECT_NE(line.find("BTCUSDT"), std::string::npos);
}

TEST(CaptureSerializers, SnapshotJsonIncludesProvenance) {
    SnapshotDocument snap{};
    snap.symbol = "BTCUSDT";
    snap.exchange = "binance";
    snap.market = "futures_usd";
    snap.tsNs = 1'713'168'000'000'000'000LL;
    snap.updateId = 100;
    snap.firstUpdateId = 95;
    snap.hasUpdateId = true;
    snap.hasFirstUpdateId = true;
    snap.sourceTsNs = 1'713'168'000'000'000'000LL;
    snap.ingestTsNs = 1'713'168'000'000'123'456LL;
    snap.captureSeq = 1;
    snap.ingestSeq = 2;
    snap.anchorUpdateId = 100;
    snap.anchorFirstUpdateId = 95;
    snap.hasAnchorUpdateId = true;
    snap.hasAnchorFirstUpdateId = true;
    snap.trustedReplayAnchor = 1u;
    snap.snapshotKind = "initial";
    snap.source = "rest_orderbook_snapshot";
    snap.bids = { PricePair{3'000'000'000'000LL, 100'000'000LL, 0, 0ULL} };
    snap.asks = { PricePair{3'000'100'000'000LL, 80'000'000LL, 0, 0ULL} };

    const auto doc = renderSnapshotJson(snap);
    EXPECT_NE(doc.find("rest_orderbook_snapshot"), std::string::npos);
    EXPECT_NE(doc.find("initial"), std::string::npos);
}

}  // namespace
