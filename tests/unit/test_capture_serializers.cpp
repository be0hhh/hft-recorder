#include <gtest/gtest.h>

#include <string>

#include "core/capture/ChannelKind.hpp"
#include "core/capture/JsonSerializers.hpp"
#include "core/capture/SessionId.hpp"
#include "core/capture/SessionManifest.hpp"
#include "core/corpus/InstrumentMetadata.hpp"
#include "primitives/composite/BookTickerData.hpp"
#include "primitives/composite/OrderBookSnapshot.hpp"
#include "primitives/composite/Trade.hpp"

namespace {

using hftrec::capture::ChannelKind;
using hftrec::capture::SessionManifest;
using hftrec::capture::channelFileName;
using hftrec::capture::isLegacyManifest;
using hftrec::capture::makeSessionId;
using hftrec::capture::parseManifestJson;
using hftrec::capture::renderBookTickerJsonLine;
using hftrec::capture::renderDepthJsonLine;
using hftrec::capture::renderManifestJson;
using hftrec::capture::renderSnapshotJson;
using hftrec::capture::renderTradeJsonLine;
using hftrec::corpus::InstrumentMetadata;
using hftrec::corpus::makeInstrumentMetadata;
using hftrec::corpus::parseInstrumentMetadataJson;
using hftrec::corpus::renderInstrumentMetadataJson;
using hftrec::capture::EventSequenceIds;
using hftrec::capture::SnapshotProvenance;

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
    const EventSequenceIds ids{7u, 11u};

    const auto line = renderTradeJsonLine(ev, ids);

    EXPECT_TRUE(contains(line, "\"tsNs\":1713168000000000000"));
    EXPECT_TRUE(contains(line, "\"captureSeq\":7"));
    EXPECT_TRUE(contains(line, "\"ingestSeq\":11"));
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
    const EventSequenceIds ids{3u, 12u};

    const auto line = renderBookTickerJsonLine(ev, {"bidQty", "askQty"}, ids);

    EXPECT_TRUE(contains(line, "\"tsNs\":1713168000500000000"));
    EXPECT_TRUE(contains(line, "\"captureSeq\":3"));
    EXPECT_TRUE(contains(line, "\"ingestSeq\":12"));
    EXPECT_TRUE(contains(line, "\"bidPriceE8\":200000000000"));
    EXPECT_TRUE(contains(line, "\"askPriceE8\":200010000000"));
    EXPECT_TRUE(contains(line, "\"bidQtyE8\":50000000"));
    EXPECT_TRUE(contains(line, "\"askQtyE8\":60000000"));
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
    const EventSequenceIds ids{5u, 14u};

    const auto line = renderDepthJsonLine(delta, ids);

    EXPECT_TRUE(contains(line, "\"captureSeq\":5"));
    EXPECT_TRUE(contains(line, "\"ingestSeq\":14"));
    EXPECT_TRUE(contains(line, "\"updateId\":120"));
    EXPECT_TRUE(contains(line, "\"firstUpdateId\":118"));
    EXPECT_TRUE(contains(line, "\"price_i64\":3000000000000"));
}

TEST(CaptureSerializers, SnapshotJsonIncludesProvenance) {
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
    SnapshotProvenance provenance{};
    provenance.sequence = EventSequenceIds{1u, 2u};
    provenance.snapshotKind = "initial";
    provenance.source = "rest_orderbook_snapshot";
    provenance.exchange = "binance";
    provenance.market = "futures_usd";
    provenance.symbol = "BTCUSDT";
    provenance.sourceTsNs = 1'713'168'000'000'000'000LL;
    provenance.ingestTsNs = 1'713'168'000'000'123'456LL;
    provenance.anchorUpdateId = 100u;
    provenance.anchorFirstUpdateId = 95u;
    provenance.trustedReplayAnchor = true;

    const auto doc = renderSnapshotJson(snap, provenance);

    EXPECT_TRUE(contains(doc, "\"captureSeq\": 1"));
    EXPECT_TRUE(contains(doc, "\"ingestSeq\": 2"));
    EXPECT_TRUE(contains(doc, "\"snapshotKind\": \"initial\""));
    EXPECT_TRUE(contains(doc, "\"trustedReplayAnchor\": 1"));
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
    m.sessionStatus = "complete";
    m.tradesEnabled = true;
    m.bookTickerEnabled = false;
    m.orderbookEnabled = true;
    m.tradesCount = 123;
    m.depthCount = 7;
    m.snapshotFiles = {"snapshot_000.json"};
    m.snapshotCount = 1;
    m.canonicalArtifacts = {"manifest.json", "instrument_metadata.json", "trades.jsonl", "depth.jsonl", "snapshot_000.json"};
    m.supportArtifacts = {"reports/session_audit.json", "reports/integrity_report.json", "reports/loader_diagnostics.json"};
    m.sessionHealth = hftrec::SessionHealth::Degraded;
    m.exactReplayEligible = false;
    m.totalIntegrityIncidents = 2u;
    m.highestIntegritySeverity = hftrec::IntegritySeverity::Error;
    m.tradesIntegrity.state = hftrec::ChannelHealthState::Missing;
    m.tradesIntegrity.reasonCode = "missing_file";
    m.tradesIntegrity.reasonText = "enabled channel has no rows";

    const auto doc = renderManifestJson(m);
    EXPECT_TRUE(contains(doc, "\"manifest_schema_version\": 1"));
    EXPECT_TRUE(contains(doc, "\"capture_contract_version\": \"hftrec.cxet_capture.v1\""));
    EXPECT_TRUE(contains(doc, "\"session_health\": \"degraded\""));
    EXPECT_TRUE(contains(doc, "\"exact_replay_eligible\": false"));
    EXPECT_TRUE(contains(doc, "\"reason_code\": \"missing_file\""));
    EXPECT_TRUE(contains(doc, "\"report_path\": \"reports/integrity_report.json\""));
}

TEST(SessionHelpers, ManifestParseRoundTripPreservesCurrentShape) {
    SessionManifest m{};
    m.sessionId = "1000_binance_futures_usd_BTCUSDT";
    m.exchange = "binance";
    m.market = "futures_usd";
    m.symbols = {"BTCUSDT"};
    m.selectedParentDir = "./recordings";
    m.tradesEnabled = true;
    m.bookTickerEnabled = true;
    m.orderbookEnabled = true;
    m.tradesCount = 11;
    m.bookTickerCount = 22;
    m.depthCount = 33;
    m.snapshotFiles = {"snapshot_000.json"};
    m.snapshotCount = 1;
    m.canonicalArtifacts = {"manifest.json", "instrument_metadata.json", "trades.jsonl", "bookticker.jsonl", "depth.jsonl", "snapshot_000.json"};
    m.supportArtifacts = {"reports/session_audit.json", "reports/integrity_report.json", "reports/loader_diagnostics.json"};
    m.sessionHealth = hftrec::SessionHealth::Clean;
    m.exactReplayEligible = true;
    m.tradesIntegrity.state = hftrec::ChannelHealthState::Clean;
    m.bookTickerIntegrity.state = hftrec::ChannelHealthState::Clean;
    m.depthIntegrity.state = hftrec::ChannelHealthState::Clean;
    m.snapshotIntegrity.state = hftrec::ChannelHealthState::Clean;

    SessionManifest parsed{};
    ASSERT_EQ(parseManifestJson(renderManifestJson(m), parsed), hftrec::Status::Ok);
    EXPECT_EQ(parsed.manifestSchemaVersion, 1);
    EXPECT_EQ(parsed.corpusSchemaVersion, 1);
    EXPECT_EQ(parsed.sessionId, m.sessionId);
    EXPECT_EQ(parsed.exchange, m.exchange);
    EXPECT_EQ(parsed.market, m.market);
    EXPECT_EQ(parsed.symbols, m.symbols);
    EXPECT_EQ(parsed.tradesEnabled, true);
    EXPECT_EQ(parsed.bookTickerEnabled, true);
    EXPECT_EQ(parsed.orderbookEnabled, true);
    EXPECT_EQ(parsed.tradesCount, 11u);
    EXPECT_EQ(parsed.bookTickerCount, 22u);
    EXPECT_EQ(parsed.depthCount, 33u);
    EXPECT_EQ(parsed.snapshotFiles.size(), 1u);
    EXPECT_EQ(parsed.snapshotFiles.front(), "snapshot_000.json");
    EXPECT_EQ(parsed.sessionHealth, hftrec::SessionHealth::Clean);
    EXPECT_TRUE(parsed.exactReplayEligible);
    EXPECT_EQ(parsed.tradesIntegrity.state, hftrec::ChannelHealthState::Clean);
    EXPECT_FALSE(isLegacyManifest(parsed));
}

TEST(SessionHelpers, InstrumentMetadataRoundTripShape) {
    const auto metadata = makeInstrumentMetadata("binance", "futures_usd", "ETHUSDT");
    const auto doc = renderInstrumentMetadataJson(metadata);

    EXPECT_TRUE(contains(doc, "\"exchange\": \"binance\""));
    EXPECT_TRUE(contains(doc, "\"market\": \"futures_usd\""));
    EXPECT_TRUE(contains(doc, "\"symbol\": \"ETHUSDT\""));
    EXPECT_TRUE(contains(doc, "\"instrument_type\": \"perpetual_linear_future\""));
    EXPECT_TRUE(contains(doc, "\"base_asset\": \"ETH\""));
    EXPECT_TRUE(contains(doc, "\"quote_asset\": \"USDT\""));
    EXPECT_TRUE(contains(doc, "\"settlement_asset\": \"USDT\""));
    EXPECT_TRUE(contains(doc, "\"price_scale_digits\": 8"));
    EXPECT_TRUE(contains(doc, "\"qty_scale_digits\": 8"));

    InstrumentMetadata parsed{};
    ASSERT_EQ(parseInstrumentMetadataJson(doc, parsed), hftrec::Status::Ok);
    ASSERT_TRUE(parsed.baseAsset.has_value());
    ASSERT_TRUE(parsed.quoteAsset.has_value());
    EXPECT_EQ(parsed.exchange, "binance");
    EXPECT_EQ(parsed.market, "futures_usd");
    EXPECT_EQ(parsed.symbol, "ETHUSDT");
    EXPECT_EQ(*parsed.baseAsset, "ETH");
    EXPECT_EQ(*parsed.quoteAsset, "USDT");
}

TEST(SessionHelpers, ManifestParseSupportsLegacyV0Shape) {
    const std::string legacy = "{\n"
        "  \"session_id\": \"1713168000_binance_futures_usd_BTCUSDT\",\n"
        "  \"exchange\": \"binance\",\n"
        "  \"market\": \"futures_usd\",\n"
        "  \"symbols\": [\"BTCUSDT\"],\n"
        "  \"selected_parent_dir\": \"./recordings\",\n"
        "  \"started_at_ns\": 100,\n"
        "  \"ended_at_ns\": 200,\n"
        "  \"target_duration_sec\": 30,\n"
        "  \"actual_duration_sec\": 1,\n"
        "  \"snapshot_interval_sec\": 60,\n"
        "  \"channel_status\": {\n"
        "    \"trades_enabled\": true,\n"
        "    \"bookticker_enabled\": false,\n"
        "    \"orderbook_enabled\": true\n"
        "  },\n"
        "  \"event_counts\": {\n"
        "    \"trades\": 12,\n"
        "    \"bookticker\": 0,\n"
        "    \"depth\": 7,\n"
        "    \"snapshot\": 1\n"
        "  },\n"
        "  \"warning_summary\": \"\"\n"
        "}\n";

    SessionManifest parsed{};
    ASSERT_EQ(parseManifestJson(legacy, parsed), hftrec::Status::Ok);
    EXPECT_TRUE(isLegacyManifest(parsed));
    EXPECT_EQ(parsed.sessionStatus, "legacy_v0");
    EXPECT_EQ(parsed.tradesPath, "trades.jsonl");
    EXPECT_EQ(parsed.depthPath, "depth.jsonl");
    EXPECT_EQ(parsed.snapshotCount, 1u);
}

TEST(SessionHelpers, ManifestParseIgnoresUnknownTopLevelFieldForSupportedVersion) {
    SessionManifest m{};
    m.sessionId = "1713168000_binance_futures_usd_BTCUSDT";
    m.exchange = "binance";
    m.market = "futures_usd";
    m.symbols = {"BTCUSDT"};
    m.tradesEnabled = true;
    m.tradesCount = 1;
    m.canonicalArtifacts = {"manifest.json", "instrument_metadata.json", "trades.jsonl"};

    auto doc = renderManifestJson(m);
    const auto insertPos = doc.find("\"identity\"");
    ASSERT_NE(insertPos, std::string::npos);
    doc.insert(insertPos, "  \"future_optional\": {\"note\": \"ignored\"},\n");

    SessionManifest parsed{};
    ASSERT_EQ(parseManifestJson(doc, parsed), hftrec::Status::Ok);
    EXPECT_EQ(parsed.sessionId, m.sessionId);
    EXPECT_EQ(parsed.exchange, m.exchange);
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
