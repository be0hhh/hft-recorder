#include <gtest/gtest.h>

#include <string>

#include "CapturedArrivalTestData.hpp"
#include "../../src/Runtime/src/Capture/JsonSerializers.hpp"
#include "../../src/Runtime/src/Replay/EventRows.hpp"
#include "../../src/Runtime/src/Replay/JsonLineParser.hpp"

namespace {

using hftrec::Status;
using hftrec::replay::BookTickerRow;
using hftrec::replay::DepthRow;
using hftrec::replay::FundingRow;
using hftrec::replay::IndexPriceRow;
using hftrec::replay::MarkPriceRow;
using hftrec::replay::PricePair;
using hftrec::replay::PriceLimitRow;
using hftrec::replay::SnapshotDocument;
using hftrec::replay::TradeRow;
using hftrec::replay::parseBookTickerLine;
using hftrec::replay::parseDepthTapeSidecarLine;
using hftrec::replay::parseFundingLine;
using hftrec::replay::parseIndexPriceLine;
using hftrec::replay::parseMarkPriceLine;
using hftrec::replay::parsePriceLimitLine;
using hftrec::replay::parseSnapshotDocument;
using hftrec::replay::parseTradeLine;
namespace captured = hftrec::test_support;

TEST(JsonLineParser, TradeLineRoundTrip) {
    TradeRow ev{};
    ev.symbol = "BTC_USDT";
    ev.exchange = "binance";
    ev.market = "futures_usd";
    ev.priceE8 = 3'000'100'000'000LL;
    ev.qtyE8 = 10'000'000LL;
    ev.quoteQtyE8 = 30'001'000'000LL;
    ev.tsNs = 1'713'168'000'000'000'000LL;
    ev.side = 1;
    ev.captureSeq = 7;
    ev.ingestSeq = 11;
    ev.arrival = captured::applicationArrival(11, ev.tsNs);

    TradeRow row{};
    ASSERT_EQ(parseTradeLine(hftrec::capture::renderTradeJsonLine(ev), row), Status::Ok);
    EXPECT_EQ(row.tsNs, 1'713'168'000'000'000'000LL);
    EXPECT_EQ(row.priceE8, 3'000'100'000'000LL);
    EXPECT_EQ(row.qtyE8, 10'000'000LL);
    EXPECT_EQ(row.sideBuy, 1u);
}

TEST(JsonLineParser, TradeLineSellSide) {
    TradeRow ev{};
    ev.symbol = "ETH_USDT";
    ev.exchange = "binance";
    ev.market = "spot";
    ev.tradeId = 42ULL;
    ev.tsNs = 1'000'000'000LL;
    ev.side = 0;
    ev.captureSeq = 8;
    ev.ingestSeq = 12;
    ev.arrival = captured::applicationArrival(12, ev.tsNs);

    TradeRow row{};
    ASSERT_EQ(parseTradeLine(hftrec::capture::renderTradeJsonLine(ev), row), Status::Ok);
    EXPECT_EQ(row.sideBuy, 0u);
}

TEST(JsonLineParser, BookTickerLineRoundTrip) {
    BookTickerRow ev{};
    ev.eventId = 17;
    ev.symbol = "ETH_USDT";
    ev.exchange = "binance";
    ev.market = "futures_usd";
    ev.bidPriceE8 = 200'000'000'000LL;
    ev.bidQtyE8 = 50'000'000LL;
    ev.askPriceE8 = 200'010'000'000LL;
    ev.askQtyE8 = 60'000'000LL;
    ev.tsNs = 1'713'168'000'500'000'000LL;
    ev.captureSeq = 3;
    ev.ingestSeq = 13;
    ev.arrival = captured::applicationArrival(13, ev.tsNs);

    BookTickerRow row{};
    ASSERT_EQ(parseBookTickerLine(hftrec::capture::renderBookTickerJsonLine(ev), row), Status::Ok);
    EXPECT_EQ(row.eventId, 17u);
    EXPECT_EQ(row.tsNs, 1'713'168'000'500'000'000LL);
    EXPECT_EQ(row.bidPriceE8, 200'000'000'000LL);
    EXPECT_EQ(row.bidQtyE8, 50'000'000LL);
    EXPECT_EQ(row.askPriceE8, 200'010'000'000LL);
    EXPECT_EQ(row.askQtyE8, 60'000'000LL);
}

TEST(JsonLineParser, ReferenceChannelLinesRoundTrip) {
    MarkPriceRow markPrice{};
    markPrice.tsNs = 1'713'168'000'500'000'000LL;
    markPrice.markPriceE8 = 3'000'100'000'000LL;
    markPrice.captureSeq = 1;
    markPrice.ingestSeq = 1;
    markPrice.arrival = captured::applicationArrival(1, markPrice.tsNs);

    IndexPriceRow indexPrice{};
    indexPrice.tsNs = 1'713'168'000'600'000'000LL;
    indexPrice.indexPriceE8 = 3'000'000'000'000LL;
    indexPrice.captureSeq = 2;
    indexPrice.ingestSeq = 2;
    indexPrice.arrival = captured::applicationArrival(2, indexPrice.tsNs);

    FundingRow funding{};
    funding.tsNs = 1'713'168'000'700'000'000LL;
    funding.fundingRateE8 = 12500LL;
    funding.fundingTsNs = 1'713'168'000'000'000'000LL;
    funding.nextFundingTsNs = 1'713'196'800'000'000'000LL;
    funding.captureSeq = 3;
    funding.ingestSeq = 3;
    funding.arrival = captured::applicationArrival(3, funding.tsNs);

    PriceLimitRow priceLimit{};
    priceLimit.tsNs = 1'713'168'000'800'000'000LL;
    priceLimit.buyLimitE8 = 3'100'000'000'000LL;
    priceLimit.sellLimitE8 = 2'900'000'000'000LL;
    priceLimit.enabled = 1u;
    priceLimit.captureSeq = 4;
    priceLimit.ingestSeq = 4;
    priceLimit.arrival = captured::applicationArrival(4, priceLimit.tsNs);

    MarkPriceRow parsedMarkPrice{};
    ASSERT_EQ(parseMarkPriceLine(hftrec::capture::renderMarkPriceJsonLine(markPrice), parsedMarkPrice), Status::Ok);
    EXPECT_EQ(parsedMarkPrice.tsNs, markPrice.tsNs);
    EXPECT_EQ(parsedMarkPrice.markPriceE8, markPrice.markPriceE8);

    IndexPriceRow parsedIndexPrice{};
    ASSERT_EQ(parseIndexPriceLine(hftrec::capture::renderIndexPriceJsonLine(indexPrice), parsedIndexPrice), Status::Ok);
    EXPECT_EQ(parsedIndexPrice.tsNs, indexPrice.tsNs);
    EXPECT_EQ(parsedIndexPrice.indexPriceE8, indexPrice.indexPriceE8);

    FundingRow parsedFunding{};
    ASSERT_EQ(parseFundingLine(hftrec::capture::renderFundingJsonLine(funding), parsedFunding), Status::Ok);
    EXPECT_EQ(parsedFunding.tsNs, funding.tsNs);
    EXPECT_EQ(parsedFunding.fundingRateE8, funding.fundingRateE8);
    EXPECT_EQ(parsedFunding.fundingTsNs, funding.fundingTsNs);
    EXPECT_EQ(parsedFunding.nextFundingTsNs, funding.nextFundingTsNs);

    PriceLimitRow parsedPriceLimit{};
    ASSERT_EQ(parsePriceLimitLine(hftrec::capture::renderPriceLimitJsonLine(priceLimit), parsedPriceLimit), Status::Ok);
    EXPECT_EQ(parsedPriceLimit.tsNs, priceLimit.tsNs);
    EXPECT_EQ(parsedPriceLimit.buyLimitE8, priceLimit.buyLimitE8);
    EXPECT_EQ(parsedPriceLimit.sellLimitE8, priceLimit.sellLimitE8);
    EXPECT_EQ(parsedPriceLimit.enabled, 1u);
}

TEST(JsonLineParser, DepthTapeSidecarRoundTrip) {
    DepthRow encoded{};
    encoded.eventId = 41;
    encoded.tsNs = 1'713'168'000'750'000'000LL;
    encoded.captureSeq = 1;
    encoded.ingestSeq = 1;
    encoded.arrival = captured::applicationArrival(1, encoded.tsNs);
    encoded.levels = {{3'000'000'000'000LL, 25'000'000LL, 0},
                      {3'000'100'000'000LL, 15'000'000LL, 1}};
    const std::string tape = hftrec::capture::renderDepthTapeJsonLine(encoded);
    const std::string sidecar = hftrec::capture::renderDepthRleSidecarJsonLine(encoded);

    DepthRow row{};
    ASSERT_EQ(parseDepthTapeSidecarLine(tape, sidecar, row), Status::Ok);
    EXPECT_EQ(row.eventId, 41u);
    EXPECT_EQ(row.tsNs, 1'713'168'000'750'000'000LL);
    ASSERT_EQ(row.levels.size(), 2u);
    EXPECT_EQ(row.levels[0].side, 0);
    EXPECT_EQ(row.levels[1].side, 1);
}

TEST(JsonLineParser, DepthTapeRleSidecarMixedRunsRoundTrip) {
    DepthRow encoded{};
    encoded.eventId = 42;
    encoded.tsNs = 1'713'168'000'750'000'000LL;
    encoded.captureSeq = 2;
    encoded.ingestSeq = 2;
    encoded.arrival = captured::applicationArrival(2, encoded.tsNs);
    encoded.levels = {{1, 10, 0}, {2, 20, 0}, {3, 30, 1},
                      {4, 40, 1}, {5, 50, 0}};
    const std::string tape = hftrec::capture::renderDepthTapeJsonLine(encoded);
    const std::string sidecar = hftrec::capture::renderDepthRleSidecarJsonLine(encoded);

    DepthRow row{};
    ASSERT_EQ(parseDepthTapeSidecarLine(tape, sidecar, row), Status::Ok);
    ASSERT_EQ(row.levels.size(), 5u);
    EXPECT_EQ(row.levels[0].side, 0);
    EXPECT_EQ(row.levels[1].side, 0);
    EXPECT_EQ(row.levels[2].side, 1);
    EXPECT_EQ(row.levels[3].side, 1);
    EXPECT_EQ(row.levels[4].side, 0);
}

TEST(JsonLineParser, RejectsDepthTapeSidecarCountMismatch) {
    DepthRow encoded{};
    encoded.eventId = 43;
    encoded.tsNs = 1'713'168'000'750'000'000LL;
    encoded.captureSeq = 3;
    encoded.ingestSeq = 3;
    encoded.arrival = captured::applicationArrival(3, encoded.tsNs);
    encoded.levels = {{3'000'000'000'001LL, 25'000'000LL, 0}};
    const std::string tape = hftrec::capture::renderDepthTapeJsonLine(encoded);
    const std::string sidecar = "[43,10936540037604775808,0,2]";

    DepthRow row{};
    EXPECT_EQ(parseDepthTapeSidecarLine(tape, sidecar, row), Status::CorruptData);
}

TEST(JsonLineParser, RejectsDepthTapeSidecarTimestampMismatch) {
    DepthRow encoded{};
    encoded.eventId = 44;
    encoded.tsNs = 1'713'168'000'750'000'000LL;
    encoded.captureSeq = 4;
    encoded.ingestSeq = 4;
    encoded.arrival = captured::applicationArrival(4, encoded.tsNs);
    encoded.levels = {{3'000'000'000'001LL, 25'000'000LL, 0}};
    const std::string tape = hftrec::capture::renderDepthTapeJsonLine(encoded);
    const std::string sidecar = "[44,10936540037604775809,0,1]";

    DepthRow row{};
    EXPECT_EQ(parseDepthTapeSidecarLine(tape, sidecar, row), Status::CorruptData);
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

TEST(JsonLineParser, RejectsTradeLineWithoutArrivalTail) {
    TradeRow row{};
    EXPECT_EQ(parseTradeLine("[1,2,1,100,0,0,0,0,0,\"BTCUSDT\",\"binance\",\"futures_usd\",1,1]", row),
              Status::CorruptData);
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
