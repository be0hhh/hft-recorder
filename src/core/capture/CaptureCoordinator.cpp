#include "core/capture/CaptureCoordinator.hpp"

#include <filesystem>
#include <fstream>
#include <string_view>


#include "core/capture/CaptureCoordinatorInternal.hpp"
#include "core/capture/CaptureCoordinatorRuntimeHelpers.hpp"
#include "core/capture/JsonSerializers.hpp"
#include "core/capture/SessionId.hpp"
#include "core/capture/SupportArtifacts.hpp"
#include "core/corpus/InstrumentMetadata.hpp"
#include "core/recordings/RecordingDiscovery.hpp"
#include "core/recordings/RecordingRoot.hpp"
#include "core/replay/SessionReplay.hpp"

#include <algorithm>

namespace hftrec::capture {

namespace {

constexpr std::int64_t kRecordingManifestFlushIntervalNs = 5'000'000'000LL;

Status aggregateStatus(Status current, Status next) noexcept {
    return isOk(current) ? next : current;
}

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

bool runtimeHealthDegraded(const ChannelRuntimeHealth& health) noexcept {
    return (health.required && health.state != "live") ||
           health.reconnectCount != 0u ||
           health.droppedEventCount != 0u ||
           health.unroutableEventCount != 0u ||
           (!health.lastError.empty() && health.required);
}

bool runtimeHealthDegraded(const SessionManifest& manifest) noexcept {
    return runtimeHealthDegraded(manifest.tradesRuntime) ||
           runtimeHealthDegraded(manifest.liquidationsRuntime) ||
           runtimeHealthDegraded(manifest.bookTickerRuntime) ||
           runtimeHealthDegraded(manifest.depthRuntime) ||
           runtimeHealthDegraded(manifest.markPriceRuntime) ||
           runtimeHealthDegraded(manifest.indexPriceRuntime) ||
           runtimeHealthDegraded(manifest.fundingRuntime) ||
           runtimeHealthDegraded(manifest.priceLimitRuntime);
}

ChannelRuntimeHealth* runtimeHealthForChannel(SessionManifest& manifest, std::string_view channel) noexcept {
    if (channel == "trades") return &manifest.tradesRuntime;
    if (channel == "liquidations") return &manifest.liquidationsRuntime;
    if (channel == "bookticker") return &manifest.bookTickerRuntime;
    if (channel == "depth") return &manifest.depthRuntime;
    if (channel == "mark_price") return &manifest.markPriceRuntime;
    if (channel == "index_price") return &manifest.indexPriceRuntime;
    if (channel == "funding") return &manifest.fundingRuntime;
    if (channel == "price_limit") return &manifest.priceLimitRuntime;
    return nullptr;
}

void normalizeCaptureRecordingIdentity(CaptureConfig& config) {
    if (config.symbols.empty()) return;
    config.routeSymbols.clear();
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

Status CaptureCoordinator::startExternalCapture(const CaptureConfig& config,
                                                const ExternalCaptureChannels& channels,
                                                const ExternalCaptureChannels& requestedChannels) noexcept {
    const auto sessionStatus = ensureSession(config);
    if (!isOk(sessionStatus)) return sessionStatus;

    std::lock_guard<std::mutex> lock(stateMutex_);
    manifest_.manifestSchemaVersion = kManifestSchemaVersionCurrent;
    for (const auto* required : {"bidQty", "askQty"}) {
        if (std::find(config_.bookTickerAliases.begin(), config_.bookTickerAliases.end(), required) ==
            config_.bookTickerAliases.end()) {
            config_.bookTickerAliases.emplace_back(required);
        }
    }
    auto enable = [&](bool selected, bool& manifestFlag, ChannelKind kind, std::string_view path) -> Status {
        if (!selected) return Status::Ok;
        manifestFlag = true;
        if (!path.empty() && std::find(manifest_.canonicalArtifacts.begin(), manifest_.canonicalArtifacts.end(), path) == manifest_.canonicalArtifacts.end()) {
            manifest_.canonicalArtifacts.emplace_back(path);
        }
        return jsonSink_.ensureChannelFile(kind);
    };

    Status status = enable(channels.trades, manifest_.tradesEnabled, ChannelKind::Trades, manifest_.tradesPath);
    status = aggregateStatus(status, enable(channels.liquidations, manifest_.liquidationsEnabled, ChannelKind::Liquidations, manifest_.liquidationsPath));
    status = aggregateStatus(status, enable(channels.bookTicker, manifest_.bookTickerEnabled, ChannelKind::BookTicker, manifest_.bookTickerPath));
    status = aggregateStatus(status, enable(channels.markPrice, manifest_.markPriceEnabled, ChannelKind::MarkPrice, manifest_.markPricePath));
    status = aggregateStatus(status, enable(channels.indexPrice, manifest_.indexPriceEnabled, ChannelKind::IndexPrice, manifest_.indexPricePath));
    status = aggregateStatus(status, enable(channels.funding, manifest_.fundingEnabled, ChannelKind::Funding, manifest_.fundingPath));
    status = aggregateStatus(status, enable(channels.priceLimit, manifest_.priceLimitEnabled, ChannelKind::PriceLimit, manifest_.priceLimitPath));
    if (channels.orderbook) {
        manifest_.orderbookEnabled = true;
        for (const std::string* path : {&manifest_.depthPath, &manifest_.depthSidecarPath}) {
            if (std::find(manifest_.canonicalArtifacts.begin(), manifest_.canonicalArtifacts.end(), *path) == manifest_.canonicalArtifacts.end()) {
                manifest_.canonicalArtifacts.push_back(*path);
            }
        }
        status = aggregateStatus(status, jsonSink_.ensureChannelFile(ChannelKind::DepthTape));
        status = aggregateStatus(status, jsonSink_.ensureChannelFile(ChannelKind::DepthSidecar));
    }
    const bool derivatives = !runtime::textEqualsAscii(config.market, "spot");
    const bool priceLimitVenue = derivatives &&
        (runtime::textEqualsAscii(config.exchange, "bybit") ||
         runtime::textEqualsAscii(config.exchange, "okx"));
    auto initializeHealth = [](ChannelRuntimeHealth& health, bool requested, bool enabled, bool required) {
        health.state = requested ? (enabled ? "waiting_first_row" : "unsupported") : "not_requested";
        health.required = requested && required;
        if (requested && !enabled) health.lastError = "requested channel is not supported by venue runtime";
    };
    initializeHealth(manifest_.tradesRuntime, requestedChannels.trades, channels.trades, true);
    initializeHealth(manifest_.liquidationsRuntime, requestedChannels.liquidations, channels.liquidations, false);
    initializeHealth(manifest_.bookTickerRuntime, requestedChannels.bookTicker, channels.bookTicker, true);
    initializeHealth(manifest_.depthRuntime, requestedChannels.orderbook, channels.orderbook, true);
    initializeHealth(manifest_.markPriceRuntime, requestedChannels.markPrice, channels.markPrice, false);
    initializeHealth(manifest_.indexPriceRuntime, requestedChannels.indexPrice, channels.indexPrice, false);
    initializeHealth(manifest_.fundingRuntime, requestedChannels.funding, channels.funding, derivatives);
    initializeHealth(manifest_.priceLimitRuntime, requestedChannels.priceLimit, channels.priceLimit, priceLimitVenue);
    manifest_.fundingRequiredWhenEnabled = derivatives;
    manifest_.priceLimitRequiredWhenEnabled = priceLimitVenue;
    if (!isOk(status)) lastError_ = "failed to prepare external capture storage";
    refreshRecordingManifestLocked_(internal::nowNs());
    const auto manifestStatus = writeManifestFile_();
    if (!isOk(manifestStatus)) {
        if (lastError_.empty()) lastError_ = "failed to persist external capture channel plan";
        status = aggregateStatus(status, manifestStatus);
    }
    return status;
}

void CaptureCoordinator::noteExternalRow_(ChannelRuntimeHealth& health, std::int64_t tsNs) noexcept {
    if (health.firstRowNs == 0) health.firstRowNs = tsNs;
    health.lastRowNs = tsNs;
    health.state = "live";
}

Status CaptureCoordinator::accountExternalAppend_(Status status,
                                                  ChannelRuntimeHealth& health,
                                                  std::atomic<std::uint64_t>& counter,
                                                  std::int64_t tsNs,
                                                  std::string_view channel) noexcept {
    if (isOk(status)) {
        counter.fetch_add(1u, std::memory_order_acq_rel);
        noteExternalRow_(health, tsNs);
        return status;
    }
    ++health.droppedEventCount;
    health.state = "degraded";
    if (health.lastError.empty()) {
        health.lastError = std::string{channel} + ": canonical storage append failed";
        if (!lastError_.empty()) lastError_ += " | ";
        lastError_ += health.lastError;
    }
    return status;
}

Status CaptureCoordinator::appendExternalTrade(const replay::TradeRow& row) noexcept {
    const auto status = jsonSink_.appendTradeLine(row, renderTradeJsonLine(row, config_.tradesAliases));
    return accountExternalAppend_(status, manifest_.tradesRuntime, tradesCount_, row.tsNs, "trades");
}

Status CaptureCoordinator::appendExternalLiquidation(const replay::LiquidationRow& row) noexcept {
    const auto status = jsonSink_.appendLiquidationLine(row, renderLiquidationJsonLine(row, config_.liquidationAliases));
    return accountExternalAppend_(status, manifest_.liquidationsRuntime, liquidationsCount_, row.tsNs, "liquidations");
}

Status CaptureCoordinator::appendExternalBookTicker(const replay::BookTickerRow& row) noexcept {
    const auto status = jsonSink_.appendBookTickerLine(row, renderBookTickerJsonLine(row, config_.bookTickerAliases));
    return accountExternalAppend_(status, manifest_.bookTickerRuntime, bookTickerCount_, row.tsNs, "bookticker");
}

Status CaptureCoordinator::appendExternalMarkPrice(const replay::MarkPriceRow& row) noexcept {
    const auto status = jsonSink_.appendMarkPriceLine(row, renderMarkPriceJsonLine(row));
    return accountExternalAppend_(status, manifest_.markPriceRuntime, markPriceCount_, row.tsNs, "mark_price");
}

Status CaptureCoordinator::appendExternalIndexPrice(const replay::IndexPriceRow& row) noexcept {
    const auto status = jsonSink_.appendIndexPriceLine(row, renderIndexPriceJsonLine(row));
    return accountExternalAppend_(status, manifest_.indexPriceRuntime, indexPriceCount_, row.tsNs, "index_price");
}

Status CaptureCoordinator::appendExternalFunding(const replay::FundingRow& row) noexcept {
    const auto status = jsonSink_.appendFundingLine(row, renderFundingJsonLine(row));
    return accountExternalAppend_(status, manifest_.fundingRuntime, fundingCount_, row.tsNs, "funding");
}

Status CaptureCoordinator::appendExternalPriceLimit(const replay::PriceLimitRow& row) noexcept {
    const auto status = jsonSink_.appendPriceLimitLine(row, renderPriceLimitJsonLine(row));
    return accountExternalAppend_(status, manifest_.priceLimitRuntime, priceLimitCount_, row.tsNs, "price_limit");
}

Status CaptureCoordinator::appendExternalDepth(const replay::DepthRow& row) noexcept {
    const auto status = jsonSink_.appendDepthTapeSidecarLines(row, renderDepthTapeJsonLine(row), renderDepthRleSidecarJsonLine(row));
    return accountExternalAppend_(status, manifest_.depthRuntime, depthCount_, row.tsNs, "depth");
}

void CaptureCoordinator::noteExternalChannelError(std::string_view channel, std::string_view error) noexcept {
    ChannelRuntimeHealth* health = runtimeHealthForChannel(manifest_, channel);
    if (health) {
        health->lastError.assign(error);
        if (health->state != "live") health->state = "degraded";
    }
    if (!lastError_.empty()) lastError_ += " | ";
    lastError_.append(error);
}

void CaptureCoordinator::noteExternalUnsupportedChannel(std::string_view channel, std::string_view error) noexcept {
    ChannelRuntimeHealth* health = runtimeHealthForChannel(manifest_, channel);
    if (health) {
        health->state = "unsupported";
        health->lastError.assign(error);
    }
    if (!lastError_.empty()) lastError_ += " | ";
    lastError_.append(error);
}

void CaptureCoordinator::noteExternalUnroutableEvent(std::string_view channel, std::string_view error) noexcept {
    ChannelRuntimeHealth* health = runtimeHealthForChannel(manifest_, channel);
    if (!health) return;
    ++health->unroutableEventCount;
    health->state = "degraded";
    if (health->lastError.empty()) health->lastError.assign(error);
}

void CaptureCoordinator::noteExternalChannelConnection(std::string_view channel,
                                                       bool connected,
                                                       bool reconnected) noexcept {
    ChannelRuntimeHealth* health = runtimeHealthForChannel(manifest_, channel);
    if (!health || health->state == "not_requested" || health->state == "unsupported") return;
    if (reconnected) ++health->reconnectCount;
    if (!connected) {
        health->state = "reconnecting";
    } else {
        health->state = health->firstRowNs == 0 ? "waiting_first_row" : "live";
    }
}

Status CaptureCoordinator::refreshExternalManifest() noexcept {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (!sessionOpen()) return Status::Ok;
    refreshRecordingManifestLocked_(internal::nowNs());
    return writeManifestFile_();
}

Status CaptureCoordinator::ensureSession(const CaptureConfig& config) noexcept {
    return ensureSession_(config, false);
}

Status CaptureCoordinator::ensureSession_(const CaptureConfig& config, bool allowMultiSymbol) noexcept {
    internal::ensureCxetInitialized();
    CaptureConfig normalizedConfig = config;
    normalizedConfig.outputDir = recordings::normalizeExplicitRecordingsPath(config.outputDir);
    if (const auto identityStatus = internal::validateCryptoIdentitySymbols(normalizedConfig, lastError_); !isOk(identityStatus)) {
        return identityStatus;
    }
    normalizeCaptureRecordingIdentity(normalizedConfig);

    if (const auto envStatus = internal::loadCaptureEnv(normalizedConfig, lastError_); !isOk(envStatus)) {
        return envStatus;
    }

    if (const auto validateStatus = internal::validateSupportedConfig(normalizedConfig, lastError_, allowMultiSymbol); !isOk(validateStatus)) {
        return validateStatus;
    }
    if (const auto authStatus = internal::refreshFinamAuthForConfig(
            normalizedConfig,
            internal::finamConfigNeedsAccountId(normalizedConfig),
            lastError_); !isOk(authStatus)) {
        return authStatus;
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
    manifest_.storageSymbol = recordings::recordingFolderSymbol(
        normalizedConfig.exchange,
        normalizedConfig.market,
        normalizedConfig.symbols.front());
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
    manifest_.captureContractVersion = "hftrec.runtime_event_id_rows_json.v3";
    manifest_.bookTickerRowSchema = "cxet_bookticker_event_id_v2";
    manifest_.depthRowSchema = "cxet_orderbook_tape_rle_sidecar_event_id_v2";
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
        (void)writeStartupFailureManifest_(lastError_);
        return metadataStatus;
    }
    if (const auto manifestStatus = writeManifestFile_(); !isOk(manifestStatus)) {
        lastError_ = "failed to write initial manifest.json";
        (void)writeStartupFailureManifest_(lastError_);
        return manifestStatus;
    }
    liveCacheEnabled_.store(config.liveCacheMode == LiveCacheMode::Full, std::memory_order_release);
    liveStore_.clear();
    if (const auto storageStatus = jsonSink_.open(sessionDir_); !isOk(storageStatus)) {
        lastError_ = "failed to open JSON session storage";
        (void)writeStartupFailureManifest_(lastError_);
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

    const bool degradedRuntime = runtimeHealthDegraded(manifest_);
    if (degradedRuntime) {
        manifest_.sessionHealth = SessionHealth::Degraded;
        manifest_.exactReplayEligible = false;
        if (!manifest_.warningSummary.empty()) manifest_.warningSummary += " | ";
        manifest_.warningSummary += "required runtime channel was missing, reconnected, or degraded";
    }
    manifest_.sessionStatus = degradedRuntime ? "complete_degraded" : "complete";

    if (const auto supportStatus = writeSupportArtifacts(); !isOk(supportStatus)) {
        lastError_ = "failed to write support artifacts";
        return supportStatus;
    }

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

Status CaptureCoordinator::writeStartupFailureManifest_(std::string_view reason) noexcept {
    if (sessionDir_.empty()) return Status::InvalidArgument;

    manifest_.endedAtNs = internal::nowNs();
    if (manifest_.startedAtNs > 0 && manifest_.endedAtNs >= manifest_.startedAtNs) {
        manifest_.actualDurationSec = (manifest_.endedAtNs - manifest_.startedAtNs) / 1000000000LL;
    }
    manifest_.sessionStatus = "failed_startup";
    manifest_.sessionHealth = SessionHealth::Degraded;
    manifest_.exactReplayEligible = false;
    manifest_.warningSummary = reason.empty()
        ? std::string{"startup failed before recorder manifest finalized"}
        : std::string{reason};
    manifest_.structurallyLoadable = false;
    manifest_.structuralBlockers.clear();
    manifest_.structuralBlockers.push_back(manifest_.warningSummary);
    manifest_.canonicalArtifacts.clear();
    manifest_.canonicalArtifacts.push_back("manifest.json");
    if (!manifest_.instrumentMetadataPath.empty()) {
        std::error_code ec;
        if (std::filesystem::exists(sessionDir_ / manifest_.instrumentMetadataPath, ec) && !ec) {
            manifest_.canonicalArtifacts.push_back(manifest_.instrumentMetadataPath);
        }
    }
    return writeManifestFile_();
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

Status CaptureCoordinator::refreshInstrumentMetadataFromExchangeInfo() noexcept {
    if (sessionDir_.empty() || config_.symbols.empty()) return Status::InvalidArgument;
    auto metadata = corpus::makeInstrumentMetadata(config_.exchange, config_.market, config_.symbols.front());
    const bool enriched = internal::enrichInstrumentMetadataFromExchangeInfo(config_, metadata);
    const bool hasPriceBasis = metadata.priceBasisQtyE8.has_value() && *metadata.priceBasisQtyE8 > 0;

    const auto metadataPath = sessionDir_ / manifest_.instrumentMetadataPath;
    const auto tempPath = sessionDir_ / (manifest_.instrumentMetadataPath + ".tmp");
    if (const auto writeStatus = writeFileFully(tempPath, corpus::renderInstrumentMetadataJson(metadata)); !isOk(writeStatus)) {
        lastError_ = "failed to write refreshed instrument metadata sidecar";
        return writeStatus;
    }
    if (const auto replaceStatus = replaceFilePreservingPrevious(tempPath, metadataPath); !isOk(replaceStatus)) {
        lastError_ = "failed to replace refreshed instrument metadata sidecar";
        return replaceStatus;
    }

    instrumentMetadataReady_ = enriched && hasPriceBasis;
    if (!instrumentMetadataReady_) {
        lastError_ = "instrument metadata missing price_basis_qty_e8";
        if (metadata.metadataWarning.has_value() && !metadata.metadataWarning->empty()) {
            lastError_ += " ";
            lastError_ += *metadata.metadataWarning;
        }
    }
    return Status::Ok;
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


