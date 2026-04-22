#include <gtest/gtest.h>

#include <string>

#include "core/capture/ChannelKind.hpp"
#include "core/capture/JsonSerializers.hpp"
#include "core/capture/SessionId.hpp"
#include "core/capture/SessionManifest.hpp"
#include "core/cxet_bridge/CxetCaptureBridge.hpp"
#include "core/corpus/InstrumentMetadata.hpp"

namespace {

using hftrec::capture::ChannelKind;
using hftrec::capture::EventSequenceIds;
using hftrec::capture::SessionManifest;
using hftrec::capture::SnapshotProvenance;
using hftrec::capture::channelFileName;
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
using hftrec::cxet_bridge::CapturedBookTickerRow;
using hftrec::cxet_bridge::CapturedLevel;
using hftrec::cxet_bridge::CapturedOrderBookRow;
using hftrec::cxet_bridge::CapturedTradeRow;

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

TEST(CaptureSerializers, TradeLineContainsKeyFields) {
    CapturedTradeRow ev{};
    ev.symbol = "BTCUSDT";
    ev.priceE8 = 3'000'100'000'000LL;
    ev.qtyE8 = 10'000'000LL;
    ev.side = 1;
    ev.tsNs = 1'713'168'000'000'000'000ULL;
    const EventSequenceIds ids{7u, 11u};

    const auto line = renderTradeJsonLine(ev, ids);

    EXPECT_EQ(line, "[3000100000000,10000000,1,1713168000000000000,0,0,0,0,0,7,11]");
    EXPECT_FALSE(contains(line, "\n"));
}

TEST(CaptureSerializers, BookTickerLineContainsKeyFields) {
    CapturedBookTickerRow ev{};
    ev.symbol = "ETHUSDT";
    ev.bidPriceE8 = 200'000'000'000LL;
    ev.bidQtyE8 = 50'000'000LL;
    ev.askPriceE8 = 200'010'000'000LL;
    ev.askQtyE8 = 60'000'000LL;
    ev.includeBidQty = true;
    ev.includeAskQty = true;
    ev.tsNs = 1'713'168'000'500'000'000ULL;
    const EventSequenceIds ids{3u, 12u};

    const auto line = renderBookTickerJsonLine(ev, ids);

    EXPECT_EQ(line, "[50000000,200000000000,60000000,200010000000,1713168000500000000,0,3,12]");
}

TEST(CaptureSerializers, DepthDeltaLineContainsLevelArrays) {
    CapturedOrderBookRow delta{};
    delta.symbol = "BTCUSDT";
    delta.tsNs = 1'713'168'000'750'000'000ULL;
    delta.updateId = 120ULL;
    delta.firstUpdateId = 118ULL;
    delta.bids = {
        CapturedLevel{3'000'000'000'000LL, 25'000'000LL, 0, 0ULL},
        CapturedLevel{2'999'900'000'000LL, 0LL, 0, 0ULL},
    };
    delta.asks = {
        CapturedLevel{3'000'100'000'000LL, 15'000'000LL, 0, 0ULL},
    };
    const EventSequenceIds ids{5u, 14u};

    const auto line = renderDepthJsonLine(delta, ids);

    EXPECT_EQ(line,
              "[120,118,1713168000750000000,2,1,5,14,"
              "[[25000000,3000000000000,0,0],[0,2999900000000,0,0]],"
              "[[15000000,3000100000000,0,0]],0]");
}

TEST(CaptureSerializers, SnapshotJsonIncludesProvenance) {
    CapturedOrderBookRow snap{};
    snap.symbol = "BTCUSDT";
    snap.tsNs = 1'713'168'000'000'000'000ULL;
    snap.updateId = 100ULL;
    snap.firstUpdateId = 95ULL;
    snap.bids = {
        CapturedLevel{3'000'000'000'000LL, 100'000'000LL, 0, 0ULL},
    };
    snap.asks = {
        CapturedLevel{3'000'100'000'000LL, 80'000'000LL, 0, 0ULL},
    };
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

    EXPECT_EQ(doc,
              "[100,95,1713168000000000000,1,1,1,2,"
              "1713168000000000000,1713168000000123456,100,95,1,"
              "[[100000000,3000000000000,0,0]],"
              "[[80000000,3000100000000,0,0]],0]\n");
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
    EXPECT_TRUE(contains(doc, "\"capture_contract_version\": \"hftrec.cxet_prefix_json.v2\""));
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

TEST(SessionHelpers, ManifestParseIgnoresUnknownNullTopLevelField) {
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
    doc.insert(insertPos, "  \"unknown_null\": null,\n");

    SessionManifest parsed{};
    ASSERT_EQ(parseManifestJson(doc, parsed), hftrec::Status::Ok);
    EXPECT_EQ(parsed.sessionId, m.sessionId);
}

TEST(SessionHelpers, ChannelFileNamesStable) {
    EXPECT_EQ(channelFileName(ChannelKind::Trades), "trades.jsonl");
    EXPECT_EQ(channelFileName(ChannelKind::BookTicker), "bookticker.jsonl");
    EXPECT_EQ(channelFileName(ChannelKind::DepthDelta), "depth.jsonl");
    EXPECT_EQ(channelFileName(ChannelKind::Snapshot), "snapshot_000.json");
}

TEST(SessionHelpers, SessionIdIncludesIdentityTuple) {
    const auto sessionId = makeSessionId("binance", "futures_usd", "BTCUSDT", 1713168000LL);
    EXPECT_EQ(sessionId, "1713168000_binance_futures_usd_BTCUSDT");
}

}  // namespace