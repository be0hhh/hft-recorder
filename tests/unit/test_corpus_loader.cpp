#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>

#include "core/capture/SessionManifest.hpp"
#include "core/corpus/BinaryMarketCorpusReader.hpp"
#include "core/corpus/BinaryMarketCorpusWriter.hpp"
#include "core/corpus/CorpusLoader.hpp"
#include "core/corpus/InstrumentMetadata.hpp"
#include "core/replay/SessionReplay.hpp"

namespace fs = std::filesystem;

namespace {

fs::path fixtureDir(const char* name) {
    return fs::path(HFTRREC_SOURCE_DIR) / "tests" / "fixtures" / "session_corpus" / name;
}

template <std::size_t Capacity>
void setBinaryText(std::array<char, Capacity>& output,
                   std::uint8_t& bytes,
                   std::string_view value) {
    ASSERT_LE(value.size(), Capacity);
    std::copy(value.begin(), value.end(), output.begin());
    bytes = static_cast<std::uint8_t>(value.size());
}

hftrec::corpus::BinaryMarketSource binaryBookTickerSource() {
    namespace binary = hftrec::corpus;
    binary::BinaryMarketSource source{};
    source.sourceId = 1u;
    source.canonicalSymbolId = 1u;
    source.initialSourceGeneration = 7u;
    source.tickSizeRaw = 1;
    source.stepSizeRaw = 1;
    source.economicBaseAssetId = 1u;
    source.quoteAssetId = 2u;
    source.venueId = 1u;
    source.marketRaw = 1u;
    source.marketKindRaw = 2u;
    source.assetDomainRaw = 0u;
    source.priceScale = 8u;
    source.quantityScale = 8u;
    source.directoryFlags = binary::BinaryMarketDirectoryBookTicker |
        binary::BinaryMarketDirectoryTickSize;
    source.instrumentMetadataFlags = binary::BinaryMarketInstrumentStepSize;
    source.configuredChannelMask = binary::binaryMarketChannelBit(
        binary::BinaryMarketChannel::BookTicker);
    source.availableChannelMask = source.configuredChannelMask;
    source.traderReplayChannelMask = source.configuredChannelMask;
    source.compatibility[binary::binaryMarketChannelIndex(
        binary::BinaryMarketChannel::BookTicker)] =
        binary::BinaryMarketCompatibility::ExactTraderReplay;
    setBinaryText(source.venue, source.venueBytes, "binance");
    setBinaryText(source.market, source.marketBytes, "futures");
    setBinaryText(source.canonicalSymbol, source.canonicalSymbolBytes,
                  "BTC_USDT");
    setBinaryText(source.nativeSymbol, source.nativeSymbolBytes, "btcusdt");
    return source;
}

hftrec::corpus::BinaryMarketRecord binaryBookTickerRecord(
    bool traderReplayCompatible) {
    namespace binary = hftrec::corpus;
    binary::BinaryMarketRecord record{};
    record.header.sourceId = 1u;
    record.header.flags = traderReplayCompatible
        ? binary::BinaryMarketRecordTraderReplayCompatible
        : binary::BinaryMarketRecordNone;
    record.header.sourceGeneration = 7u;
    record.header.sessionEpoch = 1u;
    record.header.eventSequence = 1u;
    record.header.frameSequence = 1u;
    record.header.exchangeTimestampNs = 1'500;
    record.header.receiveRealtimeNs = 2'000;
    record.header.receiveMonotonicNs = 1'000u;
    record.header.shardSequence = 1u;
    record.header.payloadBytes =
        sizeof(binary::BinaryMarketBookTickerPayload);
    record.header.sourceFlags =
        binary::BinaryMarketSourceBookTickerBidPresent |
        binary::BinaryMarketSourceBookTickerAskPresent;
    record.header.channel = binary::BinaryMarketChannel::BookTicker;
    auto* payload =
        binary::binaryMarketPayload<binary::BinaryMarketBookTickerPayload>(
            &record);
    payload->bidPriceRaw = 100;
    payload->bidQtyRaw = 1;
    payload->askPriceRaw = 101;
    payload->askQtyRaw = 1;
    return record;
}

TEST(CorpusLoader, CleanFixtureLoadsAndUsesSeekIndex) {
    hftrec::corpus::CorpusLoader loader{};
    hftrec::corpus::SessionCorpus corpus{};
    hftrec::corpus::LoadReport report{};

    ASSERT_EQ(loader.loadDetailed(fixtureDir("clean_full"), corpus, report), hftrec::Status::Ok);
    EXPECT_TRUE(report.manifestPresent);
    EXPECT_TRUE(report.usedSeekIndex);
    EXPECT_FALSE(report.staleSeekIndex);
    EXPECT_EQ(report.tradesState, hftrec::corpus::ChannelLoadState::Clean);
    EXPECT_EQ(report.bookTickerState, hftrec::corpus::ChannelLoadState::Clean);
    EXPECT_EQ(report.depthState, hftrec::corpus::ChannelLoadState::Clean);
    EXPECT_EQ(report.snapshotState, hftrec::corpus::ChannelLoadState::NotCaptured);
    EXPECT_GE(corpus.tradeLines.size(), 1u);
    EXPECT_GE(corpus.bookTickerLines.size(), 1u);
    EXPECT_GE(corpus.depthRows.size(), 2u);
}

TEST(InstrumentMetadata, RoundTripsTraderBacktestGridFields) {
    auto metadata = hftrec::corpus::makeInstrumentMetadata("binance", "futures", "BTC_USDT");
    metadata.tickSizeE8 = 10000000;
    metadata.lotSizeE8 = 100000;
    metadata.contractBaseQtyE8 = 100000;
    metadata.priceBasisQtyE8 = 10000000000LL;
    metadata.expiryUtcNs = 1781913600000000000LL;
    metadata.canonicalBaseMultiplier = 100;
    metadata.nativeBaseMultiplier = 1;
    metadata.pricePowerOfTenAdjustment = 2;
    metadata.spotQuantityPowerOfTenAdjustment = 0;
    metadata.denominationGeneration = 42;
    metadata.denominationCatalogDigest = "00112233";
    metadata.denominationSource = "cxet_plan_v1";
    metadata.tickSizeSource = "hft_trader_exchange_info";
    metadata.lotSizeSource = "hft_trader_exchange_info";
    metadata.contractBaseQtySource = "hft_trader_exchange_info";
    metadata.priceBasisQtySource = "hft_trader_exchange_info";
    metadata.expiryUtcNsSource = "hft_trader_exchange_info";
    metadata.metadataSource = "hft_trader";

    const auto document = hftrec::corpus::renderInstrumentMetadataJson(metadata);
    EXPECT_NE(document.find("\"contract_base_qty_e8\": 100000"), std::string::npos);
    EXPECT_NE(document.find("\"price_basis_qty_e8\": 10000000000"), std::string::npos);
    EXPECT_NE(document.find("\"expiry_utc_ns\": 1781913600000000000"), std::string::npos);
    EXPECT_NE(document.find("\"metadata_source\": \"hft_trader\""), std::string::npos);
    EXPECT_NE(document.find("\"canonical_base_multiplier\": 100"), std::string::npos);

    hftrec::corpus::InstrumentMetadata parsed{};
    ASSERT_EQ(hftrec::corpus::parseInstrumentMetadataJson(document, parsed), hftrec::Status::Ok);
    ASSERT_TRUE(parsed.contractBaseQtyE8.has_value());
    EXPECT_EQ(*parsed.contractBaseQtyE8, 100000);
    EXPECT_EQ(parsed.contractBaseQtySource, "hft_trader_exchange_info");
    ASSERT_TRUE(parsed.priceBasisQtyE8.has_value());
    EXPECT_EQ(*parsed.priceBasisQtyE8, 10000000000LL);
    EXPECT_EQ(parsed.priceBasisQtySource, "hft_trader_exchange_info");
    ASSERT_TRUE(parsed.expiryUtcNs.has_value());
    EXPECT_EQ(*parsed.expiryUtcNs, 1781913600000000000LL);
    EXPECT_EQ(parsed.expiryUtcNsSource, "hft_trader_exchange_info");
    EXPECT_EQ(parsed.metadataSource, "hft_trader");
    ASSERT_TRUE(parsed.canonicalBaseMultiplier.has_value());
    EXPECT_EQ(*parsed.canonicalBaseMultiplier, 100);
    EXPECT_EQ(parsed.denominationSource, "cxet_plan_v1");
}

TEST(InstrumentMetadata, ParsesLegacyDocumentWithoutPriceBasisAsIdentityMissing) {
    constexpr std::string_view document = R"({
  "schema_version": "hftrec.instrument_metadata.v1",
  "exchange": "finam",
  "market": "spot",
  "symbol": "SBER@MISX",
  "metadata_source": "recorder_inference"
})";

    hftrec::corpus::InstrumentMetadata parsed{};
    ASSERT_EQ(hftrec::corpus::parseInstrumentMetadataJson(document, parsed), hftrec::Status::Ok);
    EXPECT_FALSE(parsed.priceBasisQtyE8.has_value());
    EXPECT_EQ(parsed.priceBasisQtySource, "unknown");
    EXPECT_FALSE(parsed.expiryUtcNs.has_value());
    EXPECT_EQ(parsed.expiryUtcNsSource, "unknown");
    EXPECT_FALSE(parsed.canonicalBaseMultiplier.has_value());
    EXPECT_EQ(parsed.denominationSource, "unknown");
}

TEST(CorpusLoader, CorruptJsonFixtureReportsArtifactAndLine) {
    hftrec::corpus::CorpusLoader loader{};
    hftrec::corpus::SessionCorpus corpus{};
    hftrec::corpus::LoadReport report{};

    ASSERT_EQ(loader.loadDetailed(fixtureDir("corrupt_bad_json_line"), corpus, report), hftrec::Status::CorruptData);
    ASSERT_FALSE(report.issues.empty());
    EXPECT_EQ(report.issues.front().code, hftrec::corpus::LoadIssueCode::InvalidJsonLine);
    EXPECT_EQ(report.issues.front().artifact, "trades.jsonl");
    EXPECT_EQ(report.issues.front().lineOrRow, 2u);
}

TEST(CorpusLoader, UnsupportedSchemaFixtureFailsDeterministically) {
    hftrec::corpus::CorpusLoader loader{};
    hftrec::corpus::SessionCorpus corpus{};
    hftrec::corpus::LoadReport report{};

    ASSERT_EQ(loader.loadDetailed(fixtureDir("corrupt_schema_mismatch"), corpus, report), hftrec::Status::CorruptData);
    ASSERT_FALSE(report.issues.empty());
    EXPECT_EQ(report.issues.front().code, hftrec::corpus::LoadIssueCode::UnsupportedSchemaVersion);
}

TEST(BinaryMarketCorpus, UnknownArrivalGapRejectsEveryReceiveInterval) {
    namespace binary = hftrec::corpus;
    const fs::path corpusDir = fs::temp_directory_path() /
        "hftrec_binary_unknown_arrival_gap_test";
    std::error_code cleanupError;
    fs::remove_all(corpusDir, cleanupError);

    const std::array sources{binaryBookTickerSource()};
    binary::BinaryMarketCorpusWriter writer{};
    binary::BinaryMarketWriterConfig config{};
    config.root = corpusDir;
    config.sources = std::span<const binary::BinaryMarketSource>{sources};
    config.producerEpoch = 11u;
    config.maximumBytes = 1024u * 1024u;
    config.targetDurationNs = 1'000u;
    config.startedReceiveNs = 1'000;
    config.startedMonotonicNs = 500u;
    config.ringCapacity = 1u;
    config.shardCount = 1u;
    ASSERT_EQ(writer.start(config), hftrec::Status::Ok);

    binary::BinaryMarketGap gap{};
    gap.sourceId = 1u;
    gap.sourceGeneration = sources[0].initialSourceGeneration;
    gap.gapEpoch = 1u;
    gap.droppedRecords = 1u;
    gap.firstDroppedEventSequence = 1u;
    gap.lastDroppedEventSequence = 1u;
    gap.minimumDroppedReceiveNs = 0;
    gap.maximumDroppedReceiveNs = 0;
    gap.observedReceiveNs = 2'000;
    gap.channel = binary::BinaryMarketChannel::BookTicker;
    ASSERT_EQ(writer.appendGap(gap), hftrec::Status::Ok);
    ASSERT_EQ(writer.finalize(binary::BinaryMarketStopReason::Requested, 3'000),
              hftrec::Status::Ok);

    binary::BinaryMarketSelectionRequest request{};
    request.root = corpusDir;
    request.exchange = "binance";
    request.market = "futures";
    request.canonicalSymbol = "BTC_USDT";
    request.beginReceiveNs = 1'000;
    request.endReceiveNs = 3'000;
    request.channelMask = binary::binaryMarketChannelBit(
        binary::BinaryMarketChannel::BookTicker);
    request.requiredChannelMask = request.channelMask;
    binary::BinaryMarketSelection selection{};
    std::string error{};
    binary::BinaryMarketCorpusReader reader{};
    EXPECT_EQ(reader.select(request, selection, error),
              hftrec::Status::CorruptData);
    EXPECT_NE(error.find("capture gaps"), std::string::npos);

    fs::remove_all(corpusDir, cleanupError);
}

TEST(BinaryMarketCorpus,
     OptionalSelectedRecordedOnlyRecordRejectsTraderReplay) {
    namespace binary = hftrec::corpus;
    const fs::path corpusDir = fs::temp_directory_path() /
        "hftrec_binary_optional_recorded_only_test";
    std::error_code cleanupError;
    fs::remove_all(corpusDir, cleanupError);

    const std::array sources{binaryBookTickerSource()};
    binary::BinaryMarketCorpusWriter writer{};
    binary::BinaryMarketWriterConfig config{};
    config.root = corpusDir;
    config.sources = std::span<const binary::BinaryMarketSource>{sources};
    config.producerEpoch = 11u;
    config.maximumBytes = 1024u * 1024u;
    config.targetDurationNs = 1'000u;
    config.startedReceiveNs = 1'000;
    config.startedMonotonicNs = 500u;
    config.ringCapacity = 1u;
    config.shardCount = 1u;
    ASSERT_EQ(writer.start(config), hftrec::Status::Ok);
    ASSERT_EQ(writer.append(binaryBookTickerRecord(false)),
              hftrec::Status::Ok);
    ASSERT_EQ(writer.finalize(binary::BinaryMarketStopReason::Requested,
                              3'000),
              hftrec::Status::Ok);

    binary::BinaryMarketSelectionRequest request{};
    request.root = corpusDir;
    request.exchange = "binance";
    request.market = "futures";
    request.canonicalSymbol = "BTC_USDT";
    request.beginReceiveNs = 1'000;
    request.endReceiveNs = 3'000;
    request.channelMask = binary::binaryMarketChannelBit(
        binary::BinaryMarketChannel::BookTicker);
    request.requiredChannelMask = 0u;
    request.requireTraderReplayCompatibility = true;
    binary::BinaryMarketSelection selection{};
    std::string error{};
    binary::BinaryMarketCorpusReader reader{};
    EXPECT_EQ(reader.select(request, selection, error),
              hftrec::Status::Unimplemented);
    EXPECT_NE(error.find(
                  "selected binary corpus record is not trader-replay compatible"),
              std::string::npos);

    fs::remove_all(corpusDir, cleanupError);
}

TEST(CorpusLoader, StaleSeekIndexDegradesButStillLoads) {
    const fs::path sessionDir = fs::temp_directory_path() /
        "hftrec_current_stale_seek_index_test";
    std::error_code ec;
    fs::remove_all(sessionDir, ec);
    fs::copy(fixtureDir("clean_full"), sessionDir,
             fs::copy_options::recursive, ec);
    ASSERT_FALSE(ec);
    std::ofstream seek(sessionDir / "seek_index.json",
                       std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(seek.is_open());
    seek << R"({"version":1,"sources":{"trades.jsonl":{"size_bytes":1,"row_count":99}},"buckets":[{"ts_ns":2500,"event_index_start":1,"depth_row_index":1}]})";
    seek.close();

    hftrec::corpus::CorpusLoader loader{};
    hftrec::corpus::SessionCorpus corpus{};
    hftrec::corpus::LoadReport report{};

    ASSERT_EQ(loader.loadDetailed(sessionDir, corpus, report), hftrec::Status::Ok);
    EXPECT_TRUE(report.staleSeekIndex);
    EXPECT_FALSE(report.usedSeekIndex);
    EXPECT_EQ(report.seekIndexState, hftrec::corpus::ChannelLoadState::Degraded);
    fs::remove_all(sessionDir, ec);
}

TEST(CorpusLoader, RecordingManifestDoesNotRequireFinalSupportArtifacts) {
    const fs::path sessionDir = fs::temp_directory_path() / "hftrec_recording_manifest_loader_test";
    std::error_code ec;
    fs::remove_all(sessionDir, ec);
    ASSERT_TRUE(fs::create_directories(sessionDir, ec));
    ASSERT_FALSE(ec);

    hftrec::capture::SessionManifest manifest{};
    manifest.sessionId = "hftrec_recording_manifest_loader_test";
    manifest.exchange = "binance";
    manifest.market = "futures";
    manifest.symbols = {"ETH_USDT"};
    manifest.selectedParentDir = sessionDir.parent_path().string();
    manifest.startedAtNs = 1000;
    manifest.sessionStatus = "recording";
    manifest.canonicalArtifacts = {"manifest.json", manifest.instrumentMetadataPath};
    manifest.supportArtifacts = {};

    std::ofstream out(sessionDir / "manifest.json");
    ASSERT_TRUE(out.is_open());
    out << hftrec::capture::renderManifestJson(manifest);
    out.close();

    hftrec::corpus::CorpusLoader loader{};
    hftrec::corpus::SessionCorpus corpus{};
    hftrec::corpus::LoadReport report{};
    EXPECT_EQ(loader.loadDetailed(sessionDir, corpus, report), hftrec::Status::Ok);
    EXPECT_TRUE(report.manifestPresent);
    EXPECT_EQ(corpus.manifest.sessionStatus, "recording");

    fs::remove_all(sessionDir, ec);
}

TEST(SessionReplay, FixtureReplayUsesSharedLoaderVerdict) {
    hftrec::replay::SessionReplay replay{};
    ASSERT_EQ(replay.open(fixtureDir("clean_full")), hftrec::Status::Ok);
    EXPECT_TRUE(replay.loadReport().usedSeekIndex);
    EXPECT_EQ(replay.book().bestBidPrice(), 0);
    EXPECT_EQ(replay.book().bestAskPrice(), 0);

    replay.seek(2000);
    EXPECT_EQ(replay.book().bestBidPrice(), 30000);
    EXPECT_EQ(replay.book().bestAskPrice(), 30100);

    replay.seek(3000);
    EXPECT_EQ(replay.book().bestBidQty(), 7);
    EXPECT_EQ(replay.book().bestAskPrice(), 30200);

    replay.seek(1000);
    EXPECT_EQ(replay.book().bestBidQty(), 0);
    EXPECT_EQ(replay.book().bestAskPrice(), 0);
}

TEST(SessionReplay, LegacyDepthFixtureIsRejectedWithoutMigration) {
    hftrec::replay::SessionReplay replay{};
    EXPECT_EQ(replay.open(fixtureDir("corrupt_depth_gap")),
              hftrec::Status::CorruptData);
    EXPECT_FALSE(replay.loadReport().issues.empty());
}

}  // namespace
