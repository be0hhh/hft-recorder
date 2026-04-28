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
    EXPECT_EQ(row.priceE8, 3'000'100'000'000LL);
    EXPECT_EQ(row.qtyE8, 10'000'000LL);
    EXPECT_EQ(row.sideBuy, 1u);
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
    EXPECT_EQ(row.bidPriceE8, 200'000'000'000LL);
    EXPECT_EQ(row.bidQtyE8, 50'000'000LL);
    EXPECT_EQ(row.askPriceE8, 200'010'000'000LL);
    EXPECT_EQ(row.askQtyE8, 60'000'000LL);
}

TEST(JsonLineParser, DepthLineRoundTrip) {
    DepthRow delta{};
    delta.tsNs = 1'713'168'000'750'000'000LL;
    delta.levels = {
        PricePair{3'000'000'000'000LL, 25'000'000LL, 0},
        PricePair{2'999'900'000'000LL, 0LL, 0},
        PricePair{3'000'100'000'000LL, 15'000'000LL, 1},
    };

    DepthRow row{};
    ASSERT_EQ(parseDepthLine(hftrec::capture::renderDepthJsonLine(delta), row), Status::Ok);
    EXPECT_EQ(row.tsNs, 1'713'168'000'750'000'000LL);
    ASSERT_EQ(row.levels.size(), 3u);
    EXPECT_EQ(row.levels[0].priceE8, 3'000'000'000'000LL);
    EXPECT_EQ(row.levels[0].qtyE8, 25'000'000LL);
    EXPECT_EQ(row.levels[0].side, 0);
    EXPECT_EQ(row.levels[1].priceE8, 2'999'900'000'000LL);
    EXPECT_EQ(row.levels[1].qtyE8, 0LL);
    EXPECT_EQ(row.levels[1].side, 0);
    EXPECT_EQ(row.levels[2].priceE8, 3'000'100'000'000LL);
    EXPECT_EQ(row.levels[2].qtyE8, 15'000'000LL);
    EXPECT_EQ(row.levels[2].side, 1);
}

TEST(JsonLineParser, DepthLineEmptyAskArray) {
    DepthRow delta{};
    delta.levels = {
        PricePair{100LL, 200LL, 0},
    };

    DepthRow row{};
    ASSERT_EQ(parseDepthLine(hftrec::capture::renderDepthJsonLine(delta), row), Status::Ok);
    EXPECT_EQ(row.levels.size(), 1u);
}

TEST(JsonLineParser, SnapshotDocumentRoundTrip) {
    SnapshotDocument snap{};
    snap.tsNs = 1'713'168'000'000'000'000LL;
    snap.levels = {
        PricePair{3'000'000'000'000LL, 100'000'000LL, 0},
        PricePair{3'000'100'000'000LL, 80'000'000LL, 1},
    };

    SnapshotDocument parsed{};
    ASSERT_EQ(parseSnapshotDocument(hftrec::capture::renderSnapshotJson(snap), parsed), Status::Ok);
    EXPECT_EQ(parsed.tsNs, 1'713'168'000'000'000'000LL);
    ASSERT_EQ(parsed.levels.size(), 2u);
    EXPECT_EQ(parsed.levels[0].priceE8, 3'000'000'000'000LL);
    EXPECT_EQ(parsed.levels[0].qtyE8, 100'000'000LL);
    EXPECT_EQ(parsed.levels[0].side, 0);
    EXPECT_EQ(parsed.levels[1].priceE8, 3'000'100'000'000LL);
    EXPECT_EQ(parsed.levels[1].qtyE8, 80'000'000LL);
    EXPECT_EQ(parsed.levels[1].side, 1);
}

TEST(JsonLineParser, RejectsObjectShape) {
    TradeRow row{};
    EXPECT_EQ(parseTradeLine({}, row), Status::CorruptData);
}

TEST(JsonLineParser, RejectsShortBookTickerArray) {
    BookTickerRow row{};
    EXPECT_EQ(parseBookTickerLine("[0,456,0]", row), Status::CorruptData);
}

TEST(JsonLineParser, RejectsDepthCountMismatch) {
    DepthRow row{};
    EXPECT_EQ(parseDepthLine("[0,11,11,123,2,0,3,5,[[1,2,0]],[]]", row), Status::CorruptData);
}

TEST(JsonLineParser, RejectsLegacyDepthLevelIdField) {
    DepthRow row{};
    EXPECT_EQ(parseDepthLine("[[1,2,0,0],123]", row), Status::CorruptData);
}

TEST(JsonLineParser, RejectsMissingDepthSideField) {
    DepthRow row{};
    EXPECT_EQ(parseDepthLine("[[1,2],123]", row), Status::CorruptData);
}

TEST(JsonLineParser, RejectsTradeSideString) {
    TradeRow row{};
    EXPECT_EQ(parseTradeLine("[0,0,2,3]", row), Status::CorruptData);
}

TEST(JsonLineParser, RejectsOverflowInteger) {
    TradeRow row{};
    EXPECT_EQ(parseTradeLine("[0,0,9223372036854775808,3]", row), Status::CorruptData);
}

TEST(JsonLineParser, RejectsLeadingZeroInteger) {
    TradeRow row{};
    EXPECT_EQ(parseTradeLine("[0,0,0123,3]", row), Status::CorruptData);
}

TEST(JsonLineParser, RejectsLegacyExtendedTradeLine) {
    TradeRow row{};
    EXPECT_EQ(parseTradeLine("[1,2,1,100,0,0,0,0,0,\"BTCUSDT\",\"binance\",\"futures_usd\",1,1]", row), Status::CorruptData);
}

TEST(JsonLineParser, RejectsLegacyExtendedBookTickerLine) {
    BookTickerRow row{};
    EXPECT_EQ(parseBookTickerLine("[1,2,3,4,100,\"BTCUSDT\",\"binance\",\"futures_usd\",1,1]", row), Status::CorruptData);
}

TEST(JsonLineParser, SnapshotRejectsInvalidTrustedReplayAnchor) {
    const std::string doc = "[[100,2,2],123]";

    SnapshotDocument parsed{};
    EXPECT_EQ(parseSnapshotDocument(doc, parsed), Status::CorruptData);
}

}  // namespace
