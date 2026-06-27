#include "core/capture/CaptureCoordinator.hpp"

#include <filesystem>
#include <fstream>
#include <string_view>


#include "core/capture/CaptureCoordinatorInternal.hpp"
#include "core/capture/SessionId.hpp"
#include "core/capture/SupportArtifacts.hpp"
#include "core/corpus/InstrumentMetadata.hpp"
#include "core/recordings/RecordingRoot.hpp"
#include "core/replay/SessionReplay.hpp"

#include <algorithm>

namespace hftrec::capture {

namespace {

constexpr std::int64_t kRecordingManifestFlushIntervalNs = 5'000'000'000LL;

bool hasCapturedRows(const SessionManifest& manifest) noexcept {
    return manifest.tradesCount != 0u
        || manifest.liquidationsCount != 0u
        || manifest.bookTickerCount != 0u
        || manifest.markPriceCount != 0u
        || manifest.indexPriceCount != 0u
        || manifest.fundingCount != 0u
        || manifest.priceLimitCount != 0u
        || manifest.depthCount != 0u
        || manifest.candlesCount != 0u
        || manifest.candles2Count != 0u;
}

Status writeFileFully(const std::filesystem::path& path, const std::string& document) noexcept {
    if (document.empty()) return Status::IoError;
    {
        std::ofstream stream(path, std::ios::out | std::ios::binary | std::ios::trunc);
        if (!stream.is_open()) return Status::IoError;
        stream.write(document.data(), static_cast<std::streamsize>(document.size()));
        stream.flush();
        if (!stream.good()) {
            stream.close();
            return Status::IoError;
        }
        stream.close();
        if (!stream.good()) return Status::IoError;
    }

    std::error_code ec;
    const std::uintmax_t written = std::filesystem::file_size(path, ec);
    if (ec || written != document.size()) {
        std::filesystem::remove(path, ec);
        return Status::IoError;
    }
    return Status::Ok;
}

Status replaceFilePreservingPrevious(const std::filesystem::path& tempPath,
                                     const std::filesystem::path& targetPath) noexcept {
    const std::filesystem::path previousPath = targetPath.string() + ".prev";
    bool previousReady = false;

    std::error_code ec;
    if (std::filesystem::exists(targetPath, ec) && !ec) {
        const std::uintmax_t targetSize = std::filesystem::file_size(targetPath, ec);
        if (!ec && targetSize > 0u) {
            std::filesystem::copy_file(targetPath,
                                       previousPath,
                                       std::filesystem::copy_options::overwrite_existing,
                                       ec);
            if (ec) return Status::IoError;
            previousReady = true;
        }
    }

    ec.clear();
    std::filesystem::rename(tempPath, targetPath, ec);
    if (!ec) return Status::Ok;

    ec.clear();
    if (std::filesystem::exists(targetPath, ec) && !ec) {
        std::filesystem::remove(targetPath, ec);
        if (ec) return Status::IoError;
    }

    ec.clear();
    std::filesystem::rename(tempPath, targetPath, ec);
    if (!ec) return Status::Ok;

    if (previousReady) {
        std::error_code restoreEc;
        std::filesystem::copy_file(previousPath,
                                   targetPath,
                                   std::filesystem::copy_options::overwrite_existing,
                                   restoreEc);
    }
    return Status::IoError;
}

}  // namespace

CaptureCoordinator::CaptureCoordinator() = default;

CaptureCoordinator::~CaptureCoordinator() {
    (void)finalizeSession();
}

Status CaptureCoordinator::ensureSession(const CaptureConfig& config) noexcept {
    internal::ensureCxetInitialized();
    CaptureConfig normalizedConfig = config;
    normalizedConfig.outputDir = recordings::normalizeExplicitRecordingsPath(config.outputDir);

    if (const auto envStatus = internal::loadCaptureEnv(normalizedConfig, lastError_); !isOk(envStatus)) {
        return envStatus;
    }

    if (const auto validateStatus = internal::validateSupportedConfig(normalizedConfig, lastError_); !isOk(validateStatus)) {
        return validateStatus;
    }

    std::lock_guard<std::mutex> lock(stateMutex_);
    if (sessionOpen()) {
        if (!internal::sessionConfigMatches(config_, normalizedConfig)) {
            lastError_ = "capture session already open with a different exchange/market/symbol/env/api/output directory";
            return Status::InvalidArgument;
        }
        config_.tradesHistoryWarmupSec = normalizedConfig.tradesHistoryWarmupSec;
        manifest_.tradesHistoryWarmupSec = normalizedConfig.tradesHistoryWarmupSec;
        return Status::Ok;
    }

    config_ = normalizedConfig;
    manifest_ = {};
    manifest_.sessionId = makeSessionId(normalizedConfig.exchange, normalizedConfig.market, normalizedConfig.symbols.front(), internal::nowNs());
    manifest_.exchange = normalizedConfig.exchange;
    manifest_.market = normalizedConfig.market;
    manifest_.symbols = normalizedConfig.symbols;
    manifest_.selectedParentDir = normalizedConfig.outputDir.string();
    manifest_.startedAtNs = internal::nowNs();
    manifest_.targetDurationSec = normalizedConfig.durationSec;
    manifest_.snapshotIntervalSec = normalizedConfig.snapshotIntervalSec;
    manifest_.tradesHistoryWarmupSec = normalizedConfig.tradesHistoryWarmupSec;
    manifest_.tradesPath = std::string{channelJsonlRelativePath(ChannelKind::Trades)};
    manifest_.liquidationsPath = std::string{channelJsonlRelativePath(ChannelKind::Liquidations)};
    manifest_.bookTickerPath = std::string{channelJsonlRelativePath(ChannelKind::BookTicker)};
    manifest_.depthPath = std::string{channelJsonlRelativePath(ChannelKind::DepthTape)};
    manifest_.depthSidecarPath = std::string{channelJsonlRelativePath(ChannelKind::DepthSidecar)};
    manifest_.candlesPath = std::string{channelJsonlRelativePath(ChannelKind::Candles)};
    manifest_.candles2Path = std::string{channelJsonlRelativePath(ChannelKind::Candles2)};
    manifest_.markPricePath = std::string{channelJsonlRelativePath(ChannelKind::MarkPrice)};
    manifest_.indexPricePath = std::string{channelJsonlRelativePath(ChannelKind::IndexPrice)};
    manifest_.fundingPath = std::string{channelJsonlRelativePath(ChannelKind::Funding)};
    manifest_.priceLimitPath = std::string{channelJsonlRelativePath(ChannelKind::PriceLimit)};
    manifest_.canonicalArtifacts = {"manifest.json", manifest_.instrumentMetadataPath};
    manifest_.captureContractVersion = "hftrec.strict_canonical_rows_json.v2";
    manifest_.tradesRowSchema = "cxet_trade_strict_v1";
    manifest_.liquidationsRowSchema = "cxet_liquidation_alias_first_v1";
    manifest_.bookTickerRowSchema = "cxet_bookticker_strict_v1";
    manifest_.depthRowSchema = "cxet_orderbook_tape_rle_sidecar_v1";
    manifest_.candlesRowSchema = "cxet_candle_lite_tiered_v1";
    manifest_.candles2RowSchema = "cxet_ohlcv_numeric_v3";
    manifest_.sessionStatus = "recording";

    sessionDir_ = normalizedConfig.outputDir / manifest_.sessionId;
    std::error_code ec;
    if (std::filesystem::exists(sessionDir_, ec)) {
        lastError_ = "session path already exists: " + sessionDir_.string();
        return Status::IoError;
    }

    std::filesystem::create_directories(sessionDir_, ec);
    if (ec) {
        lastError_ = "failed to create session directory: " + sessionDir_.string();
        return Status::IoError;
    }

    if (const auto metadataStatus = writeInstrumentMetadataFile(); !isOk(metadataStatus)) {
        lastError_ = "failed to write instrument metadata sidecar";
        return metadataStatus;
    }
    if (const auto manifestStatus = writeManifestFile_(); !isOk(manifestStatus)) {
        lastError_ = "failed to write initial manifest.json";
        return manifestStatus;
    }
    liveCacheEnabled_.store(config.liveCacheMode == LiveCacheMode::Full, std::memory_order_release);
    liveStore_.clear();
    if (const auto storageStatus = jsonSink_.open(sessionDir_); !isOk(storageStatus)) {
        lastError_ = "failed to open JSON session storage";
        return storageStatus;
    }
    eventSink_.clearSinks();
    if (liveCacheEnabled()) eventSink_.addSink(&liveStore_);
    eventSink_.addSink(&jsonSink_);
    manifest_.supportArtifacts = {
        manifest_.sessionAuditPath,
        manifest_.integrityReportPath,
        manifest_.loaderDiagnosticsPath,
        manifest_.marketDataLaunchPath,
    };

    lastError_.clear();
    return Status::Ok;
}

Status CaptureCoordinator::finalizeSession() noexcept {
    (void)stopTrades();
    (void)stopLiquidations();
    (void)stopBookTicker();
    (void)stopOrderbook();
    (void)stopMarkPrice();
    (void)stopIndexPrice();
    (void)stopFunding();
    (void)stopPriceLimit();

    std::lock_guard<std::mutex> lock(stateMutex_);
    if (!sessionOpen()) {
        return Status::Ok;
    }

    manifest_.endedAtNs = internal::nowNs();
    if (manifest_.startedAtNs > 0 && manifest_.endedAtNs >= manifest_.startedAtNs) {
        manifest_.actualDurationSec = (manifest_.endedAtNs - manifest_.startedAtNs) / 1000000000LL;
    }
    manifest_.tradesCount = tradesCount_.load(std::memory_order_relaxed);
    manifest_.liquidationsCount = liquidationsCount_.load(std::memory_order_relaxed);
    manifest_.bookTickerCount = bookTickerCount_.load(std::memory_order_relaxed);
    manifest_.markPriceCount = markPriceCount_.load(std::memory_order_relaxed);
    manifest_.indexPriceCount = indexPriceCount_.load(std::memory_order_relaxed);
    manifest_.fundingCount = fundingCount_.load(std::memory_order_relaxed);
    manifest_.priceLimitCount = priceLimitCount_.load(std::memory_order_relaxed);
    manifest_.depthCount = depthCount_.load(std::memory_order_relaxed);
    manifest_.candlesCount = candlesCount_.load(std::memory_order_relaxed);
    manifest_.candles2Count = candles2Count_.load(std::memory_order_relaxed);
    manifest_.warningSummary = lastError_;
    manifest_.structuralBlockers.clear();
    manifest_.structurallyLoadable = true;

    auto noteCloseStatus = [&](Status status, std::string_view label) {
        if (isOk(status)) return;
        if (!lastError_.empty()) lastError_ += " | ";
        lastError_ += std::string{label};
    };
	
    noteCloseStatus(eventSink_.close(), "event sink close failed");
    noteCloseStatus(tradesWriter_.close(), "trades writer close failed");
    noteCloseStatus(liquidationsWriter_.close(), "liquidations writer close failed");
    noteCloseStatus(bookTickerWriter_.close(), "bookticker writer close failed");
    noteCloseStatus(candlesWriter_.close(), "candles writer close failed");
    noteCloseStatus(candles2Writer_.close(), "candles2 writer close failed");
    noteCloseStatus(depthWriter_.close(), "depth writer close failed");
    manifest_.warningSummary = lastError_;

    if (!hasCapturedRows(manifest_)) {
        if (manifest_.warningSummary.empty()) {
            lastError_ = "no canonical rows captured";
            manifest_.warningSummary = lastError_;
        }
        manifest_.sessionStatus = "failed_empty";
        manifest_.sessionHealth = SessionHealth::Degraded;
        manifest_.exactReplayEligible = false;
        if (std::find(manifest_.supportArtifacts.begin(),
                      manifest_.supportArtifacts.end(),
                      manifest_.marketDataLaunchPath) == manifest_.supportArtifacts.end()) {
            manifest_.supportArtifacts.push_back(manifest_.marketDataLaunchPath);
        }
        if (const auto manifestStatus = writeManifestFile_(); !isOk(manifestStatus)) {
            lastError_ = "failed to write empty failed-session manifest.json";
            return manifestStatus;
        }
        if (const auto supportStatus = writeSupportArtifacts(); !isOk(supportStatus)) {
            lastError_ = "failed to write empty failed-session support artifacts";
            return supportStatus;
        }
        resetSessionState();
        return Status::Ok;
    }

    if (const auto seedStatus = writeManifestFile_(); !isOk(seedStatus)) {
        lastError_ = "failed to seed manifest.json for integrity sync";
        return seedStatus;
    }

    syncManifestIntegrityFromReplay_();

    if (const auto supportStatus = writeSupportArtifacts(); !isOk(supportStatus)) {
        lastError_ = "failed to write support artifacts";
        return supportStatus;
    }

    manifest_.sessionStatus = "complete";
    if (const auto manifestStatus = writeManifestFile_(); !isOk(manifestStatus)) {
        lastError_ = "failed to write manifest.json";
        return manifestStatus;
    }

    resetSessionState();
    return Status::Ok;
}

std::string CaptureCoordinator::lastError() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return lastError_;
}

SessionManifest CaptureCoordinator::manifestCopy() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return manifest_;
}

std::filesystem::path CaptureCoordinator::sessionDirCopy() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return sessionDir_;
}

storage::EventBatch CaptureCoordinator::liveEventsCopy() const {
    if (!liveCacheEnabled()) return {};
    return liveStore_.readAll();
}

Status CaptureCoordinator::appendLiveTrade(const replay::TradeRow& row) noexcept {
    return liveCacheEnabled() ? liveStore_.appendTrade(row) : Status::Ok;
}

Status CaptureCoordinator::appendLiveLiquidation(const replay::LiquidationRow& row) noexcept {
    return liveCacheEnabled() ? liveStore_.appendLiquidation(row) : Status::Ok;
}

Status CaptureCoordinator::appendLiveBookTicker(const replay::BookTickerRow& row) noexcept {
    return liveCacheEnabled() ? liveStore_.appendBookTicker(row) : Status::Ok;
}

Status CaptureCoordinator::appendLiveMarkPrice(const replay::MarkPriceRow& row) noexcept {
    return liveCacheEnabled() ? liveStore_.appendMarkPrice(row) : Status::Ok;
}

Status CaptureCoordinator::appendLiveIndexPrice(const replay::IndexPriceRow& row) noexcept {
    return liveCacheEnabled() ? liveStore_.appendIndexPrice(row) : Status::Ok;
}

Status CaptureCoordinator::appendLiveFunding(const replay::FundingRow& row) noexcept {
    return liveCacheEnabled() ? liveStore_.appendFunding(row) : Status::Ok;
}

Status CaptureCoordinator::appendLivePriceLimit(const replay::PriceLimitRow& row) noexcept {
    return liveCacheEnabled() ? liveStore_.appendPriceLimit(row) : Status::Ok;
}

Status CaptureCoordinator::appendLiveDepth(const replay::DepthRow& row) noexcept {
    return liveCacheEnabled() ? liveStore_.appendDepth(row) : Status::Ok;
}

void CaptureCoordinator::resetSessionState() noexcept {
    manifest_ = {};
    sessionDir_.clear();
    config_ = {};
    instrumentMetadataReady_ = false;
    lastError_.clear();
    tradesStop_.store(false, std::memory_order_release);
    liquidationsStop_.store(false, std::memory_order_release);
    bookTickerStop_.store(false, std::memory_order_release);
    orderbookStop_.store(false, std::memory_order_release);
    markPriceStop_.store(false, std::memory_order_release);
    indexPriceStop_.store(false, std::memory_order_release);
    fundingStop_.store(false, std::memory_order_release);
    priceLimitStop_.store(false, std::memory_order_release);
    tradesRunning_.store(false, std::memory_order_release);
    liquidationsRunning_.store(false, std::memory_order_release);
    bookTickerRunning_.store(false, std::memory_order_release);
    orderbookRunning_.store(false, std::memory_order_release);
    markPriceRunning_.store(false, std::memory_order_release);
    indexPriceRunning_.store(false, std::memory_order_release);
    fundingRunning_.store(false, std::memory_order_release);
    priceLimitRunning_.store(false, std::memory_order_release);
    tradesCount_.store(0, std::memory_order_release);
    liquidationsCount_.store(0, std::memory_order_release);
    bookTickerCount_.store(0, std::memory_order_release);
    markPriceCount_.store(0, std::memory_order_release);
    indexPriceCount_.store(0, std::memory_order_release);
    fundingCount_.store(0, std::memory_order_release);
    priceLimitCount_.store(0, std::memory_order_release);
    depthCount_.store(0, std::memory_order_release);
    candlesCount_.store(0, std::memory_order_release);
    candles2Count_.store(0, std::memory_order_release);
    tradesCaptureSeq_.store(0, std::memory_order_release);
    liquidationsCaptureSeq_.store(0, std::memory_order_release);
    bookTickerCaptureSeq_.store(0, std::memory_order_release);
    ingestSeq_.store(0, std::memory_order_release);
    (void)tradesWriter_.close();
    (void)liquidationsWriter_.close();
    (void)bookTickerWriter_.close();
    (void)candlesWriter_.close();
    (void)candles2Writer_.close();
    (void)depthWriter_.close();
    liveStore_.clear();
    eventSink_.clearSinks();
    liveCacheEnabled_.store(false, std::memory_order_release);
}

bool CaptureCoordinator::sessionOpen() const noexcept {
    return !sessionDir_.empty();
}

void CaptureCoordinator::refreshRecordingManifestLocked_(std::int64_t nowNs) noexcept {
    manifest_.sessionStatus = "recording";
    manifest_.endedAtNs = nowNs;
    if (manifest_.startedAtNs > 0 && manifest_.endedAtNs >= manifest_.startedAtNs) {
        manifest_.actualDurationSec = (manifest_.endedAtNs - manifest_.startedAtNs) / 1000000000LL;
    }
    manifest_.tradesCount = tradesCount_.load(std::memory_order_relaxed);
    manifest_.liquidationsCount = liquidationsCount_.load(std::memory_order_relaxed);
    manifest_.bookTickerCount = bookTickerCount_.load(std::memory_order_relaxed);
    manifest_.markPriceCount = markPriceCount_.load(std::memory_order_relaxed);
    manifest_.indexPriceCount = indexPriceCount_.load(std::memory_order_relaxed);
    manifest_.fundingCount = fundingCount_.load(std::memory_order_relaxed);
    manifest_.priceLimitCount = priceLimitCount_.load(std::memory_order_relaxed);
    manifest_.depthCount = depthCount_.load(std::memory_order_relaxed);
    manifest_.candlesCount = candlesCount_.load(std::memory_order_relaxed);
    manifest_.candles2Count = candles2Count_.load(std::memory_order_relaxed);
    manifest_.warningSummary = lastError_;
    manifest_.structuralBlockers.clear();
    manifest_.structurallyLoadable = true;
}

Status CaptureCoordinator::flushRecordingManifestIfDue_(std::int64_t& nextFlushNs) noexcept {
    const auto nowNs = internal::nowNs();
    if (nowNs < nextFlushNs) {
        return Status::Ok;
    }
    nextFlushNs = nowNs + kRecordingManifestFlushIntervalNs;

    std::lock_guard<std::mutex> lock(stateMutex_);
    if (!sessionOpen()) {
        return Status::Ok;
    }

    refreshRecordingManifestLocked_(nowNs);
    const auto status = writeManifestFile_();
    if (!isOk(status) && lastError_.empty()) {
        lastError_ = "failed to refresh manifest.json";
    }
    return status;
}

void CaptureCoordinator::syncManifestIntegrityFromReplay_() noexcept {
    replay::SessionReplay replay;
    const auto replayStatus = replay.open(sessionDir_);
    const auto& summary = replay.integritySummary();

    manifest_.sessionHealth = summary.sessionHealth;
    manifest_.exactReplayEligible = summary.exactReplayEligible;
    manifest_.tradesIntegrity = summary.trades;
    manifest_.liquidationsIntegrity = summary.liquidations;
    manifest_.bookTickerIntegrity = summary.bookTicker;
    manifest_.depthIntegrity = summary.depth;
    manifest_.snapshotIntegrity = summary.snapshot;
    manifest_.totalIntegrityIncidents = summary.totalIncidents;
    manifest_.highestIntegritySeverity = summary.highestSeverity;

    if (!isOk(replayStatus) && !replay.errorDetail().empty()) {
        if (!manifest_.warningSummary.empty()) {
            manifest_.warningSummary += " | ";
        }
        manifest_.warningSummary += std::string{replay.errorDetail()};
    }
}

Status CaptureCoordinator::writeManifestFile_() noexcept {
    if (sessionDir_.empty()) return Status::InvalidArgument;
    const auto manifestPath = sessionDir_ / "manifest.json";
    const auto tempPath = sessionDir_ / "manifest.json.tmp";
    const std::string document = renderManifestJson(manifest_);
    if (const auto writeStatus = writeFileFully(tempPath, document); !isOk(writeStatus)) return writeStatus;
    return replaceFilePreservingPrevious(tempPath, manifestPath);
}

Status CaptureCoordinator::writeInstrumentMetadataFile() noexcept {
    if (config_.symbols.empty()) return Status::InvalidArgument;
    auto metadata = corpus::makeInstrumentMetadata(config_.exchange, config_.market, config_.symbols.front());
    // Do not block session creation on venue exchangeInfo REST. Newly added
    // venues can stall here after the directory is created but before the
    // initial manifest and JSON sinks are opened. Start with recorder-inferred
    // metadata; cold enrichment can happen after the session is recording.
    metadata.metadataWarning = "hft_trader_metadata_deferred_startup_nonblocking";
    instrumentMetadataReady_ = false;
    std::ofstream out(sessionDir_ / manifest_.instrumentMetadataPath, std::ios::out | std::ios::trunc);
    if (!out.is_open()) return Status::IoError;
    out << corpus::renderInstrumentMetadataJson(metadata);
    return out.good() ? Status::Ok : Status::IoError;
}

Status CaptureCoordinator::writeSupportArtifacts() noexcept {
    std::error_code ec;
    std::filesystem::create_directories(sessionDir_ / "reports", ec);
    if (ec) return Status::IoError;

    const auto generatedAtNs = internal::nowNs();
    struct ArtifactSpec {
        std::filesystem::path path;
        std::string document;
    };
    const ArtifactSpec artifacts[] = {
        {sessionDir_ / manifest_.sessionAuditPath, renderSessionAuditJson(manifest_, generatedAtNs)},
        {sessionDir_ / manifest_.loaderDiagnosticsPath, renderLoaderDiagnosticsJson(manifest_, generatedAtNs)},
        {sessionDir_ / manifest_.marketDataLaunchPath, renderMarketDataLaunchJson(manifest_, generatedAtNs)},
    };
    for (const auto& artifact : artifacts) {
        std::ofstream out(artifact.path, std::ios::out | std::ios::trunc);
        if (!out.is_open()) return Status::IoError;
        out << artifact.document;
        if (!out.good()) return Status::IoError;
    }
    return Status::Ok;
}

}  // namespace hftrec::capture


