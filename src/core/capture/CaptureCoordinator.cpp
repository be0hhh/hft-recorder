#include "core/capture/CaptureCoordinator.hpp"

#include <filesystem>
#include <fstream>


#include "core/capture/CaptureCoordinatorInternal.hpp"
#include "core/capture/SessionId.hpp"
#include "core/capture/SupportArtifacts.hpp"
#include "core/corpus/InstrumentMetadata.hpp"

namespace hftrec::capture {

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
    manifest_.tradesPath = "trades.jsonl";
    manifest_.bookTickerPath = "bookticker.jsonl";
    manifest_.depthPath = "depth.jsonl";
    manifest_.canonicalArtifacts = {"manifest.json", manifest_.instrumentMetadataPath};
    manifest_.captureContractVersion = "hftrec.cxet_prefix_json.v2";
    manifest_.tradesRowSchema = "cxet_trade_prefix_v2";
    manifest_.bookTickerRowSchema = "cxet_bookticker_prefix_v2";
    manifest_.depthRowSchema = "cxet_orderbook_prefix_v2";
    manifest_.snapshotSchema = "cxet_orderbook_snapshot_prefix_v2";

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
    manifest_.bookTickerCount = bookTickerCount_.load(std::memory_order_relaxed);
    manifest_.depthCount = depthCount_.load(std::memory_order_relaxed);
    manifest_.snapshotCount = snapshotCount_.load(std::memory_order_relaxed);
    manifest_.warningSummary = lastError_;
    manifest_.structuralBlockers.clear();
    manifest_.structurallyLoadable = true;

    if (const auto supportStatus = writeSupportArtifacts(); !isOk(supportStatus)) {
        lastError_ = "failed to write support artifacts";
        return supportStatus;
    }

    std::ofstream manifestStream(sessionDir_ / "manifest.json", std::ios::out | std::ios::trunc);
    if (!manifestStream.is_open()) {
        lastError_ = "failed to open manifest.json for writing";
        return Status::IoError;
    }
    manifestStream << renderManifestJson(manifest_);
    if (!manifestStream.good()) {
        lastError_ = "failed to write manifest.json";
        return Status::IoError;
    }

    (void)eventSink_.close();
    (void)tradesWriter_.close();
    (void)bookTickerWriter_.close();
    (void)depthWriter_.close();
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
    bookTickerStop_.store(false, std::memory_order_release);
    orderbookStop_.store(false, std::memory_order_release);
    tradesRunning_.store(false, std::memory_order_release);
    bookTickerRunning_.store(false, std::memory_order_release);
    orderbookRunning_.store(false, std::memory_order_release);
    tradesCount_.store(0, std::memory_order_release);
    bookTickerCount_.store(0, std::memory_order_release);
    depthCount_.store(0, std::memory_order_release);
    snapshotCount_.store(0, std::memory_order_release);
    tradesCaptureSeq_.store(0, std::memory_order_release);
    bookTickerCaptureSeq_.store(0, std::memory_order_release);
    depthCaptureSeq_.store(0, std::memory_order_release);
    snapshotCaptureSeq_.store(0, std::memory_order_release);
    ingestSeq_.store(0, std::memory_order_release);
    liveStore_.clear();
    eventSink_.clearSinks();
}

bool CaptureCoordinator::sessionOpen() const noexcept {
    return !sessionDir_.empty();
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
        {sessionDir_ / manifest_.integrityReportPath, renderIntegrityReportJson(manifest_, generatedAtNs)},
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
