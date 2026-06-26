#include "gui/viewer/ChartController.hpp"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <chrono>

#include "core/metrics/Metrics.hpp"
#include "core/replay/CxetReplaySessionLoader.hpp"

namespace hftrec::gui::viewer {

namespace {

std::string stripFileUrl(const QString& path) {
    const QString p = path.trimmed();
    if (p.startsWith(QStringLiteral("file://"))) {
        const QUrl url(p);
        if (url.isLocalFile()) return url.toLocalFile().toStdString();
    }
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
    return !batch.trades.empty()
        || !batch.liquidations.empty()
        || !batch.bookTickers.empty()
        || !batch.markPrices.empty()
        || !batch.indexPrices.empty()
        || !batch.fundings.empty()
        || !batch.priceLimits.empty()
        || !batch.depths.empty();
}

QString recordedSourceIdFromPath(const QString& dir) {
    const auto sessionName = QFileInfo(dir).fileName();
    return sessionName.isEmpty() ? QStringLiteral("recorded") : QStringLiteral("recorded:%1").arg(sessionName);
}

struct SourceIdentity {
    QString exchange{};
    QString market{};
    QString symbol{};
};

QJsonObject readSessionManifestObject(const std::filesystem::path& sessionPath);

SourceIdentity sourceIdentityFromManifest(const QJsonObject& manifest) {
    SourceIdentity out{};
    const QJsonObject identity = manifest.value(QStringLiteral("identity")).toObject();
    out.exchange = identity.value(QStringLiteral("exchange")).toString();
    out.market = identity.value(QStringLiteral("market")).toString();
    const QJsonArray symbols = identity.value(QStringLiteral("symbols")).toArray();
    if (!symbols.isEmpty()) out.symbol = symbols.at(0).toString();
    if (out.exchange.isEmpty()) out.exchange = manifest.value(QStringLiteral("exchange")).toString();
    if (out.market.isEmpty()) out.market = manifest.value(QStringLiteral("market")).toString();
    if (out.symbol.isEmpty()) out.symbol = manifest.value(QStringLiteral("symbol")).toString();
    return out;
}

SourceIdentity sourceIdentityFromSessionPath(const std::filesystem::path& path) {
    return sourceIdentityFromManifest(readSessionManifestObject(path));
}

SourceIdentity sourceIdentityFromLiveRegistry(const QString& sourceId) {
    const std::string id = sourceId.toStdString();
    for (const auto& source : LiveDataRegistry::instance().snapshotSources()) {
        if (source.viewerSourceId != id) continue;
        return SourceIdentity{
            QString::fromStdString(source.exchange),
            QString::fromStdString(source.market),
            QString::fromStdString(source.symbol),
        };
    }
    return {};
}

std::filesystem::path existingPathOrEmpty(const std::filesystem::path& path) noexcept {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec) && !ec ? path : std::filesystem::path{};
}

QJsonObject readSessionManifestObject(const std::filesystem::path& sessionPath) {
    QFile file(QString::fromStdString((sessionPath / "manifest.json").string()));
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    return doc.isObject() ? doc.object() : QJsonObject{};
}

std::filesystem::path sessionChannelPath(const std::filesystem::path& sessionPath,
                                         const QJsonObject& manifest,
                                         QStringView channel,
                                         const char* fallbackFileName) {
    const QJsonObject channels = manifest.value(QStringLiteral("channels")).toObject();
    const QString manifestPath = channels.value(channel.toString()).toObject().value(QStringLiteral("path")).toString();
    if (!manifestPath.isEmpty()) {
        if (const auto path = existingPathOrEmpty(sessionPath / manifestPath.toStdString()); !path.empty()) return path;
    }
    if (QString::fromUtf8(fallbackFileName) == QStringLiteral("depth.jsonl")) {
        if (const auto path = existingPathOrEmpty(sessionPath / "jsonl" / "depth_tape.jsonl"); !path.empty()) return path;
        if (const auto path = existingPathOrEmpty(sessionPath / "depth_tape.jsonl"); !path.empty()) return path;
    }
    if (const auto path = existingPathOrEmpty(sessionPath / "jsonl" / fallbackFileName); !path.empty()) return path;
    return existingPathOrEmpty(sessionPath / fallbackFileName);
}

std::size_t channelDeclaredCount(const QJsonObject& manifest, QStringView channel) {
    const QJsonObject channels = manifest.value(QStringLiteral("channels")).toObject();
    const qint64 count = channels.value(channel.toString()).toObject().value(QStringLiteral("declared_event_count")).toInteger();
    return count > 0 ? static_cast<std::size_t>(count) : 0u;
}

std::size_t replayRowCount(const hftrec::replay::SessionReplay& replay) noexcept {
    return replay.trades().size()
        + replay.liquidations().size()
        + replay.candles().size()
        + replay.candles2().size()
        + replay.depths().size()
        + replay.bookTickers().size()
        + replay.markPrices().size()
        + replay.indexPrices().size()
        + replay.fundings().size()
        + replay.priceLimits().size();
}

QString formatLoadNsAsMs(std::uint64_t ns) {
    const std::uint64_t wholeMs = ns / 1000000ull;
    const std::uint64_t hundredths = (ns % 1000000ull) / 10000ull;
    return QStringLiteral("%1.%2ms")
        .arg(static_cast<qulonglong>(wholeMs))
        .arg(static_cast<int>(hundredths), 2, 10, QLatin1Char('0'));
}

bool mergeStatus(Status& aggregate, Status next) noexcept {
    if (isOk(next)) return true;
    aggregate = next;
    return false;
}

struct CachedManifestSummary {
    std::filesystem::file_time_type writeTime{};
    std::uintmax_t size{0};
    bool valid{false};
    bool selectable{false};
    QString type{};
    QString pnlText{};
    QString rightText{};
};

std::unordered_map<std::string, CachedManifestSummary>& resultManifestCache() {
    static std::unordered_map<std::string, CachedManifestSummary> cache;
    return cache;
}

CachedManifestSummary readManifestSummary(const std::filesystem::path& manifestPath) {
    std::error_code ec;
    const auto writeTime = std::filesystem::last_write_time(manifestPath, ec);
    if (ec) return {};
    const auto size = std::filesystem::file_size(manifestPath, ec);
    if (ec) return {};

    auto& cache = resultManifestCache();
    const std::string key = manifestPath.string();
    const auto cached = cache.find(key);
    if (cached != cache.end() && cached->second.writeTime == writeTime && cached->second.size == size) return cached->second;

    CachedManifestSummary summary{};
    summary.writeTime = writeTime;
    summary.size = size;

    QFile file(QString::fromStdString(manifestPath.string()));
    if (!file.open(QIODevice::ReadOnly)) {
        return summary;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) {
        return summary;
    }

    const QJsonObject root = doc.object();
    const QString type = root.value(QStringLiteral("type")).toString();
    summary.type = type;
    summary.valid = type == QStringLiteral("run.result.v2") || type == QStringLiteral("sweep.result.v1");
    summary.selectable = type == QStringLiteral("run.result.v2");
    if (type == QStringLiteral("sweep.result.v1")) {
        const qint64 points = root.value(QStringLiteral("points_evaluated")).toInteger();
        summary.rightText = points > 0 ? QStringLiteral("sweep %1 pts").arg(points) : QStringLiteral("sweep");
    }
    if (!summary.valid) {
        return summary;
    }

    const QJsonObject summaryObject = root.value(QStringLiteral("summary")).toObject();
    const qint64 initial = summaryObject.value(QStringLiteral("initial_balance_e8")).toInteger();
    const qint64 pnl = summaryObject.value(QStringLiteral("total_pnl_e8")).toInteger();
    if (initial <= 0) {
        cache[key] = summary;
        return summary;
    }
    const qint64 bps = (pnl * 10000) / initial;
    const QString sign = bps > 0 ? QStringLiteral("+") : (bps < 0 ? QStringLiteral("-") : QString{});
    const qint64 absBps = bps < 0 ? -bps : bps;
    summary.pnlText = QStringLiteral("%1%2.%3%").arg(sign, QString::number(absBps / 100), QString::number(absBps % 100).rightJustified(2, QLatin1Char('0')));
    const qint64 maxDrawdown = summaryObject.value(QStringLiteral("max_drawdown_e8")).toInteger();
    const qint64 ddBps = maxDrawdown > 0 ? (maxDrawdown * 10000) / initial : 0;
    const qint64 fills = summaryObject.value(QStringLiteral("fills")).toInteger();
    summary.rightText = QStringLiteral("%1 | DD %2.%3% | F %4")
        .arg(summary.pnlText,
             QString::number(ddBps / 100),
             QString::number(ddBps % 100).rightJustified(2, QLatin1Char('0')),
             QString::number(fills));
    cache[key] = summary;
    return summary;
}

void appendResultDirsDirect(const QString& sessionPath,
                            QStringView prefix,
                            const std::filesystem::path& dir,
                            QVariantList& rows) {
    std::error_code ec;
    if (!std::filesystem::exists(dir, ec) || ec || !std::filesystem::is_directory(dir, ec) || ec) return;

    std::vector<std::filesystem::path> resultDirs;
    for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_directory(ec) || ec) continue;
        const auto path = entry.path();
        if (!std::filesystem::is_regular_file(path / "manifest.json", ec) || ec) continue;
        resultDirs.push_back(path);
    }
    std::sort(resultDirs.begin(), resultDirs.end());

    for (const auto& path : resultDirs) {
        const auto manifestPath = path / "manifest.json";
        const CachedManifestSummary summary = readManifestSummary(manifestPath);
        if (!summary.valid) continue;
        QVariantMap row;
        row.insert(QStringLiteral("sessionPath"), sessionPath);
        row.insert(QStringLiteral("path"), QString::fromStdString(path.string()));
        row.insert(QStringLiteral("label"), prefix.toString() + QStringLiteral(" ") + QString::fromStdString(path.filename().string()));
        row.insert(QStringLiteral("pnlText"), summary.pnlText);
        row.insert(QStringLiteral("rightText"), summary.rightText.isEmpty() ? summary.pnlText : summary.rightText);
        row.insert(QStringLiteral("type"), summary.type);
        row.insert(QStringLiteral("selectable"), summary.selectable);
        rows.push_back(row);
    }
}

void appendBacktestResultRowsDirect(const QString& sessionPath,
                                    QStringView prefix,
                                    QVariantList& rows) {
    if (sessionPath.trimmed().isEmpty()) return;
    const std::filesystem::path dir = std::filesystem::path(stripFileUrl(sessionPath)) / "backtests";
    appendResultDirsDirect(sessionPath, prefix, dir, rows);
    appendResultDirsDirect(sessionPath, prefix, dir / "sweeps", rows);
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

void ChartController::clearStrategyOverlay_() noexcept {
    const bool changed = !selectedBacktestResult_.isEmpty() ||
                         !strategyOverlay_.empty() ||
                         !strategyIndicator_.empty() ||
                         !rateLimitUsage_.empty();
    selectedBacktestResult_.clear();
    strategyOverlay_ = StrategyOverlayData{};
    strategyIndicator_ = StrategyIndicatorData{};
    rateLimitUsage_ = RateLimitUsageData{};
    if (!changed) return;
    emit backtestResultChanged();
    emit markersChanged();
    emit viewportChanged();
}

void ChartController::refreshBacktestResults(const QString& primarySessionPath, const QString& secondarySessionPath) {
    QVariantList rows;
    appendBacktestResultRowsDirect(primarySessionPath, QStringLiteral("A"), rows);
    appendBacktestResultRowsDirect(secondarySessionPath, QStringLiteral("B"), rows);

    QVariantList deduped;
    QStringList seen;
    for (const QVariant& rowValue : rows) {
        const QVariantMap row = rowValue.toMap();
        const QString path = row.value(QStringLiteral("path")).toString();
        if (path.isEmpty() || seen.contains(path)) continue;
        seen.push_back(path);
        deduped.push_back(row);
    }

    backtestResults_ = deduped;
    bool stillSelected = selectedBacktestResult_.isEmpty();
    for (const QVariant& rowValue : backtestResults_) {
        if (rowValue.toMap().value(QStringLiteral("path")).toString() == selectedBacktestResult_) {
            stillSelected = true;
            break;
        }
    }
    if (!stillSelected) clearStrategyOverlay_();
    emit backtestResultsChanged();
}

bool ChartController::selectBacktestResult(const QString& resultPath) {
    const QString pathText = resultPath.trimmed();
    if (pathText.isEmpty()) {
        clearBacktestResult();
        return true;
    }

    const std::filesystem::path resultDir(stripFileUrl(pathText));
    StrategyOverlayData next{};
    StrategyIndicatorData nextIndicator{};
    RateLimitUsageData nextRateLimitUsage{};
    std::string error;
    if (!loadStrategyOverlayFromResult(resultDir, latestRenderableTsNs_(), next, error)) {
        statusText_ = QStringLiteral("Backtest load failed: ") + QString::fromStdString(error);
        emit statusChanged();
        return false;
    }
    if (!loadStrategyIndicatorFromResult(resultDir, nextIndicator, error)) {
        statusText_ = QStringLiteral("Backtest indicator load failed: ") + QString::fromStdString(error);
        emit statusChanged();
        return false;
    }
    if (!loadRateLimitUsageFromResult(resultDir, nextRateLimitUsage, error)) {
        statusText_ = QStringLiteral("Backtest rate-limit load failed: ") + QString::fromStdString(error);
        emit statusChanged();
        return false;
    }

    const QString orderSegmentCount = QString::number(static_cast<qulonglong>(next.orderSegments.size()));
    const QString fillMarkerCount = QString::number(static_cast<qulonglong>(next.fillMarkers.size()));
    selectedBacktestResult_ = pathText;
    strategyOverlay_ = std::move(next);
    strategyIndicator_ = std::move(nextIndicator);
    rateLimitUsage_ = std::move(nextRateLimitUsage);
    statusText_ = QStringLiteral("Backtest loaded: %1 | orders %2 fills %3")
        .arg(QString::fromStdString(resultDir.filename().string()))
        .arg(orderSegmentCount)
        .arg(fillMarkerCount);
    emit backtestResultChanged();
    emit statusChanged();
    emit markersChanged();
    emit viewportChanged();
    return true;
}

void ChartController::clearBacktestResult() {
    clearStrategyOverlay_();
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
    const bool oldHasMarkPrice = hasMarkPrice();
    const bool oldHasIndexPrice = hasIndexPrice();
    const bool oldHasFunding = hasFunding();
    const bool oldHasPriceLimit = hasPriceLimit();
    const auto oldTradeCount = replay_.trades().size();
    const auto oldLiquidationCount = replay_.liquidations().size();
    const auto oldDepthCount = replay_.depths().size();
    const auto oldBookTickerCount = replay_.bookTickers().size();
    const auto oldMarkPriceCount = replay_.markPrices().size();
    const auto oldIndexPriceCount = replay_.indexPrices().size();
    const auto oldFundingCount = replay_.fundings().size();
    const auto oldPriceLimitCount = replay_.priceLimits().size();

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
        || (hasMarkPrice() != oldHasMarkPrice)
        || (hasIndexPrice() != oldHasIndexPrice)
        || (hasFunding() != oldHasFunding)
        || (hasPriceLimit() != oldHasPriceLimit)
        || (replay_.trades().size() != oldTradeCount)
        || (replay_.liquidations().size() != oldLiquidationCount)
        || (replay_.depths().size() != oldDepthCount)
        || (replay_.bookTickers().size() != oldBookTickerCount)
        || (replay_.markPrices().size() != oldMarkPriceCount)
        || (replay_.indexPrices().size() != oldIndexPriceCount)
        || (replay_.fundings().size() != oldFundingCount)
        || (replay_.priceLimits().size() != oldPriceLimitCount);

    if (reloadedSession || sessionChangedFlag) emit sessionChanged();
    else if (hasLiveDataBatch) emit liveDataChanged();
    if (viewportChangedFlag) emit viewportChanged();
    if (statusText_ != nextStatus) {
        statusText_ = nextStatus;
        emit statusChanged();
    }
}

bool ChartController::activateLiveSource(const QString& sourceId, const QString& sessionPath) {
    const QString normalizedSourceId = sourceId.trimmed();
    if (normalizedSourceId.isEmpty()) {
        activateLiveOnlyMode();
        return false;
    }
    if (currentSourceKind_ == QStringLiteral("live")
        && currentSourceId_ == normalizedSourceId
        && sessionDir_ == sessionPath) {
        refreshProviderFromRegistry_();
        pollLiveData_();
        return true;
    }

    stopLiveData_();
    clearStrategyOverlay_();
    replay_.reset();
    loaded_ = false;
    sessionDir_ = sessionPath;
    currentSourceId_ = normalizedSourceId;
    currentSourceKind_ = QStringLiteral("live");
    SourceIdentity identity = sourceIdentityFromLiveRegistry(normalizedSourceId);
    const auto path = std::filesystem::path(stripFileUrl(sessionPath));
    if (identity.exchange.isEmpty() && !sessionPath.trimmed().isEmpty()) {
        identity = sourceIdentityFromSessionPath(path);
    }
    sourceExchange_ = identity.exchange;
    sourceMarket_ = identity.market;
    sourceSymbol_ = identity.symbol;
    lastRecordedLoadLabel_.clear();
    lastRecordedLoadNs_ = 0;
    lastRecordedLoadRows_ = 0;
    tsMin_ = tsMax_ = priceMinE8_ = priceMaxE8_ = 0;
    currentBookTickerIndex_ = -1;
    selectionActive_ = false;
    selectionSummaryText_.clear();
    if (!verticalMarkers_.empty()) {
        verticalMarkers_.clear();
        emit markersChanged();
    }

    if (!sessionPath.trimmed().isEmpty()) {
        const auto st = replay_.open(path);
        if (isOk(st)) {
            refreshLoadedStateFromSources_();
            if (loaded_) computeInitialViewport_();
        }
    }

    statusText_ = QStringLiteral("Live source selected");
    startLiveData_(path);
    emit sessionChanged();
    emit statusChanged();
    emit viewportChanged();
    emit selectionChanged();
    pollLiveData_();
    return true;
}

void ChartController::activateLiveOnlyMode() {
    stopLiveData_();
    clearStrategyOverlay_();
    replay_.reset();
    loaded_ = false;
    sessionDir_.clear();
    currentSourceId_.clear();
    liveProviderSourceId_.clear();
    currentSourceKind_ = QStringLiteral("live");
    sourceExchange_.clear();
    sourceMarket_.clear();
    sourceSymbol_.clear();
    lastRecordedLoadLabel_.clear();
    lastRecordedLoadNs_ = 0;
    lastRecordedLoadRows_ = 0;
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
    clearStrategyOverlay_();
    replay_.reset();
    loaded_ = false;
    sessionDir_.clear();
    currentSourceId_.clear();
    liveProviderSourceId_.clear();
    currentSourceKind_.clear();
    sourceExchange_.clear();
    sourceMarket_.clear();
    sourceSymbol_.clear();
    lastRecordedLoadLabel_.clear();
    lastRecordedLoadNs_ = 0;
    lastRecordedLoadRows_ = 0;
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

bool ChartController::addCandlesFile(const QString& path) {
    if (path.trimmed().isEmpty()) {
        statusText_ = QStringLiteral("No path. Enter a candles.jsonl path first.");
        emit statusChanged();
        return false;
    }

    const auto st = replay_.addCandlesFile(stripFileUrl(path));
    if (!isOk(st)) {
        statusText_ = replayFailureText(replay_, st, QStringLiteral("candles load failed"));
        emit statusChanged();
        return false;
    }

    loaded_ = loaded_ || !replay_.candles().empty();
    if (loaded_) computeInitialViewport_();
    statusText_ = QStringLiteral("+ candles (now %1 rows)").arg(replay_.candles().size());
    emit sessionChanged();
    emit statusChanged();
    emit viewportChanged();
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
        statusText_ = QStringLiteral("No path. Enter a depth_tape.jsonl or depth.jsonl path first.");
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

    refreshLoadedStateFromSources_();
    if (loaded_) computeInitialViewport_();
    currentBookTickerIndex_ = -1;

    statusText_ = QStringLiteral("Finalized.");
    emit sessionChanged();
    emit statusChanged();
    emit viewportChanged();
}

void ChartController::noteRecordedLoad_(QString label, std::uint64_t loadNs, std::size_t rowsLoaded) {
    lastRecordedLoadLabel_ = std::move(label);
    lastRecordedLoadNs_ = loadNs;
    lastRecordedLoadRows_ = rowsLoaded;
    hftrec::metrics::recordReplayLoad(rowsLoaded, loadNs);
}

QString ChartController::recordedLoadStatus_(QStringView prefix) const {
    QString counts = QStringLiteral("tr=%1 liq=%2 C=%3 C2=%4 depth=%5 bt=%6")
        .arg(QString::number(static_cast<qulonglong>(replay_.trades().size())))
        .arg(QString::number(static_cast<qulonglong>(replay_.liquidations().size())))
        .arg(QString::number(static_cast<qulonglong>(replay_.candles().size())))
        .arg(QString::number(static_cast<qulonglong>(replay_.candles2().size())))
        .arg(QString::number(static_cast<qulonglong>(replay_.depths().size())))
        .arg(QString::number(static_cast<qulonglong>(replay_.bookTickers().size())));
    counts += QStringLiteral(" mark=%1 index=%2 fund=%3 limits=%4")
        .arg(QString::number(static_cast<qulonglong>(replay_.markPrices().size())))
        .arg(QString::number(static_cast<qulonglong>(replay_.indexPrices().size())))
        .arg(QString::number(static_cast<qulonglong>(replay_.fundings().size())))
        .arg(QString::number(static_cast<qulonglong>(replay_.priceLimits().size())));
    return QStringLiteral("%1 %2 rows=%3 time=%4 | %5")
        .arg(prefix.toString(),
             lastRecordedLoadLabel_.isEmpty() ? QStringLiteral("session") : lastRecordedLoadLabel_,
             QString::number(static_cast<qulonglong>(lastRecordedLoadRows_)),
             formatLoadNsAsMs(lastRecordedLoadNs_),
             counts);
}

bool ChartController::loadRecordedChannel_(const QString& channelName) {
    if (currentSourceKind_ != QStringLiteral("recorded") || sessionDir_.trimmed().isEmpty()) {
        statusText_ = QStringLiteral("No recorded session selected for channel load.");
        emit statusChanged();
        return false;
    }

    const bool alreadyLoaded =
        (channelName == QStringLiteral("trades") && !replay_.trades().empty())
        || (channelName == QStringLiteral("liquidations") && !replay_.liquidations().empty())
        || (channelName == QStringLiteral("candles") && !replay_.candles().empty())
        || (channelName == QStringLiteral("candles2") && !replay_.candles2().empty())
        || (channelName == QStringLiteral("bookticker") && !replay_.bookTickers().empty())
        || (channelName == QStringLiteral("mark_price") && !replay_.markPrices().empty())
        || (channelName == QStringLiteral("index_price") && !replay_.indexPrices().empty())
        || (channelName == QStringLiteral("funding") && !replay_.fundings().empty())
        || (channelName == QStringLiteral("price_limit") && !replay_.priceLimits().empty());
    if (alreadyLoaded) return true;

    const bool hadLoadedData = loaded_;
    const auto path = std::filesystem::path(stripFileUrl(sessionDir_));
    const QJsonObject manifest = readSessionManifestObject(path);
    Status st = Status::Ok;
    bool loadedAnyChannel = false;
    QString label = channelName;

    auto loadOptional = [&](QStringView channelLabel, const std::filesystem::path& channelPath, auto&& load) {
        label = channelLabel.toString();
        if (channelPath.empty()) return true;
        loadedAnyChannel = true;
        return mergeStatus(st, load(channelPath));
    };

    const auto rowsBefore = replayRowCount(replay_);
    const auto loadStartedAt = std::chrono::steady_clock::now();
    if (channelName == QStringLiteral("trades")) {
        loadOptional(QStringLiteral("trades"),
                     sessionChannelPath(path, manifest, QStringLiteral("trades"), "trades.jsonl"),
                     [&](const std::filesystem::path& channelPath) {
                         return replay_.addTradesFile(channelPath, channelDeclaredCount(manifest, QStringLiteral("trades")));
                     });
    } else if (channelName == QStringLiteral("liquidations")) {
        loadOptional(QStringLiteral("liquidations"),
                     sessionChannelPath(path, manifest, QStringLiteral("liquidations"), "liquidations.jsonl"),
                     [&](const std::filesystem::path& channelPath) {
                         return replay_.addLiquidationsFile(channelPath, channelDeclaredCount(manifest, QStringLiteral("liquidations")));
                     });
    } else if (channelName == QStringLiteral("candles")) {
        loadOptional(QStringLiteral("candles"),
                     sessionChannelPath(path, manifest, QStringLiteral("candles"), "candles.jsonl"),
                     [&](const std::filesystem::path& channelPath) {
                         return replay_.addCandlesFile(channelPath, channelDeclaredCount(manifest, QStringLiteral("candles")), false);
                     });
    } else if (channelName == QStringLiteral("candles2")) {
        loadOptional(QStringLiteral("candles2"),
                     sessionChannelPath(path, manifest, QStringLiteral("candles2"), "candles2.jsonl"),
                     [&](const std::filesystem::path& channelPath) {
                         return replay_.addCandles2File(channelPath, channelDeclaredCount(manifest, QStringLiteral("candles2")), false);
                     });
    } else if (channelName == QStringLiteral("bookticker")) {
        loadOptional(QStringLiteral("bookticker"),
                     sessionChannelPath(path, manifest, QStringLiteral("bookticker"), "bookticker.jsonl"),
                     [&](const std::filesystem::path& channelPath) {
                         return replay_.addBookTickerFile(channelPath, channelDeclaredCount(manifest, QStringLiteral("bookticker")));
                     });
    } else if (channelName == QStringLiteral("mark_price")) {
        loadOptional(QStringLiteral("mark_price"),
                     sessionChannelPath(path, manifest, QStringLiteral("mark_price"), "mark_price.jsonl"),
                     [&](const std::filesystem::path& channelPath) {
                         return replay_.addMarkPriceFile(channelPath, channelDeclaredCount(manifest, QStringLiteral("mark_price")));
                     });
    } else if (channelName == QStringLiteral("index_price")) {
        loadOptional(QStringLiteral("index_price"),
                     sessionChannelPath(path, manifest, QStringLiteral("index_price"), "index_price.jsonl"),
                     [&](const std::filesystem::path& channelPath) {
                         return replay_.addIndexPriceFile(channelPath, channelDeclaredCount(manifest, QStringLiteral("index_price")));
                     });
    } else if (channelName == QStringLiteral("funding")) {
        loadOptional(QStringLiteral("funding"),
                     sessionChannelPath(path, manifest, QStringLiteral("funding"), "funding.jsonl"),
                     [&](const std::filesystem::path& channelPath) {
                         return replay_.addFundingFile(channelPath, channelDeclaredCount(manifest, QStringLiteral("funding")));
                     });
    } else if (channelName == QStringLiteral("price_limit")) {
        loadOptional(QStringLiteral("price_limit"),
                     sessionChannelPath(path, manifest, QStringLiteral("price_limit"), "price_limit.jsonl"),
                     [&](const std::filesystem::path& channelPath) {
                         return replay_.addPriceLimitFile(channelPath, channelDeclaredCount(manifest, QStringLiteral("price_limit")));
                     });
    } else {
        statusText_ = QStringLiteral("Unknown recorded channel: %1").arg(channelName);
        emit statusChanged();
        return false;
    }

    const auto loadNs = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - loadStartedAt).count());
    if (!loadedAnyChannel) {
        statusText_ = QStringLiteral("No %1 data found for selected session.").arg(label);
        emit statusChanged();
        return false;
    }
    if (isOk(st)) replay_.finalize();
    if (!isOk(st) || !isOk(replay_.status())) {
        const Status failure = isOk(st) ? replay_.status() : st;
        statusText_ = replayFailureText(replay_, failure, QStringLiteral("Channel load failed"));
        emit sessionChanged();
        emit statusChanged();
        return false;
    }

    const auto rowsAfter = replayRowCount(replay_);
    noteRecordedLoad_(label, loadNs, rowsAfter >= rowsBefore ? rowsAfter - rowsBefore : rowsAfter);
    refreshLoadedStateFromSources_();
    currentBookTickerIndex_ = -1;
    if (loaded_ && !hadLoadedData) {
        computeInitialViewport_();
        applyRecordedRenderWindowViewport_();
    }
    statusText_ = recordedLoadStatus_(QStringLiteral("Loaded"));
    emit sessionChanged();
    emit statusChanged();
    emit viewportChanged();
    return true;
}

bool ChartController::loadSessionForLayers(const QString& dir,
                                           bool tradesVisible,
                                           bool liquidationsVisible,
                                           bool candlesVisible,
                                           bool candles2Visible,
                                           bool orderbookVisible,
                                           bool bookTickerVisible,
                                           bool markPriceVisible,
                                           bool indexPriceVisible,
                                           bool fundingVisible,
                                           bool priceLimitVisible) {
    stopLiveData_();
    clearStrategyOverlay_();
    sessionDir_ = dir;
    currentSourceId_ = recordedSourceIdFromPath(dir);
    liveProviderSourceId_.clear();
    currentSourceKind_ = QStringLiteral("recorded");
    loaded_ = false;
    replay_ = hftrec::replay::SessionReplay{};
    lastRecordedLoadLabel_.clear();
    lastRecordedLoadNs_ = 0;
    lastRecordedLoadRows_ = 0;
    clearSelection();
    if (!verticalMarkers_.empty()) {
        verticalMarkers_.clear();
        emit markersChanged();
    }

    const auto path = std::filesystem::path(stripFileUrl(dir));
    const QJsonObject manifest = readSessionManifestObject(path);
    const SourceIdentity identity = sourceIdentityFromManifest(manifest);
    sourceExchange_ = identity.exchange;
    sourceMarket_ = identity.market;
    sourceSymbol_ = identity.symbol;
    Status st = Status::Ok;
    bool loadedAnyChannel = false;
    QStringList loadedChannels;
    const auto rowsBefore = replayRowCount(replay_);
    const auto loadStartedAt = std::chrono::steady_clock::now();

    auto loadOptional = [&](QStringView label, const std::filesystem::path& channelPath, auto&& load) {
        if (channelPath.empty()) return true;
        loadedAnyChannel = true;
        loadedChannels.push_back(label.toString());
        return mergeStatus(st, load(channelPath));
    };

    if (tradesVisible) {
        loadOptional(QStringLiteral("trades"),
                     sessionChannelPath(path, manifest, QStringLiteral("trades"), "trades.jsonl"),
                     [&](const std::filesystem::path& channelPath) {
                         return replay_.addTradesFile(channelPath, channelDeclaredCount(manifest, QStringLiteral("trades")));
                     });
    }
    if (liquidationsVisible) {
        loadOptional(QStringLiteral("liquidations"),
                     sessionChannelPath(path, manifest, QStringLiteral("liquidations"), "liquidations.jsonl"),
                     [&](const std::filesystem::path& channelPath) {
                         return replay_.addLiquidationsFile(channelPath, channelDeclaredCount(manifest, QStringLiteral("liquidations")));
                     });
    }
    if (candlesVisible) {
        loadOptional(QStringLiteral("candles"),
                     sessionChannelPath(path, manifest, QStringLiteral("candles"), "candles.jsonl"),
                     [&](const std::filesystem::path& channelPath) {
                         return replay_.addCandlesFile(channelPath, channelDeclaredCount(manifest, QStringLiteral("candles")), false);
                     });
    }
    if (candles2Visible) {
        loadOptional(QStringLiteral("candles2"),
                     sessionChannelPath(path, manifest, QStringLiteral("candles2"), "candles2.jsonl"),
                     [&](const std::filesystem::path& channelPath) {
                         return replay_.addCandles2File(channelPath, channelDeclaredCount(manifest, QStringLiteral("candles2")), false);
                     });
    }
    if (bookTickerVisible) {
        loadOptional(QStringLiteral("bookticker"),
                     sessionChannelPath(path, manifest, QStringLiteral("bookticker"), "bookticker.jsonl"),
                     [&](const std::filesystem::path& channelPath) {
                         return replay_.addBookTickerFile(channelPath, channelDeclaredCount(manifest, QStringLiteral("bookticker")));
                     });
    }
    if (markPriceVisible) {
        loadOptional(QStringLiteral("mark_price"),
                     sessionChannelPath(path, manifest, QStringLiteral("mark_price"), "mark_price.jsonl"),
                     [&](const std::filesystem::path& channelPath) {
                         return replay_.addMarkPriceFile(channelPath, channelDeclaredCount(manifest, QStringLiteral("mark_price")));
                     });
    }
    if (indexPriceVisible) {
        loadOptional(QStringLiteral("index_price"),
                     sessionChannelPath(path, manifest, QStringLiteral("index_price"), "index_price.jsonl"),
                     [&](const std::filesystem::path& channelPath) {
                         return replay_.addIndexPriceFile(channelPath, channelDeclaredCount(manifest, QStringLiteral("index_price")));
                     });
    }
    if (fundingVisible) {
        loadOptional(QStringLiteral("funding"),
                     sessionChannelPath(path, manifest, QStringLiteral("funding"), "funding.jsonl"),
                     [&](const std::filesystem::path& channelPath) {
                         return replay_.addFundingFile(channelPath, channelDeclaredCount(manifest, QStringLiteral("funding")));
                     });
    }
    if (priceLimitVisible) {
        loadOptional(QStringLiteral("price_limit"),
                     sessionChannelPath(path, manifest, QStringLiteral("price_limit"), "price_limit.jsonl"),
                     [&](const std::filesystem::path& channelPath) {
                         return replay_.addPriceLimitFile(channelPath, channelDeclaredCount(manifest, QStringLiteral("price_limit")));
                     });
    }
    if (orderbookVisible) {
        loadOptional(QStringLiteral("depth"),
                     sessionChannelPath(path, manifest, QStringLiteral("depth"), "depth.jsonl"),
                     [&](const std::filesystem::path& channelPath) {
                         const auto depthStatus = replay_.addDepthFileAllowPartial(channelPath, channelDeclaredCount(manifest, QStringLiteral("depth")));
                         return !isOk(depthStatus) && replay_.depths().empty() ? depthStatus : Status::Ok;
                     });
    }
    if (loadedAnyChannel && isOk(st)) replay_.finalize();
    if (!loadedAnyChannel) {
        statusText_ = QStringLiteral("No selected channel data found for session.");
        emit sessionChanged();
        emit statusChanged();
        return false;
    }
    if (!isOk(st) || !isOk(replay_.status())) {
        const Status failure = isOk(st) ? replay_.status() : st;
        statusText_ = replayFailureText(replay_, failure, QStringLiteral("Failed to load session"));
        emit sessionChanged();
        emit statusChanged();
        return false;
    }

    const auto rowsAfter = replayRowCount(replay_);
    const auto loadNs = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - loadStartedAt).count());
    noteRecordedLoad_(loadedChannels.join(QLatin1Char('+')),
                      loadNs,
                      rowsAfter >= rowsBefore ? rowsAfter - rowsBefore : rowsAfter);
    refreshLoadedStateFromSources_();
    currentBookTickerIndex_ = -1;
    statusText_ = recordedLoadStatus_(QStringLiteral("Loaded"));
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

bool ChartController::loadRecordedSession(const QString& dir) {
    const auto path = std::filesystem::path(stripFileUrl(dir));
    const QJsonObject manifest = readSessionManifestObject(path);
    if (!sessionChannelPath(path, manifest, QStringLiteral("bookticker"), "bookticker.jsonl").empty()) {
        return loadSessionForLayers(dir, false, false, false, false, false, true, false, false, false, false);
    }
    if (!sessionChannelPath(path, manifest, QStringLiteral("candles2"), "candles2.jsonl").empty()) {
        return loadSessionForLayers(dir, false, false, false, true, false, false, false, false, false, false);
    }
    if (!sessionChannelPath(path, manifest, QStringLiteral("candles"), "candles.jsonl").empty()) {
        return loadSessionForLayers(dir, false, false, true, false, false, false, false, false, false, false);
    }
    if (!sessionChannelPath(path, manifest, QStringLiteral("trades"), "trades.jsonl").empty()) {
        return loadSessionForLayers(dir, true, false, false, false, false, false, false, false, false, false);
    }
    return loadSessionForLayers(dir, false, false, false, false, false, false, false, false, false, false);
}

bool ChartController::loadRecordedTrades() {
    return loadRecordedChannel_(QStringLiteral("trades"));
}

bool ChartController::loadRecordedLiquidations() {
    return loadRecordedChannel_(QStringLiteral("liquidations"));
}

bool ChartController::loadRecordedCandles() {
    return loadRecordedChannel_(QStringLiteral("candles"));
}

bool ChartController::loadRecordedCandles2() {
    return loadRecordedChannel_(QStringLiteral("candles2"));
}

bool ChartController::loadRecordedBookTicker() {
    return loadRecordedChannel_(QStringLiteral("bookticker"));
}

bool ChartController::loadRecordedMarkPrice() {
    return loadRecordedChannel_(QStringLiteral("mark_price"));
}

bool ChartController::loadRecordedIndexPrice() {
    return loadRecordedChannel_(QStringLiteral("index_price"));
}

bool ChartController::loadRecordedFunding() {
    return loadRecordedChannel_(QStringLiteral("funding"));
}

bool ChartController::loadRecordedPriceLimit() {
    return loadRecordedChannel_(QStringLiteral("price_limit"));
}

bool ChartController::loadRecordedOrderbook() {
    if (currentSourceKind_ != QStringLiteral("recorded") || sessionDir_.trimmed().isEmpty()) {
        statusText_ = QStringLiteral("No recorded session selected for orderbook load.");
        emit statusChanged();
        return false;
    }
    if (hasOrderbook()) return true;
    const bool hadLoadedData = loaded_;

    const auto path = std::filesystem::path(stripFileUrl(sessionDir_));
    const QJsonObject manifest = readSessionManifestObject(path);
    Status st = Status::Ok;
    bool loadedAnyChannel = false;

    auto loadOptional = [&](const std::filesystem::path& channelPath, auto&& load) {
        if (channelPath.empty()) return true;
        loadedAnyChannel = true;
        return mergeStatus(st, load(channelPath));
    };

    const auto rowsBefore = replayRowCount(replay_);
    const auto loadStartedAt = std::chrono::steady_clock::now();
    loadOptional(sessionChannelPath(path, manifest, QStringLiteral("depth"), "depth.jsonl"),
                 [&](const std::filesystem::path& channelPath) {
                     const auto depthStatus = replay_.addDepthFileAllowPartial(channelPath, channelDeclaredCount(manifest, QStringLiteral("depth")));
                     return !isOk(depthStatus) && replay_.depths().empty() ? depthStatus : Status::Ok;
                 });

    if (!loadedAnyChannel) {
        if (hadLoadedData || loaded_) {
            statusText_ = QStringLiteral("Loaded trades=%1 liq=%2 C=%3 C2=%4 depth=%5 bookticker=%6")
                              .arg(static_cast<qulonglong>(replay_.trades().size()))
                              .arg(static_cast<qulonglong>(replay_.liquidations().size()))
                              .arg(static_cast<qulonglong>(replay_.candles().size()))
                              .arg(static_cast<qulonglong>(replay_.candles2().size()))
                              .arg(static_cast<qulonglong>(replay_.depths().size()))
                              .arg(static_cast<qulonglong>(replay_.bookTickers().size()));
            emit sessionChanged();
            emit statusChanged();
            emit viewportChanged();
            return true;
        }
        statusText_ = QStringLiteral("No orderbook data found for selected session.");
        emit statusChanged();
        return false;
    }
    if (isOk(st)) replay_.finalize();
    if (!isOk(st) || !isOk(replay_.status())) {
        const Status failure = isOk(st) ? replay_.status() : st;
        statusText_ = replayFailureText(replay_, failure, QStringLiteral("Orderbook load failed"));
        emit sessionChanged();
        emit statusChanged();
        return false;
    }

    refreshLoadedStateFromSources_();
    currentBookTickerIndex_ = -1;
    if (loaded_ && !hadLoadedData) {
        computeInitialViewport_();
        applyRecordedRenderWindowViewport_();
    }
    const auto rowsAfter = replayRowCount(replay_);
    const auto loadNs = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - loadStartedAt).count());
    noteRecordedLoad_(QStringLiteral("orderbook"),
                      loadNs,
                      rowsAfter >= rowsBefore ? rowsAfter - rowsBefore : rowsAfter);
    statusText_ = recordedLoadStatus_(QStringLiteral("Loaded"));
    if (!replay_.errorDetail().empty()) {
        statusText_ += QStringLiteral(" | orderbook partial: %1 depth=%2")
            .arg(QString::fromStdString(std::string{replay_.errorDetail()}))
            .arg(static_cast<qulonglong>(replay_.depths().size()));
    }
    emit sessionChanged();
    emit statusChanged();
    emit viewportChanged();
    return true;
}

bool ChartController::loadSession(const QString& dir) {
#if HFTREC_WITH_CXET_REPLAY
    stopLiveData_();
    clearStrategyOverlay_();
    sessionDir_ = dir;
    currentSourceId_ = recordedSourceIdFromPath(dir);
    liveProviderSourceId_.clear();
    currentSourceKind_ = QStringLiteral("recorded");
    loaded_ = false;
    replay_ = hftrec::replay::SessionReplay{};
    lastRecordedLoadLabel_.clear();
    lastRecordedLoadNs_ = 0;
    lastRecordedLoadRows_ = 0;
    clearSelection();
    if (!verticalMarkers_.empty()) {
        verticalMarkers_.clear();
        emit markersChanged();
    }

    const auto path = std::filesystem::path(stripFileUrl(dir));
    const SourceIdentity identity = sourceIdentityFromSessionPath(path);
    sourceExchange_ = identity.exchange;
    sourceMarket_ = identity.market;
    sourceSymbol_ = identity.symbol;
    std::string cxetReplayError;
    const hftrec::replay::CxetReplaySessionLoader cxetLoader{};
    const auto st = cxetLoader.loadRenderOnce(path, replay_, cxetReplayError);
    if (!isOk(st)) {
        statusText_ = cxetReplayError.empty()
            ? replayFailureText(replay_, st, QStringLiteral("Failed to load session"))
            : QStringLiteral("Failed to load session: %1").arg(QString::fromStdString(cxetReplayError));
        emit sessionChanged();
        emit statusChanged();
        return false;
    }

    refreshLoadedStateFromSources_();
    currentBookTickerIndex_ = -1;
    statusText_ = QStringLiteral("Loaded trades=%1 liq=%2 C=%3 C2=%4 depth=%5 bookticker=%6")
                       .arg(replay_.trades().size())
                       .arg(replay_.liquidations().size())
                       .arg(replay_.candles().size())
                       .arg(replay_.candles2().size())
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
#else
    return loadSessionForLayers(dir, true, true, true, true, true, true, true, true, true, true);
#endif
}

}  // namespace hftrec::gui::viewer












