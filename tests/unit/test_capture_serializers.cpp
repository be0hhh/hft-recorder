#include <gtest/gtest.h>

#include <string>

#include "core/capture/JsonSerializers.hpp"
#include "core/capture/SessionId.hpp"
#include "core/capture/SessionManifest.hpp"
#include "core/capture/ChannelKind.hpp"
#include "primitives/composite/BookTickerData.hpp"
#include "primitives/composite/OrderBookSnapshot.hpp"
#include "primitives/composite/Trade.hpp"

namespace {

using hftrec::capture::renderTradeJsonLine;
using hftrec::capture::renderBookTickerJsonLine;
using hftrec::capture::renderDepthJsonLine;
using hftrec::capture::renderSnapshotJson;
using hftrec::capture::renderManifestJson;
using hftrec::capture::makeSessionId;
using hftrec::capture::SessionManifest;
using hftrec::capture::ChannelKind;
using hftrec::capture::channelFileName;

// Helper: does `haystack` contain the exact substring `needle`?
bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

TEST(CaptureSerializers, TradeLineContainsKeyFields) {
    cxet::composite::TradePublic ev{};
    ev.symbol.copyFrom("BTCUSDT");
    ev.id.raw = 5'123'456ULL;
    ev.price.raw = 3'000'100'000'000LL;   // 30001.0 * 1e8
    ev.amount.raw = 10'000'000LL;         // 0.1 * 1e8
    ev.ts.raw = 1'713'168'000'000'000'000ULL;
    ev.side = Side::Buy();

    const auto line = renderTradeJsonLine("sess-1", "binance", "futures_usd", ev, 7);

    EXPECT_TRUE(contains(line, "\"channel\":\"trades\""));
    EXPECT_TRUE(contains(line, "\"symbol\":\"BTCUSDT\""));
    EXPECT_TRUE(contains(line, "\"trade_id\":5123456"));
    EXPECT_TRUE(contains(line, "\"price_i64\":3000100000000"));
    EXPECT_TRUE(contains(line, "\"qty_i64\":10000000"));
    EXPECT_TRUE(contains(line, "\"event_index\":7"));
    EXPECT_TRUE(contains(line, "\"side\":\"buy\""));
    EXPECT_TRUE(contains(line, "\"exchange\":\"binance\""));
    EXPECT_TRUE(contains(line, "\"market\":\"futures_usd\""));
    EXPECT_FALSE(contains(line, "\n"));  // JSONL line, no embedded newline
}

TEST(CaptureSerializers, BookTickerLineContainsKeyFields) {
    cxet::composite::BookTickerData ev{};
    ev.symbol.copyFrom("ETHUSDT");
    ev.bidPrice.raw = 200'000'000'000LL;
    ev.bidAmount.raw = 50'000'000LL;
    ev.askPrice.raw = 200'010'000'000LL;
    ev.askAmount.raw = 60'000'000LL;
    ev.ts.raw = 1'713'168'000'500'000'000ULL;

    const auto line = renderBookTickerJsonLine("sess-1", "binance", "futures_usd", ev, 3);

    EXPECT_TRUE(contains(line, "\"channel\":\"bookticker\""));
    EXPECT_TRUE(contains(line, "\"symbol\":\"ETHUSDT\""));
    EXPECT_TRUE(contains(line, "\"best_bid_price_i64\":200000000000"));
    EXPECT_TRUE(contains(line, "\"best_ask_price_i64\":200010000000"));
    EXPECT_TRUE(contains(line, "\"best_bid_qty_i64\":50000000"));
    EXPECT_TRUE(contains(line, "\"best_ask_qty_i64\":60000000"));
    EXPECT_TRUE(contains(line, "\"event_index\":3"));
}

TEST(CaptureSerializers, DepthDeltaLineContainsLevelArrays) {
    cxet::composite::OrderBookSnapshot delta{};
    delta.symbol.copyFrom("BTCUSDT");
    delta.ts.raw = 1'713'168'000'750'000'000ULL;
    delta.firstUpdateId.raw = 100ULL;
    delta.updateId.raw = 102ULL;

    delta.bidCount.raw = 2u;
    delta.bids[0].price.raw  = 3'000'000'000'000LL;
    delta.bids[0].amount.raw = 25'000'000LL;
    delta.bids[1].price.raw  = 2'999'900'000'000LL;
    delta.bids[1].amount.raw = 0LL;  // Binance convention: qty=0 = remove level

    delta.askCount.raw = 1u;
    delta.asks[0].price.raw  = 3'000'100'000'000LL;
    delta.asks[0].amount.raw = 15'000'000LL;

    const auto line = renderDepthJsonLine("sess-1", "binance", "futures_usd", delta, 42);

    EXPECT_TRUE(contains(line, "\"channel\":\"depth\""));
    EXPECT_TRUE(contains(line, "\"symbol\":\"BTCUSDT\""));
    EXPECT_TRUE(contains(line, "\"first_update_id\":100"));
    EXPECT_TRUE(contains(line, "\"final_update_id\":102"));
    EXPECT_TRUE(contains(line, "\"price_i64\":3000000000000"));
    EXPECT_TRUE(contains(line, "\"qty_i64\":25000000"));
    EXPECT_TRUE(contains(line, "\"price_i64\":2999900000000"));
    EXPECT_TRUE(contains(line, "\"price_i64\":3000100000000"));
    EXPECT_TRUE(contains(line, "\"event_index\":42"));
}

TEST(CaptureSerializers, DepthDeltaRespectsCountFields) {
    // Only `bidCount` levels should be serialized even if the fixed array has
    // uninitialized slots after that.
    cxet::composite::OrderBookSnapshot delta{};
    delta.symbol.copyFrom("BTCUSDT");
    delta.bidCount.raw = 1u;
    delta.bids[0].price.raw  = 100LL;
    delta.bids[0].amount.raw = 200LL;
    delta.bids[1].price.raw  = 999'999LL;  // must NOT appear
    delta.bids[1].amount.raw = 888'888LL;  // must NOT appear
    delta.askCount.raw = 0u;

    const auto line = renderDepthJsonLine("s", "e", "m", delta, 0);
    EXPECT_TRUE(contains(line, "\"price_i64\":100"));
    EXPECT_FALSE(contains(line, "999999"));
    EXPECT_FALSE(contains(line, "888888"));
    EXPECT_TRUE(contains(line, "\"asks\":[]"));
}

TEST(CaptureSerializers, SnapshotJsonIsPrettyPrinted) {
    cxet::composite::OrderBookSnapshot snap{};
    snap.symbol.copyFrom("BTCUSDT");
    snap.ts.raw = 1'713'168'000'000'000'000ULL;
    snap.bidCount.raw = 1u;
    snap.bids[0].price.raw = 3'000'000'000'000LL;
    snap.bids[0].amount.raw = 100'000'000LL;
    snap.askCount.raw = 1u;
    snap.asks[0].price.raw = 3'000'100'000'000LL;
    snap.asks[0].amount.raw = 80'000'000LL;

    const auto doc = renderSnapshotJson("sess-1", "binance", "futures_usd", snap, 0);

    // Pretty-printed → has newlines and indent; root object present.
    EXPECT_TRUE(contains(doc, "\"channel\": \"snapshot\""));
    EXPECT_TRUE(contains(doc, "\"symbol\": \"BTCUSDT\""));
    EXPECT_TRUE(contains(doc, "\"snapshot_index\": 0"));
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
    m.endedAtNs   = 2'000'000'000LL;
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

TEST(SessionHelpers, MakeSessionIdShape) {
    const auto id = makeSessionId("binance", "futures_usd", "BTCUSDT", 1'713'168'000LL);
    EXPECT_EQ(id, std::string{"1713168000_binance_futures_usd_BTCUSDT"});
}

TEST(SessionHelpers, ChannelFileNamesDisjoint) {
    EXPECT_EQ(channelFileName(ChannelKind::Trades),     "trades.jsonl");
    EXPECT_EQ(channelFileName(ChannelKind::BookTicker), "bookticker.jsonl");
    EXPECT_EQ(channelFileName(ChannelKind::DepthDelta), "depth.jsonl");
    // Snapshot uses an indexed name so additional snapshots during one session
    // can be written without colliding.
    EXPECT_EQ(channelFileName(ChannelKind::Snapshot),   "snapshot_000.json");
}

}  // namespace
