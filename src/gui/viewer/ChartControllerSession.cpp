#include "gui/viewer/ChartController.hpp"

#include <QFileInfo>
#include <algorithm>
#include <filesystem>
#include <string>
#include <utility>
#include <chrono>

#include "core/metrics/Metrics.hpp"
#include "core/replay/CxetReplaySessionLoader.hpp"

namespace hftrec::gui::viewer {

namespace {

std::string stripFileUrl(const QString& path) {
    QString p = path;
    if (p.startsWith(QStringLiteral("file:///"))) p.remove(0, 8);
    else if (p.startsWith(QStringLiteral("file://"))) p.remove(0, 7);
    return p.toStdString();
}

QString replayFailureText(const hftrec::replay::SessionReplay& replay, Status status, QStringView prefix) {
    if (!replay.errorDetail().empty()) {
        return QStringLiteral("%1: %2")
            .arg(prefix, QString::fromStdString(std::string{replay.errorDetail()}));
    }
    return QStringLiteral("%1: %2").arg(prefix, QString::fromUtf8(statusToString(status).data()));
}

QString liveModeLabel(int intervalMs) {
    if (intervalMs <= 16) return QStringLiteral("tick");
    return QStringLiteral("%1 ms").arg(intervalMs);
}

bool hasRows(const LiveDataBatch& batch) noexcept {
    return !batch.trades.empty() || !batch.liquidations.empty() || !batch.bookTickers.empty() || !batch.depths.empty();
}

QString recordedSourceIdFromPath(const QString& dir) {
    const auto sessionName = QFileInfo(dir).fileName();
    return sessionName.isEmpty() ? QStringLiteral("recorded") : QStringLiteral("recorded:%1").arg(sessionName);
}

}  // namespace

void ChartController::clearLiveDataCache_() noexcept {
    liveDataCache_.stableRows = LiveDataBatch{};
    liveDataCache_.overlayRows = LiveDataBatch{};
    liveDataCache_.hasRenderRange = false;
    liveDataCache_.renderTsMin = 0;
    liveDataCache_.renderTsMax = 0;
    liveOverlayState_ = LiveDataBatch{};
    liveInitialViewportApplied_ = false;
    ++liveDataCache_.version;
    liveDataStats_ = LiveDataStats{};
    liveWindowTsMin_ = 0;
    liveWindowTsMax_ = 0;
    liveWindowVersion_ = 0;
}

void ChartController::startLiveData_(const std::filesystem::path& sessionDir) {
    refreshProviderFromRegistry_();
    liveOrderbookHealthy_ = isOk(replay_.status());
    liveFollowEdge_ = false;
    liveDataBatchSeq_ = 0;
    clearLiveDataCache_();
    if (liveDataProvider_ != nullptr) {
        const auto sourceId = liveProviderFromRegistry_ ? currentSourceId_.toStdString() : std::string{};
        liveDataProvider_->start(LiveDataProviderConfig{sessionDir, {}, sourceId});
    }
}

void ChartController::stopLiveData_() noexcept {
    if (liveDataProvider_ != nullptr) liveDataProvider_->stop();
    liveOrderbookHealthy_ = true;
    clearLiveDataCache_();
}

void ChartController::markUserViewportControl_() noexcept {
    liveFollowEdge_ = false;
}

void ChartController::pollLiveData_() {
    if (!active_) return;
    if (currentSourceKind_ != QStringLiteral("live")) return;
    refreshProviderFromRegistry_();
    if (liveDataProvider_ == nullptr) return;

    if (sessionDir_.isEmpty() && !LiveDataRegistry::instance().hasSource(currentSourceId_.toStdString())) {
        stopLiveData_();
        replay_.reset();
        loaded_ = false;
        currentSourceId_.clear();
        liveProviderSourceId_.clear();
        tsMin_ = tsMax_ = priceMinE8_ = priceMaxE8_ = 0;
        currentBookTickerIndex_ = -1;
        selectionActive_ = false;
        selectionSummaryText_.clear();
        const auto nextStatus = QStringLiteral("Live source ended");
        if (statusText_ != nextStatus) {
            statusText_ = nextStatus;
            emit statusChanged();
        }
        emit sessionChanged();
        emit viewportChanged();
        emit selectionChanged();
        emit liveDataChanged();
        return;
    }

    if (!liveProviderFromRegistry_ && sessionDir_.isEmpty()) return;

    std::filesystem::path sessionPath{};
    if (!liveProviderFromRegistry_) {
        sessionPath = std::filesystem::path(stripFileUrl(sessionDir_));
        std::error_code ec;
        if (!std::filesystem::exists(sessionPath, ec) || ec) return;
    }

    const auto oldTsMin = tsMin_;
    const auto oldTsMax = tsMax_;
    const auto oldPriceMin = priceMinE8_;
    const auto oldPriceMax = priceMaxE8_;
    const auto oldLoaded = loaded_;
    const bool oldHasTrades = hasTrades();
    const bool oldHasLiquidations = hasLiquidations();
    const bool oldHasBookTicker = hasBookTicker();
    const bool oldHasOrderbook = hasOrderbook();
    const auto oldTradeCount = replay_.trades().size();
    const auto oldLiquidationCount = replay_.liquidations().size();
    const auto oldDepthCount = replay_.depths().size();
    const auto oldBookTickerCount = replay_.bookTickers().size();

    bool reloadedSession = false;
    QString failureText{};
    const auto pollStart = std::chrono::steady_clock::now();
    auto pollResult = liveDataProvider_->pollHot(liveDataBatchSeq_ + 1u);
    hftrec::metrics::recordLivePoll(static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - pollStart).count()));
    auto nextLiveBatch = std::move(pollResult.batch);
    if (!isOk(pollResult.failureStatus) && !pollResult.failureDetail.empty()) {
        failureText = QStringLiteral("%1: %2")
            .arg(QString::fromStdString(pollResult.failureDetail),
                 QString::fromUtf8(statusToString(pollResult.failureStatus).data()));
    }

    if (pollResult.reloadRequired && !liveProviderFromRegistry_) {
        const auto st = replay_.open(sessionPath);
        if (!isOk(st)) {
            const auto nextStatus = replayFailureText(replay_, st, QStringLiteral("Live reload failed"));
            if (statusText_ != nextStatus) {
                statusText_ = nextStatus;
                emit statusChanged();
            }
            return;
        }
        startLiveData_(sessionPath);
        reloadedSession = true;
        pollResult.appendedRows = true;
        failureText.clear();
    }

    if (!failureText.isEmpty()) {
        if (statusText_ != failureText) {
            statusText_ = failureText;
            emit statusChanged();
        }
        return;
    }

    if (!pollResult.appendedRows) return;

    const bool hasLiveDataBatch = hasRows(nextLiveBatch);
    if (hasLiveDataBatch) {
        liveDataBatchSeq_ = nextLiveBatch.id;
        if (liveProviderFromRegistry_) {
            if (!appendOverlayBatch_(nextLiveBatch, &failureText)) {
                liveOverlayState_ = LiveDataBatch{};
                liveDataCache_.overlayRows = LiveDataBatch{};
                ++liveDataCache_.version;
                if (statusText_ != failureText) {
                    statusText_ = failureText;
                    emit statusChanged();
                }
                emit liveDataChanged();
                return;
            }
            liveDataCache_.overlayRows = liveOverlayState_;
            liveDataCache_.overlayRows.id = liveDataCache_.version + 1u;
            ++liveDataCache_.version;
        } else {
            if (!appendOverlayBatch_(nextLiveBatch, &failureText)) {
                liveOverlayState_ = LiveDataBatch{};
                liveDataCache_.overlayRows = LiveDataBatch{};
                ++liveDataCache_.version;
                if (statusText_ != failureText) {
                    statusText_ = failureText;
                    emit statusChanged();
                }
                emit liveDataChanged();
                return;
            }
            liveDataCache_.overlayRows = liveOverlayState_;
            liveDataCache_.overlayRows.id = liveDataCache_.version + 1u;
            ++liveDataCache_.version;
        }
    }

    liveDataStats_ = liveDataProvider_->stats();
    liveOrderbookHealthy_ = isOk(replay_.status());
    refreshLoadedStateFromSources_();
    initializeViewportFromLiveDataOnce_();
    currentBookTickerIndex_ = -1;

    const auto nextStatus = isOk(replay_.status())
        ? QStringLiteral("Live %1 | trades=%2 liq=%3 depth=%4 bookticker=%5")
              .arg(liveModeLabel(liveUpdateIntervalMs_))
              .arg(replay_.trades().size() + liveDataStats_.tradesTotal)
              .arg(replay_.liquidations().size() + liveDataStats_.liquidationsTotal)
              .arg(replay_.depths().size() + liveDataStats_.depthsTotal)
              .arg(replay_.bookTickers().size() + liveDataStats_.bookTickersTotal)
        : replayFailureText(replay_, replay_.status(), QStringLiteral("Live integrity failed"));
    const bool viewportChangedFlag = (tsMin_ != oldTsMin) || (tsMax_ != oldTsMax)
        || (priceMinE8_ != oldPriceMin) || (priceMaxE8_ != oldPriceMax);
    const bool sessionChangedFlag = (loaded_ != oldLoaded)
        || (hasTrades() != oldHasTrades)
        || (hasLiquidations() != oldHasLiquidations)
        || (hasBookTicker() != oldHasBookTicker)
        || (hasOrderbook() != oldHasOrderbook)
        || (replay_.trades().size() != oldTradeCount)
        || (replay_.liquidations().size() != oldLiquidationCount)
        || (replay_.depths().size() != oldDepthCount)
        || (replay_.bookTickers().size() != oldBookTickerCount);

    if (reloadedSession || sessionChangedFlag) emit sessionChanged();
    else if (hasLiveDataBatch) emit liveDataChanged();
    if (viewportChangedFlag) emit viewportChanged();
    if (statusText_ != nextStatus) {
        statusText_ = nextStatus;
        emit statusChanged();
    }
}

bool ChartController::activateLiveSource(const QString& sourceId, const QString& sessionPath) {
    if (sourceId.trimmed().isEmpty()) {
        activateLiveOnlyMode();
        return false;
    }

    stopLiveData_();
    replay_.reset();
    loaded_ = false;
    sessionDir_ = sessionPath;
    currentSourceId_ = sourceId.trimmed();
    currentSourceKind_ = QStringLiteral("live");
    tsMin_ = tsMax_ = priceMinE8_ = priceMaxE8_ = 0;
    currentBookTickerIndex_ = -1;
    selectionActive_ = false;
    selectionSummaryText_.clear();
    if (!verticalMarkers_.empty()) {
        verticalMarkers_.clear();
        emit markersChanged();
    }

    const auto path = std::filesystem::path(stripFileUrl(sessionPath));
    if (!sessionPath.trimmed().isEmpty()) {
        const auto st = replay_.open(path);
        if (isOk(st)) {
            loaded_ = !replay_.buckets().empty()
                || !replay_.trades().empty()
                || !replay_.liquidations().empty()
                || !replay_.bookTickers().empty()
                || !replay_.depths().empty()
                || !replay_.book().bids().empty()
                || !replay_.book().asks().empty();
            if (loaded_) computeInitialViewport_();
        }
    }

    statusText_ = QStringLiteral("Live source selected");
    startLiveData_(path);
    emit sessionChanged();
    emit statusChanged();
    emit viewportChanged();
    emit selectionChanged();
    return true;
}

void ChartController::activateLiveOnlyMode() {
    stopLiveData_();
    replay_.reset();
    loaded_ = false;
    sessionDir_.clear();
    currentSourceId_.clear();
    liveProviderSourceId_.clear();
    currentSourceKind_ = QStringLiteral("live");
    tsMin_ = tsMax_ = priceMinE8_ = priceMaxE8_ = 0;
    currentBookTickerIndex_ = -1;
    selectionActive_ = false;
    selectionSummaryText_.clear();
    if (!verticalMarkers_.empty()) {
        verticalMarkers_.clear();
        emit markersChanged();
    }
    statusText_ = QStringLiteral("Choose a live source.");
    emit sessionChanged();
    emit statusChanged();
    emit viewportChanged();
    emit selectionChanged();
}

void ChartController::resetSession() {
    stopLiveData_();
    replay_.reset();
    loaded_ = false;
    sessionDir_.clear();
    currentSourceId_.clear();
    liveProviderSourceId_.clear();
    currentSourceKind_.clear();
    tsMin_ = tsMax_ = priceMinE8_ = priceMaxE8_ = 0;
    currentBookTickerIndex_ = -1;
    selectionActive_ = false;
    selectionSummaryText_.clear();
    if (!verticalMarkers_.empty()) {
        verticalMarkers_.clear();
        emit markersChanged();
    }
    statusText_ = QStringLiteral("Choose a source.");
    emit sessionChanged();
    emit statusChanged();
    emit viewportChanged();
    emit selectionChanged();
}

bool ChartController::addTradesFile(const QString& path) {
    if (path.trimmed().isEmpty()) {
        statusText_ = QStringLiteral("No path. Enter a trades.jsonl path first.");
        emit statusChanged();
        return false;
    }

    const auto st = replay_.addTradesFile(stripFileUrl(path));
    if (!isOk(st)) {
        statusText_ = replayFailureText(replay_, st, QStringLiteral("trades load failed"));
        emit statusChanged();
        return false;
    }

    statusText_ = QStringLiteral("+ trades (now %1 rows)").arg(replay_.trades().size());
    emit statusChanged();
    return true;
}

bool ChartController::addLiquidationsFile(const QString& path) {
    if (path.trimmed().isEmpty()) {
        statusText_ = QStringLiteral("No path. Enter a liquidations.jsonl path first.");
        emit statusChanged();
        return false;
    }

    const auto st = replay_.addLiquidationsFile(stripFileUrl(path));
    if (!isOk(st)) {
        statusText_ = replayFailureText(replay_, st, QStringLiteral("liquidations load failed"));
        emit statusChanged();
        return false;
    }

    statusText_ = QStringLiteral("+ liquidations (now %1 rows)").arg(replay_.liquidations().size());
    emit statusChanged();
    return true;
}
bool ChartController::addBookTickerFile(const QString& path) {
    if (path.trimmed().isEmpty()) {
        statusText_ = QStringLiteral("No path. Enter a bookticker.jsonl path first.");
        emit statusChanged();
        return false;
    }

    const auto st = replay_.addBookTickerFile(stripFileUrl(path));
    if (!isOk(st)) {
        statusText_ = replayFailureText(replay_, st, QStringLiteral("bookticker load failed"));
        emit statusChanged();
        return false;
    }

    statusText_ = QStringLiteral("+ bookticker (now %1 rows)").arg(replay_.bookTickers().size());
    emit statusChanged();
    return true;
}

bool ChartController::addDepthFile(const QString& path) {
    if (path.trimmed().isEmpty()) {
        statusText_ = QStringLiteral("No path. Enter a depth.jsonl path first.");
        emit statusChanged();
        return false;
    }

    const auto st = replay_.addDepthFile(stripFileUrl(path));
    if (!isOk(st)) {
        statusText_ = replayFailureText(replay_, st, QStringLiteral("depth load failed"));
        emit statusChanged();
        return false;
    }

    statusText_ = QStringLiteral("+ depth (now %1 rows)").arg(replay_.depths().size());
    emit statusChanged();
    return true;
}

bool ChartController::addSnapshotFile(const QString& path) {
    if (path.trimmed().isEmpty()) {
        statusText_ = QStringLiteral("No path. Enter a snapshot_*.json path first.");
        emit statusChanged();
        return false;
    }

    const auto st = replay_.addSnapshotFile(stripFileUrl(path));
    if (!isOk(st)) {
        statusText_ = replayFailureText(replay_, st, QStringLiteral("snapshot load failed"));
        emit statusChanged();
        return false;
    }

    statusText_ = QStringLiteral("+ snapshot");
    emit statusChanged();
    return true;
}

void ChartController::finalizeFiles() {
    stopLiveData_();
    clearSelection();
    replay_.finalize();
    if (!isOk(replay_.status())) {
        loaded_ = false;
        currentBookTickerIndex_ = -1;
        statusText_ = replayFailureText(replay_, replay_.status(), QStringLiteral("Finalize failed"));
        emit sessionChanged();
        emit statusChanged();
        emit viewportChanged();
        return;
    }

    loaded_ = !replay_.buckets().empty()
        || !replay_.trades().empty()
                || !replay_.liquidations().empty()
                || !replay_.bookTickers().empty()
        || !replay_.depths().empty()
        || !replay_.book().bids().empty()
        || !replay_.book().asks().empty();
    if (loaded_) computeInitialViewport_();
    currentBookTickerIndex_ = -1;

    statusText_ = QStringLiteral("Finalized.");
    emit sessionChanged();
    emit statusChanged();
    emit viewportChanged();
}

bool ChartController::loadSession(const QString& dir) {
    stopLiveData_();
    sessionDir_ = dir;
    currentSourceId_ = recordedSourceIdFromPath(dir);
    liveProviderSourceId_.clear();
    currentSourceKind_ = QStringLiteral("recorded");
    loaded_ = false;
    replay_ = hftrec::replay::SessionReplay{};
    clearSelection();
    if (!verticalMarkers_.empty()) {
        verticalMarkers_.clear();
        emit markersChanged();
    }

    const auto path = std::filesystem::path(stripFileUrl(dir));
    std::string cxetReplayError;
#if HFTREC_WITH_CXET_REPLAY
    const hftrec::replay::CxetReplaySessionLoader cxetLoader{};
    const auto st = cxetLoader.loadRenderOnce(path, replay_, cxetReplayError);
#else
    const auto st = replay_.open(path);
#endif
    if (!isOk(st)) {
        statusText_ = cxetReplayError.empty()
            ? replayFailureText(replay_, st, QStringLiteral("Failed to load session"))
            : QStringLiteral("Failed to load session: %1").arg(QString::fromStdString(cxetReplayError));
        emit sessionChanged();
        emit statusChanged();
        return false;
    }

    loaded_ = !replay_.buckets().empty()
        || !replay_.trades().empty()
                || !replay_.liquidations().empty()
                || !replay_.bookTickers().empty()
        || !replay_.depths().empty()
        || !replay_.book().bids().empty()
        || !replay_.book().asks().empty();
    currentBookTickerIndex_ = -1;
    statusText_ = QStringLiteral("Loaded trades=%1 liq=%2 depth=%3 bookticker=%4")
                       .arg(replay_.trades().size())
                      .arg(replay_.liquidations().size())
                      .arg(replay_.depths().size())
                      .arg(replay_.bookTickers().size());
    if (!replay_.errorDetail().empty()) {
        statusText_ += QStringLiteral(" | %1").arg(QString::fromStdString(std::string{replay_.errorDetail()}));
    }
    if (loaded_) {
        computeInitialViewport_();
        applyRecordedRenderWindowViewport_();
    }
    emit sessionChanged();
    emit statusChanged();
    emit viewportChanged();
    return true;
}

}  // namespace hftrec::gui::viewer










