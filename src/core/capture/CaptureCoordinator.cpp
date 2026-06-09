#include "core/capture/CaptureCoordinator.hpp"

#include <filesystem>
#include <fstream>


#include "core/capture/CaptureCoordinatorInternal.hpp"
#include "core/capture/SessionId.hpp"
#include "core/capture/SupportArtifacts.hpp"
#include "core/corpus/InstrumentMetadata.hpp"
#include "core/replay/SessionReplay.hpp"

namespace hftrec::capture {

namespace {

constexpr std::int64_t kRecordingManifestFlushIntervalNs = 5'000'000'000LL;

}  // namespace

CaptureCoordinator::CaptureCoordinator() = default;

CaptureCoordinator::~CaptureCoordinator() {
    (void)finalizeSession();
}

Status CaptureCoordinator::ensureSession(const CaptureConfig& config) noexcept {
    internal::ensureCxetInitialized();

    if (const auto validateStatus = internal::validateSupportedConfig(config, lastError_); !isOk(validateStatus)) {
        return validateStatus;
    }

    std::lock_guard<std::mutex> lock(stateMutex_);
    if (sessionOpen()) {
        if (!internal::sessionConfigMatches(config_, config)) {
            lastError_ = "capture session already open with a different exchange/market/symbol/output directory";
            return Status::InvalidArgument;
        }
        config_.tradesHistoryWarmupSec = config.tradesHistoryWarmupSec;
        manifest_.tradesHistoryWarmupSec = config.tradesHistoryWarmupSec;
        return Status::Ok;
    }

    config_ = config;
    manifest_ = {};
    manifest_.sessionId = makeSessionId(config.exchange, config.market, config.symbols.front(), internal::nowSec());
    manifest_.exchange = config.exchange;
    manifest_.market = config.market;
    manifest_.symbols = config.symbols;
    manifest_.selectedParentDir = config.outputDir.string();
    manifest_.startedAtNs = internal::nowNs();
    manifest_.targetDurationSec = config.durationSec;
    manifest_.snapshotIntervalSec = config.snapshotIntervalSec;
    manifest_.tradesHistoryWarmupSec = config.tradesHistoryWarmupSec;
    manifest_.tradesPath = std::string{channelJsonlRelativePath(ChannelKind::Trades)};
    manifest_.liquidationsPath = std::string{channelJsonlRelativePath(ChannelKind::Liquidations)};
    manifest_.bookTickerPath = std::string{channelJsonlRelativePath(ChannelKind::BookTicker)};
    manifest_.depthPath = std::string{channelJsonlRelativePath(ChannelKind::DepthDelta)};
    manifest_.candlesPath = std::string{channelJsonlRelativePath(ChannelKind::Candles)};
    manifest_.canonicalArtifacts = {"manifest.json", manifest_.instrumentMetadataPath};
    manifest_.captureContractVersion = "hftrec.strict_canonical_rows_json.v1";
    manifest_.tradesRowSchema = "cxet_trade_strict_v1";
    manifest_.liquidationsRowSchema = "cxet_liquidation_alias_first_v1";
    manifest_.bookTickerRowSchema = "cxet_bookticker_strict_v1";
    manifest_.depthRowSchema = "cxet_orderbook_flat_levels_v1";
    manifest_.candlesRowSchema = "cxet_candle_lite_tiered_v1";
    manifest_.snapshotSchema = "cxet_orderbook_snapshot_flat_levels_v1";
    manifest_.sessionStatus = "recording";

    sessionDir_ = config.outputDir / manifest_.sessionId;
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
    liveStore_.clear();
    if (const auto storageStatus = jsonSink_.open(sessionDir_); !isOk(storageStatus)) {
        lastError_ = "failed to open JSON session storage";
        return storageStatus;
    }
    eventSink_.clearSinks();
    eventSink_.addSink(&liveStore_);
    eventSink_.addSink(&jsonSink_);
    manifest_.supportArtifacts = {
        manifest_.sessionAuditPath,
        manifest_.integrityReportPath,
        manifest_.loaderDiagnosticsPath,
    };

    lastError_.clear();
    return Status::Ok;
}

Status CaptureCoordinator::finalizeSession() noexcept {
    (void)stopTrades();
    (void)stopLiquidations();
    (void)stopBookTicker();
    (void)stopOrderbook();

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
    manifest_.depthCount = depthCount_.load(std::memory_order_relaxed);
    manifest_.candlesCount = candlesCount_.load(std::memory_order_relaxed);
    manifest_.snapshotCount = snapshotCount_.load(std::memory_order_relaxed);
    manifest_.warningSummary = lastError_;
    manifest_.structuralBlockers.clear();
    manifest_.structurallyLoadable = true;

    (void)eventSink_.close();
    (void)tradesWriter_.close();
    (void)liquidationsWriter_.close();
    (void)bookTickerWriter_.close();
    (void)candlesWriter_.close();
    (void)depthWriter_.close();

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
    return liveStore_.readAll();
}

void CaptureCoordinator::resetSessionState() noexcept {
    manifest_ = {};
    sessionDir_.clear();
    config_ = {};
    lastError_.clear();
    tradesStop_.store(false, std::memory_order_release);
    liquidationsStop_.store(false, std::memory_order_release);
    bookTickerStop_.store(false, std::memory_order_release);
    orderbookStop_.store(false, std::memory_order_release);
    tradesRunning_.store(false, std::memory_order_release);
    liquidationsRunning_.store(false, std::memory_order_release);
    bookTickerRunning_.store(false, std::memory_order_release);
    orderbookRunning_.store(false, std::memory_order_release);
    tradesCount_.store(0, std::memory_order_release);
    liquidationsCount_.store(0, std::memory_order_release);
    bookTickerCount_.store(0, std::memory_order_release);
    depthCount_.store(0, std::memory_order_release);
    candlesCount_.store(0, std::memory_order_release);
    snapshotCount_.store(0, std::memory_order_release);
    tradesCaptureSeq_.store(0, std::memory_order_release);
    liquidationsCaptureSeq_.store(0, std::memory_order_release);
    bookTickerCaptureSeq_.store(0, std::memory_order_release);
    ingestSeq_.store(0, std::memory_order_release);
    (void)tradesWriter_.close();
    (void)liquidationsWriter_.close();
    (void)bookTickerWriter_.close();
    (void)candlesWriter_.close();
    (void)depthWriter_.close();
    liveStore_.clear();
    eventSink_.clearSinks();
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
    manifest_.depthCount = depthCount_.load(std::memory_order_relaxed);
    manifest_.candlesCount = candlesCount_.load(std::memory_order_relaxed);
    manifest_.snapshotCount = snapshotCount_.load(std::memory_order_relaxed);
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
    {
        std::ofstream stream(tempPath, std::ios::out | std::ios::trunc);
        if (!stream.is_open()) return Status::IoError;
        stream << renderManifestJson(manifest_);
        if (!stream.good()) return Status::IoError;
    }

    std::error_code ec;
    std::filesystem::rename(tempPath, manifestPath, ec);
    if (!ec) return Status::Ok;

    ec.clear();
    std::filesystem::remove(manifestPath, ec);
    if (ec) {
        std::filesystem::remove(tempPath, ec);
        return Status::IoError;
    }
    ec.clear();
    std::filesystem::rename(tempPath, manifestPath, ec);
    if (ec) {
        std::error_code cleanupEc;
        std::filesystem::remove(tempPath, cleanupEc);
        return Status::IoError;
    }
    return Status::Ok;
}

Status CaptureCoordinator::writeInstrumentMetadataFile() noexcept {
    if (config_.symbols.empty()) return Status::InvalidArgument;
    const auto metadata = corpus::makeInstrumentMetadata(config_.exchange, config_.market, config_.symbols.front());
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


