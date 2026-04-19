#include <gtest/gtest.h>

#include <string>

#include "core/capture/ChannelKind.hpp"
#include "core/capture/JsonSerializers.hpp"
#include "core/capture/SessionId.hpp"
#include "core/capture/SessionManifest.hpp"
#include "primitives/composite/BookTickerData.hpp"
#include "primitives/composite/OrderBookSnapshot.hpp"
#include "primitives/composite/Trade.hpp"

namespace {

using hftrec::capture::ChannelKind;
using hftrec::capture::SessionManifest;
using hftrec::capture::channelFileName;
using hftrec::capture::makeSessionId;
using hftrec::capture::renderBookTickerJsonLine;
using hftrec::capture::renderDepthJsonLine;
using hftrec::capture::renderManifestJson;
using hftrec::capture::renderSnapshotJson;
using hftrec::capture::renderTradeJsonLine;

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

TEST(CaptureSerializers, TradeLineContainsKeyFields) {
    cxet::composite::TradePublic ev{};
    ev.symbol.copyFrom("BTCUSDT");
    ev.price.raw = 3'000'100'000'000LL;
    ev.amount.raw = 10'000'000LL;
    ev.ts.raw = 1'713'168'000'000'000'000ULL;
    ev.side = Side::Buy();

    const auto line = renderTradeJsonLine(ev);

    EXPECT_TRUE(contains(line, "\"tsNs\":1713168000000000000"));
    EXPECT_TRUE(contains(line, "\"priceE8\":3000100000000"));
    EXPECT_TRUE(contains(line, "\"qtyE8\":10000000"));
    EXPECT_TRUE(contains(line, "\"sideBuy\":1"));
    EXPECT_FALSE(contains(line, "\n"));
}

TEST(CaptureSerializers, BookTickerLineContainsKeyFields) {
    cxet::composite::BookTickerData ev{};
    ev.symbol.copyFrom("ETHUSDT");
    ev.bidPrice.raw = 200'000'000'000LL;
    ev.bidAmount.raw = 50'000'000LL;
    ev.askPrice.raw = 200'010'000'000LL;
    ev.askAmount.raw = 60'000'000LL;
    ev.ts.raw = 1'713'168'000'500'000'000ULL;

    const auto line = renderBookTickerJsonLine(ev, {"bidQty", "askQty"});

    EXPECT_TRUE(contains(line, "\"tsNs\":1713168000500000000"));
    EXPECT_TRUE(contains(line, "\"bidPriceE8\":200000000000"));
    EXPECT_TRUE(contains(line, "\"askPriceE8\":200010000000"));
    EXPECT_TRUE(contains(line, "\"bidQtyE8\":50000000"));
    EXPECT_TRUE(contains(line, "\"askQtyE8\":60000000"));
}

TEST(CaptureSerializers, BookTickerLineOmitsOptionalQtyWhenNotRequested) {
    cxet::composite::BookTickerData ev{};
    ev.bidPrice.raw = 200'000'000'000LL;
    ev.bidAmount.raw = 50'000'000LL;
    ev.askPrice.raw = 200'010'000'000LL;
    ev.askAmount.raw = 60'000'000LL;
    ev.ts.raw = 1'713'168'000'500'000'000ULL;

    const auto line = renderBookTickerJsonLine(ev, {});

    EXPECT_TRUE(contains(line, "\"tsNs\":1713168000500000000"));
    EXPECT_TRUE(contains(line, "\"bidPriceE8\":200000000000"));
    EXPECT_TRUE(contains(line, "\"askPriceE8\":200010000000"));
    EXPECT_FALSE(contains(line, "\"bidQtyE8\""));
    EXPECT_FALSE(contains(line, "\"askQtyE8\""));
}

TEST(CaptureSerializers, DepthDeltaLineContainsLevelArrays) {
    cxet::composite::OrderBookSnapshot delta{};
    delta.symbol.copyFrom("BTCUSDT");
    delta.ts.raw = 1'713'168'000'750'000'000ULL;
    delta.updateId.raw = 120ULL;
    delta.firstUpdateId.raw = 118ULL;
    delta.bidCount.raw = 2u;
    delta.bids[0].price.raw = 3'000'000'000'000LL;
    delta.bids[0].amount.raw = 25'000'000LL;
    delta.bids[1].price.raw = 2'999'900'000'000LL;
    delta.bids[1].amount.raw = 0LL;
    delta.askCount.raw = 1u;
    delta.asks[0].price.raw = 3'000'100'000'000LL;
    delta.asks[0].amount.raw = 15'000'000LL;

    const auto line = renderDepthJsonLine(delta);

    EXPECT_TRUE(contains(line, "\"tsNs\":1713168000750000000"));
    EXPECT_TRUE(contains(line, "\"updateId\":120"));
    EXPECT_TRUE(contains(line, "\"firstUpdateId\":118"));
    EXPECT_TRUE(contains(line, "\"price_i64\":3000000000000"));
    EXPECT_TRUE(contains(line, "\"qty_i64\":25000000"));
    EXPECT_TRUE(contains(line, "\"price_i64\":2999900000000"));
    EXPECT_TRUE(contains(line, "\"price_i64\":3000100000000"));
}

TEST(CaptureSerializers, DepthDeltaRespectsCountFields) {
    cxet::composite::OrderBookSnapshot delta{};
    delta.symbol.copyFrom("BTCUSDT");
    delta.bidCount.raw = 1u;
    delta.bids[0].price.raw = 100LL;
    delta.bids[0].amount.raw = 200LL;
    delta.bids[1].price.raw = 999'999LL;
    delta.bids[1].amount.raw = 888'888LL;
    delta.askCount.raw = 0u;

    const auto line = renderDepthJsonLine(delta);
    EXPECT_TRUE(contains(line, "\"price_i64\":100"));
    EXPECT_FALSE(contains(line, "999999"));
    EXPECT_FALSE(contains(line, "888888"));
    EXPECT_TRUE(contains(line, "\"asks\":[]"));
}

TEST(CaptureSerializers, SnapshotJsonIsPrettyPrinted) {
    cxet::composite::OrderBookSnapshot snap{};
    snap.symbol.copyFrom("BTCUSDT");
    snap.ts.raw = 1'713'168'000'000'000'000ULL;
    snap.updateId.raw = 100ULL;
    snap.firstUpdateId.raw = 95ULL;
    snap.bidCount.raw = 1u;
    snap.bids[0].price.raw = 3'000'000'000'000LL;
    snap.bids[0].amount.raw = 100'000'000LL;
    snap.askCount.raw = 1u;
    snap.asks[0].price.raw = 3'000'100'000'000LL;
    snap.asks[0].amount.raw = 80'000'000LL;

    const auto doc = renderSnapshotJson(snap);

    EXPECT_TRUE(contains(doc, "\"tsNs\": 1713168000000000000"));
    EXPECT_TRUE(contains(doc, "\"updateId\": 100"));
    EXPECT_TRUE(contains(doc, "\"firstUpdateId\": 95"));
    EXPECT_TRUE(contains(doc, "\"price_i64\":3000000000000"));
    EXPECT_TRUE(contains(doc, "\n"));
}

TEST(SessionHelpers, ManifestRoundTripShape) {
    SessionManifest m{};
    m.sessionId = "1000_binance_futures_usd_BTCUSDT";
    m.exchange = "binance";
    m.market = "futures_usd";
    m.symbols = {"BTCUSDT", "ETHUSDT"};
    m.selectedParentDir = "./recordings";
    m.startedAtNs = 1'000'000'000LL;
    m.endedAtNs = 2'000'000'000LL;
    m.targetDurationSec = 30;
    m.actualDurationSec = 1;
    m.snapshotIntervalSec = 60;
    m.tradesEnabled = true;
    m.bookTickerEnabled = false;
    m.orderbookEnabled = true;
    m.tradesCount = 123;
    m.depthCount = 7;

    const auto doc = renderManifestJson(m);
    EXPECT_TRUE(contains(doc, "\"session_id\": \"1000_binance_futures_usd_BTCUSDT\""));
    EXPECT_TRUE(contains(doc, "\"exchange\": \"binance\""));
    EXPECT_TRUE(contains(doc, "\"BTCUSDT\""));
    EXPECT_TRUE(contains(doc, "\"ETHUSDT\""));
    EXPECT_TRUE(contains(doc, "\"trades_enabled\": true"));
    EXPECT_TRUE(contains(doc, "\"bookticker_enabled\": false"));
    EXPECT_TRUE(contains(doc, "\"orderbook_enabled\": true"));
    EXPECT_TRUE(contains(doc, "\"trades\": 123"));
    EXPECT_TRUE(contains(doc, "\"depth\": 7"));
}

TEST(SessionHelpers, ManifestEscapesStrings) {
    SessionManifest m{};
    m.sessionId = "s\"1";
    m.exchange = "binance";
    m.market = "futures_usd";
    m.symbols = {"BTC\"USDT", "ETH\\USDT"};
    m.selectedParentDir = "C:\\recordings\\\"demo\"";
    m.warningSummary = "quote=\" backslash=\\ newline=\n";

    const auto doc = renderManifestJson(m);
    EXPECT_TRUE(contains(doc, "\"session_id\": \"s\\\"1\""));
    EXPECT_TRUE(contains(doc, "\"BTC\\\"USDT\""));
    EXPECT_TRUE(contains(doc, "\"ETH\\\\USDT\""));
    EXPECT_TRUE(contains(doc, "\"selected_parent_dir\": \"C:\\\\recordings\\\\\\\"demo\\\"\""));
    EXPECT_TRUE(contains(doc, "\"warning_summary\": \"quote=\\\" backslash=\\\\ newline=\\n\""));
}

TEST(SessionHelpers, MakeSessionIdShape) {
    const auto id = makeSessionId("binance", "futures_usd", "BTCUSDT", 1'713'168'000LL);
    EXPECT_EQ(id, std::string{"1713168000_binance_futures_usd_BTCUSDT"});
}

TEST(SessionHelpers, ChannelFileNamesDisjoint) {
    EXPECT_EQ(channelFileName(ChannelKind::Trades), "trades.jsonl");
    EXPECT_EQ(channelFileName(ChannelKind::BookTicker), "bookticker.jsonl");
    EXPECT_EQ(channelFileName(ChannelKind::DepthDelta), "depth.jsonl");
    EXPECT_EQ(channelFileName(ChannelKind::Snapshot), "snapshot_000.json");
}

}  // namespace
