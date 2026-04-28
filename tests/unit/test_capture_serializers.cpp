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

    EXPECT_EQ(renderTradeJsonLine(ev), "[3000100000000,10000000,1,1713168000000000000]");
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

    EXPECT_EQ(renderBookTickerJsonLine(ev), "[200000000000,50000000,200010000000,60000000,1713168000500000000]");
}

TEST(CaptureSerializers, DepthDeltaLineContainsLevelArrays) {
    DepthRow delta{};
    delta.tsNs = 1'713'168'000'750'000'000LL;
    delta.levels = {
        PricePair{3'000'000'000'000LL, 25'000'000LL, 0},
        PricePair{3'000'100'000'000LL, 15'000'000LL, 1},
    };

    EXPECT_EQ(renderDepthJsonLine(delta),
              "[[3000000000000,25000000,0],[3000100000000,15000000,1],1713168000750000000]");
}

TEST(CaptureSerializers, SnapshotJsonContainsOnlyLevelsAndTimestamp) {
    SnapshotDocument snap{};
    snap.tsNs = 1'713'168'000'000'000'000LL;
    snap.levels = {
        PricePair{3'000'000'000'000LL, 100'000'000LL, 0},
        PricePair{3'000'100'000'000LL, 80'000'000LL, 1},
    };

    EXPECT_EQ(renderSnapshotJson(snap),
              "[[3000000000000,100000000,0],[3000100000000,80000000,1],1713168000000000000]\n");
}

}  // namespace
