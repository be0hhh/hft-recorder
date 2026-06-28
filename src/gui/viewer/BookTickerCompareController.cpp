#include "gui/viewer/BookTickerCompareController.hpp"

#include <algorithm>
#include <cmath>
#include <string_view>

#include "core/arbitrage/PriceBasis.hpp"
#include "core/corpus/InstrumentMetadata.hpp"
#include "core/replay/SessionReplay.hpp"
#include "gui/backtests/BacktestSessionSummary.hpp"

#include <QFile>
#include <QByteArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStringList>

namespace hftrec::gui::viewer {

namespace {

bool isLive(QString value) noexcept {
    return value.trimmed().toLower() == QStringLiteral("live");
}

bool rowsLessTs(const hftrec::replay::BookTickerRow& lhs,
                const hftrec::replay::BookTickerRow& rhs) noexcept {
    if (lhs.tsNs != rhs.tsNs) return lhs.tsNs < rhs.tsNs;
    if (lhs.ingestSeq != rhs.ingestSeq) return lhs.ingestSeq < rhs.ingestSeq;
    return lhs.captureSeq < rhs.captureSeq;
}

double cleanBps(double value) noexcept {
    if (!std::isfinite(value) || value < 0.0) return 0.0;
    if (value > 1000.0) return 1000.0;
    return value;
}

double cleanMeanWindowSeconds(double value) noexcept {
    if (!std::isfinite(value) || value < 0.1) return 0.1;
    if (value > 3600.0) return 3600.0;
    return value;
}

std::int64_t meanWindowNs(double seconds) noexcept {
    return static_cast<std::int64_t>(cleanMeanWindowSeconds(seconds) * 1000000000.0);
}

bool hasMarketRows(const std::vector<hftrec::replay::BookTickerRow>& rows,
                   const std::vector<hftrec::replay::CandleRow>& candles) noexcept {
    return !rows.empty() || !candles.empty();
}

QString feeSettingsKey(const QString& exchange, const QString& market) {
    const QString ex = exchange.trimmed().toLower();
    const QString mk = market.trimmed().toLower();
    if (ex.isEmpty() || mk.isEmpty()) return {};
    return QStringLiteral("fees/%1/%2/action_bps").arg(ex, mk);
}

QString meanWindowSettingsKey() {
    return QStringLiteral("viewer/bookticker_compare/mean_window_seconds");
}

std::filesystem::path existingFileOrEmpty(const std::filesystem::path& path) noexcept {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec) && !ec ? path : std::filesystem::path{};
}

QJsonObject readSessionManifestObject(const std::filesystem::path& sessionPath) {
    QFile file(QString::fromStdString((sessionPath / "manifest.json").string()));
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    return doc.isObject() ? doc.object() : QJsonObject{};
}

std::filesystem::path recordedBookTickerPath(const std::filesystem::path& sessionPath) {
    const QJsonObject manifest = readSessionManifestObject(sessionPath);
    const QJsonObject channels = manifest.value(QStringLiteral("channels")).toObject();
    const QString manifestPath = channels.value(QStringLiteral("bookticker")).toObject().value(QStringLiteral("path")).toString();
    if (!manifestPath.isEmpty()) {
        if (const auto path = existingFileOrEmpty(sessionPath / manifestPath.toStdString()); !path.empty()) return path;
    }
    if (const auto path = existingFileOrEmpty(sessionPath / "jsonl" / "bookticker.jsonl"); !path.empty()) return path;
    return existingFileOrEmpty(sessionPath / "bookticker.jsonl");
}

std::filesystem::path recordedFundingPath(const std::filesystem::path& sessionPath) {
    const QJsonObject manifest = readSessionManifestObject(sessionPath);
    const QJsonObject channels = manifest.value(QStringLiteral("channels")).toObject();
    const QString manifestPath = channels.value(QStringLiteral("funding")).toObject().value(QStringLiteral("path")).toString();
    if (!manifestPath.isEmpty()) {
        if (const auto path = existingFileOrEmpty(sessionPath / manifestPath.toStdString()); !path.empty()) return path;
    }
    if (const auto path = existingFileOrEmpty(sessionPath / "jsonl" / "funding.jsonl"); !path.empty()) return path;
    return existingFileOrEmpty(sessionPath / "funding.jsonl");
}

std::filesystem::path recordedChannelPath(const std::filesystem::path& sessionPath,
                                          const char* channel,
                                          const char* fallbackName) {
    const QJsonObject manifest = readSessionManifestObject(sessionPath);
    const QJsonObject channels = manifest.value(QStringLiteral("channels")).toObject();
    const QString manifestPath = channels.value(QString::fromLatin1(channel)).toObject().value(QStringLiteral("path")).toString();
    if (!manifestPath.isEmpty()) {
        if (const auto path = existingFileOrEmpty(sessionPath / manifestPath.toStdString()); !path.empty()) return path;
    }
    if (const auto path = existingFileOrEmpty(sessionPath / "jsonl" / fallbackName); !path.empty()) return path;
    return existingFileOrEmpty(sessionPath / fallbackName);
}

std::filesystem::path recordedDetailedCandlesPath(const std::filesystem::path& sessionPath) {
    return recordedChannelPath(sessionPath, "candles2", "candles2.jsonl");
}

std::filesystem::path recordedTieredCandlesPath(const std::filesystem::path& sessionPath) {
    return recordedChannelPath(sessionPath, "candles", "candles.jsonl");
}

std::string manifestMarketHint(const std::filesystem::path& sessionPath) {
    const QJsonObject manifest = readSessionManifestObject(sessionPath);
    const QJsonObject identity = manifest.value(QStringLiteral("identity")).toObject();
    return identity.value(QStringLiteral("market")).toString().trimmed().toLower().toStdString();
}

QString manifestHealthLabel(const std::filesystem::path& sessionPath) {
    const QJsonObject manifest = readSessionManifestObject(sessionPath);
    return hftrec::gui::sessionHealthSummaryLabel(
        manifest.value(QStringLiteral("integrity")).toObject().value(QStringLiteral("session_health")).toString(),
        manifest.value(QStringLiteral("summary")).toObject().value(QStringLiteral("warning_summary")).toString());
}

std::int64_t metadataPriceBasisQtyE8(const std::filesystem::path& sessionPath) {
    QFile file(QString::fromStdString((sessionPath / "instrument_metadata.json").string()));
    if (!file.open(QIODevice::ReadOnly)) return hftrec::arbitrage::kPriceBasisScaleE8;

    const QByteArray bytes = file.readAll();
    hftrec::corpus::InstrumentMetadata metadata{};
    const auto status = hftrec::corpus::parseInstrumentMetadataJson(
        std::string_view{bytes.constData(), static_cast<std::size_t>(bytes.size())},
        metadata);
    if (!isOk(status) || !metadata.priceBasisQtyE8.has_value() || *metadata.priceBasisQtyE8 <= 0) {
        return hftrec::arbitrage::kPriceBasisScaleE8;
    }
    return *metadata.priceBasisQtyE8;
}

void normalizeBookTickerRows(std::vector<hftrec::replay::BookTickerRow>& rows,
                             std::int64_t priceBasisQtyE8) {
    for (auto& row : rows) {
        row.bidPriceE8 = hftrec::arbitrage::normalizeNativePriceE8(row.bidPriceE8, priceBasisQtyE8);
        row.askPriceE8 = hftrec::arbitrage::normalizeNativePriceE8(row.askPriceE8, priceBasisQtyE8);
    }
}

void normalizeCandleRows(std::vector<hftrec::replay::CandleRow>& rows,
                         std::int64_t priceBasisQtyE8) {
    for (auto& row : rows) {
        row.openE8 = hftrec::arbitrage::normalizeNativePriceE8(row.openE8, priceBasisQtyE8);
        row.highE8 = hftrec::arbitrage::normalizeNativePriceE8(row.highE8, priceBasisQtyE8);
        row.lowE8 = hftrec::arbitrage::normalizeNativePriceE8(row.lowE8, priceBasisQtyE8);
        row.closeE8 = hftrec::arbitrage::normalizeNativePriceE8(row.closeE8, priceBasisQtyE8);
    }
}

double clampZoom(double value) noexcept {
    if (!std::isfinite(value) || value < 1.0) return 1.0;
    if (value > 4096.0) return 4096.0;
    return value;
}

double clampPan(double pan, double zoom) noexcept {
    zoom = clampZoom(zoom);
    const double limit = zoom <= 1.0 ? 0.0 : (1.0 - (1.0 / zoom)) * 0.5;
    return std::clamp(pan, -limit, limit);
}

void zoomAt(double factor, double anchorFraction, double& zoom, double& pan) noexcept {
    if (factor <= 0.0) return;
    anchorFraction = std::clamp(anchorFraction, 0.0, 1.0);
    const double prevZoom = clampZoom(zoom);
    const double nextZoom = clampZoom(prevZoom * factor);
    pan += (anchorFraction - 0.5) * ((1.0 / prevZoom) - (1.0 / nextZoom));
    zoom = nextZoom;
    pan = clampPan(pan, zoom);
}

}  // namespace

BookTickerCompareController::BookTickerCompareController(QObject* parent)
    : QObject(parent) {
    meanWindowSeconds_ = cleanMeanWindowSeconds(QSettings{}.value(meanWindowSettingsKey(), meanWindowSeconds_).toDouble());
    updateLowerPane_();
    liveTimer_.setInterval(100);
    connect(&liveTimer_, &QTimer::timeout, this, &BookTickerCompareController::pollLive_);
}

bool BookTickerCompareController::setPrimarySource(const QString& sourceId,
                                                   const QString& sourceKind,
                                                   const QString& sessionPath) {
    if (sourceId.trimmed().isEmpty()) {
        primary_ = SourceState{};
        primarySourceId_.clear();
        rebuild_();
        emit sourcesChanged();
        return true;
    }
    if (primary_.sourceId == sourceId && primary_.sourceKind == sourceKind
        && primary_.sessionPath == std::filesystem::path{sessionPath.toStdString()}) {
        return true;
    }
    if (!setSource_(primary_, sourceId, sourceKind, sessionPath)) return false;
    primarySourceId_ = sourceId;
    emit sourcesChanged();
    rebuild_();
    return true;
}

bool BookTickerCompareController::setSecondarySource(const QString& sourceId,
                                                     const QString& sourceKind,
                                                     const QString& sessionPath) {
    if (sourceId.trimmed().isEmpty()) {
        secondary_ = SourceState{};
        secondarySourceId_.clear();
        rebuild_();
        emit sourcesChanged();
        return true;
    }
    if (secondary_.sourceId == sourceId && secondary_.sourceKind == sourceKind
        && secondary_.sessionPath == std::filesystem::path{sessionPath.toStdString()}) {
        return true;
    }
    if (!setSource_(secondary_, sourceId, sourceKind, sessionPath)) return false;
    secondarySourceId_ = sourceId;
    emit sourcesChanged();
    rebuild_();
    return true;
}

void BookTickerCompareController::clear() {
    primary_ = SourceState{};
    secondary_ = SourceState{};
    primarySourceId_.clear();
    secondarySourceId_.clear();
    primaryRows_.clear();
    secondaryRows_.clear();
    primaryFundingRows_.clear();
    secondaryFundingRows_.clear();
    primaryCandles_.clear();
    secondaryCandles_.clear();
    spreadPoints_.clear();
    meanPoints_.clear();
    candleSpreadPoints_.clear();
    selectedBacktestResult_.clear();
    strategyOverlay_ = StrategyOverlayData{};
    strategyIndicator_ = StrategyIndicatorData{};
    rateLimitUsage_ = RateLimitUsageData{};
    updateLowerPane_();
    fullTsMin_ = 0;
    fullTsMax_ = 1;
    tsMin_ = 0;
    tsMax_ = 1;
    viewportInitialized_ = false;
    userViewportControl_ = false;
    resetValueScale_();
    liveTimer_.stop();
    setStatus_(QStringLiteral("Select two market sessions"));
    emit sourcesChanged();
    emit dataChanged();
}

bool BookTickerCompareController::setBacktestResult(const QString& resultPath) {
    const QString next = resultPath.trimmed();
    if (selectedBacktestResult_ == next) return true;
    selectedBacktestResult_ = next;
    strategyOverlay_ = StrategyOverlayData{};
    strategyIndicator_ = StrategyIndicatorData{};
    rateLimitUsage_ = RateLimitUsageData{};
    if (next.isEmpty()) {
        updateLowerPane_();
        if (hasMarketRows(primaryRows_, primaryCandles_) && hasMarketRows(secondaryRows_, secondaryCandles_) && lowerPaneState_.hasData) {
            setStatus_(comparisonReadyStatus_());
        }
        emit dataChanged();
        return true;
    }

    std::string error;
    const std::int64_t fallbackRunEndNs = fullTsMax_ > 0 ? fullTsMax_ : 1;
    if (!loadStrategyOverlayFromResult(std::filesystem::path{next.toStdString()}, fallbackRunEndNs, strategyOverlay_, error)) {
        setStatus_(QStringLiteral("Failed to load backtest overlay: %1").arg(QString::fromStdString(error)));
        emit dataChanged();
        return false;
    }
    const std::filesystem::path resultDir{next.toStdString()};
    if (strategyOverlay_.spreadPoints.empty()
        && !loadStrategyIndicatorFromResult(std::filesystem::path{next.toStdString()}, strategyIndicator_, error)) {
        setStatus_(QStringLiteral("Failed to load strategy indicator: %1").arg(QString::fromStdString(error)));
        emit dataChanged();
        return false;
    }
    if (!loadRateLimitUsageFromResult(resultDir, rateLimitUsage_, error)) {
        setStatus_(QStringLiteral("Failed to load rate-limit usage: %1").arg(QString::fromStdString(error)));
        emit dataChanged();
        return false;
    }
    updateLowerPane_();
    updateFullRange_();
    initializeViewportIfNeeded_();
    if (rateLimitVisible_ && rateLimitUsage_.empty()) {
        setStatus_(QStringLiteral("No rate-limit usage in selected result"));
    } else if (hasMarketRows(primaryRows_, primaryCandles_) && hasMarketRows(secondaryRows_, secondaryCandles_) && lowerPaneState_.hasData) {
        setStatus_(comparisonReadyStatus_());
    }
    emit dataChanged();
    return true;
}

void BookTickerCompareController::setRateLimitVisible(bool visible) {
    if (rateLimitVisible_ == visible) return;
    rateLimitVisible_ = visible;
    updateLowerPane_();
    if (visible && rateLimitUsage_.empty()) {
        setStatus_(QStringLiteral("No rate-limit usage in selected result"));
    } else if (hasMarketRows(primaryRows_, primaryCandles_) && hasMarketRows(secondaryRows_, secondaryCandles_) && lowerPaneState_.hasData) {
        setStatus_(comparisonReadyStatus_());
    }
    emit dataChanged();
}

void BookTickerCompareController::setPrimaryFeeActionBps(double bps) {
    bps = cleanBps(bps);
    if (std::abs(primaryFeeActionBps_ - bps) < 0.000001) return;
    primaryFeeActionBps_ = bps;
    emit feesChanged();
    rebuild_();
}

void BookTickerCompareController::setSecondaryFeeActionBps(double bps) {
    bps = cleanBps(bps);
    if (std::abs(secondaryFeeActionBps_ - bps) < 0.000001) return;
    secondaryFeeActionBps_ = bps;
    emit feesChanged();
    rebuild_();
}

void BookTickerCompareController::setMeanWindowSeconds(double seconds) {
    seconds = cleanMeanWindowSeconds(seconds);
    if (std::abs(meanWindowSeconds_ - seconds) < 0.000001) return;
    meanWindowSeconds_ = seconds;
    QSettings{}.setValue(meanWindowSettingsKey(), meanWindowSeconds_);
    emit meanChanged();
    rebuild_();
}

double BookTickerCompareController::savedFeeActionBps(const QString& exchange, const QString& market) const {
    const QString key = feeSettingsKey(exchange, market);
    if (key.isEmpty()) return 0.0;
    return cleanBps(QSettings{}.value(key, 0.0).toDouble());
}

void BookTickerCompareController::saveFeeActionBps(const QString& exchange, const QString& market, double bps) {
    const QString key = feeSettingsKey(exchange, market);
    if (key.isEmpty()) return;
    QSettings{}.setValue(key, cleanBps(bps));
}

void BookTickerCompareController::autoFit() {
    updateFullRange_();
    tsMin_ = fullTsMin_;
    tsMax_ = fullTsMax_;
    viewportInitialized_ = true;
    userViewportControl_ = false;
    resetValueScale_();
    emit viewportChanged();
    emit dataChanged();
}

void BookTickerCompareController::panTime(double fraction) {
    const qint64 span = tsMax_ - tsMin_;
    if (span <= 0) return;
    const qint64 delta = static_cast<qint64>(static_cast<double>(span) * fraction);
    const qint64 nextMin = tsMin_ + delta;
    const qint64 nextMax = tsMax_ + delta;
    if (nextMax <= nextMin) return;
    tsMin_ = nextMin;
    tsMax_ = nextMax;
    viewportInitialized_ = true;
    userViewportControl_ = true;
    emit viewportChanged();
    emit dataChanged();
}

void BookTickerCompareController::zoomTime(double factor) {
    zoomTimeAt(factor, 0.5);
}

void BookTickerCompareController::zoomTimeAt(double factor, double anchorFraction) {
    if (factor <= 0.0) return;
    anchorFraction = std::clamp(anchorFraction, 0.0, 1.0);
    const qint64 span = tsMax_ - tsMin_;
    if (span <= 0) return;
    const qint64 anchorTs = tsMin_ + static_cast<qint64>(static_cast<double>(span) * anchorFraction);
    qint64 nextSpan = static_cast<qint64>(static_cast<double>(span) / factor);
    if (nextSpan < 1000000) nextSpan = 1000000;
    qint64 nextMin = anchorTs - static_cast<qint64>(static_cast<double>(nextSpan) * anchorFraction);
    qint64 nextMax = nextMin + nextSpan;
    if (nextMax <= nextMin) return;
    tsMin_ = nextMin;
    tsMax_ = nextMax;
    viewportInitialized_ = true;
    userViewportControl_ = true;
    emit viewportChanged();
    emit dataChanged();
}

void BookTickerCompareController::panPrice(double fraction) {
    pricePan_ = clampPan(pricePan_ + fraction / priceZoom_, priceZoom_);
    emit viewportChanged();
    emit dataChanged();
}

void BookTickerCompareController::panSpread(double fraction) {
    spreadPan_ = clampPan(spreadPan_ + fraction / spreadZoom_, spreadZoom_);
    emit viewportChanged();
    emit dataChanged();
}

void BookTickerCompareController::zoomPrice(double factor) {
    priceZoom_ = clampZoom(priceZoom_ * factor);
    pricePan_ = clampPan(pricePan_, priceZoom_);
    emit viewportChanged();
    emit dataChanged();
}

void BookTickerCompareController::zoomSpread(double factor) {
    spreadZoom_ = clampZoom(spreadZoom_ * factor);
    spreadPan_ = clampPan(spreadPan_, spreadZoom_);
    emit viewportChanged();
    emit dataChanged();
}

void BookTickerCompareController::zoomPriceAt(double factor, double anchorFraction) {
    zoomAt(factor, anchorFraction, priceZoom_, pricePan_);
    emit viewportChanged();
    emit dataChanged();
}

void BookTickerCompareController::zoomSpreadAt(double factor, double anchorFraction) {
    zoomAt(factor, anchorFraction, spreadZoom_, spreadPan_);
    emit viewportChanged();
    emit dataChanged();
}

void BookTickerCompareController::resetValueScale() {
    resetValueScale_();
    emit viewportChanged();
    emit dataChanged();
}

void BookTickerCompareController::setLiveUpdateIntervalMs(int intervalMs) {
    if (intervalMs < 16) intervalMs = 16;
    liveTimer_.setInterval(intervalMs);
}

bool BookTickerCompareController::setSource_(SourceState& state,
                                             const QString& sourceId,
                                             const QString& sourceKind,
                                             const QString& sessionPath) {
    state = SourceState{};
    state.sourceId = sourceId;
    state.sourceKind = sourceKind;
    state.sessionPath = std::filesystem::path{sessionPath.toStdString()};
    state.nextBatchId = 1;
    viewportInitialized_ = false;
    userViewportControl_ = false;
    resetValueScale_();

    if (sourceId.trimmed().isEmpty()) {
        state.rows.clear();
        setStatus_(QStringLiteral("Select two market sessions"));
        return false;
    }

    if (isLive(sourceKind)) {
        state.liveProvider = LiveDataRegistry::instance().makeProvider(sourceId.toStdString());
        if (!state.liveProvider) {
            setStatus_(QStringLiteral("Live source is no longer available"));
            return false;
        }
        state.liveProvider->start(LiveDataProviderConfig{state.sessionPath, {}, sourceId.toStdString()});
        liveTimer_.start();
        return true;
    }

    if (sessionPath.trimmed().isEmpty()) {
        state.rows.clear();
        setStatus_(QStringLiteral("Recorded session path is empty"));
        return false;
    }
    reloadRecorded_(state);
    return true;
}

void BookTickerCompareController::reloadRecorded_(SourceState& state) {
    state.rows.clear();
    state.fundings.clear();
    state.candles.clear();
    state.marketHint = manifestMarketHint(state.sessionPath);
    state.healthLabel = manifestHealthLabel(state.sessionPath);
    state.priceBasisQtyE8 = metadataPriceBasisQtyE8(state.sessionPath);
    const auto bookTickerPath = recordedBookTickerPath(state.sessionPath);
    if (!bookTickerPath.empty()) {
        hftrec::replay::SessionReplay replay{};
        const auto status = replay.addBookTickerFile(bookTickerPath);
        if (!isOk(status)) {
            setStatus_(QStringLiteral("Failed to load recorded bookTicker"));
        } else {
            replay.finalize();
            state.rows = replay.bookTickers();
            normalizeBookTickerRows(state.rows, state.priceBasisQtyE8);
            std::sort(state.rows.begin(), state.rows.end(), rowsLessTs);
        }
    }

    const auto fundingPath = recordedFundingPath(state.sessionPath);
    if (!fundingPath.empty()) {
        hftrec::replay::SessionReplay fundingReplay{};
        if (isOk(fundingReplay.addFundingFile(fundingPath))) {
            state.fundings = fundingReplay.fundings();
            std::sort(state.fundings.begin(), state.fundings.end(), [](const auto& lhs, const auto& rhs) noexcept {
                return lhs.tsNs < rhs.tsNs;
            });
        }
    }

    const auto detailedCandlesPath = recordedDetailedCandlesPath(state.sessionPath);
    const auto tieredCandlesPath = recordedTieredCandlesPath(state.sessionPath);
    const bool useDetailedCandles = !detailedCandlesPath.empty();
    const auto candlePath = useDetailedCandles ? detailedCandlesPath : tieredCandlesPath;
    if (!candlePath.empty()) {
        hftrec::replay::SessionReplay candleReplay{};
        const auto status = useDetailedCandles
            ? candleReplay.addCandles2File(candlePath)
            : candleReplay.addCandlesFile(candlePath);
        if (isOk(status)) {
            const auto& sourceCandles = useDetailedCandles ? candleReplay.candles2() : candleReplay.candles();
            state.candles = hftrec::arbitrage::selectCompareCandles(sourceCandles);
            normalizeCandleRows(state.candles, state.priceBasisQtyE8);
        } else if (state.rows.empty()) {
            setStatus_(QStringLiteral("Failed to load recorded candles"));
        }
    }
    if (state.marketHint.empty()) {
        if (!state.candles.empty() && !state.candles.front().market.empty()) {
            state.marketHint = state.candles.front().market;
        } else {
            state.marketHint = state.sourceId.trimmed().toLower().toStdString();
        }
    }
}

void BookTickerCompareController::pollLive_() {
    bool changed = false;
    auto pollOne = [&](SourceState& state) {
        if (!state.liveProvider) return;
        const auto result = state.liveProvider->pollHot(state.nextBatchId++);
        if (!isOk(result.failureStatus)) {
            setStatus_(QString::fromStdString(result.failureDetail));
            return;
        }
        if (!result.batch.bookTickers.empty()) {
            state.rows.insert(state.rows.end(), result.batch.bookTickers.begin(), result.batch.bookTickers.end());
            std::sort(state.rows.begin(), state.rows.end(), rowsLessTs);
            changed = true;
        }
        if (!result.batch.fundings.empty()) {
            state.fundings.insert(state.fundings.end(), result.batch.fundings.begin(), result.batch.fundings.end());
            std::sort(state.fundings.begin(), state.fundings.end(), [](const auto& lhs, const auto& rhs) noexcept {
                return lhs.tsNs < rhs.tsNs;
            });
            changed = true;
        }
    };

    pollOne(primary_);
    pollOne(secondary_);
    if (changed) rebuild_();
    if (!primary_.liveProvider && !secondary_.liveProvider) liveTimer_.stop();
}

void BookTickerCompareController::rebuild_() {
    primaryRows_ = primary_.rows;
    secondaryRows_ = secondary_.rows;
    primaryFundingRows_ = primary_.fundings;
    secondaryFundingRows_ = secondary_.fundings;
    primaryCandles_ = primary_.candles;
    secondaryCandles_ = secondary_.candles;
    spreadPoints_ = hftrec::arbitrage::buildBestSideBookTickerSpread(primaryRows_, secondaryRows_, totalFeePenaltyBps());
    meanPoints_ = hftrec::arbitrage::buildRollingBookTickerSpreadMean(spreadPoints_, meanWindowNs(meanWindowSeconds_), totalFeePenaltyBps());
    candleSpreadPoints_ = hftrec::arbitrage::buildBestSideCandleSpread(
        hftrec::arbitrage::CandleSpreadSource{primaryCandles_, primary_.marketHint},
        hftrec::arbitrage::CandleSpreadSource{secondaryCandles_, secondary_.marketHint});
    updateLowerPane_();
    updateFullRange_();
    initializeViewportIfNeeded_();

    if (primarySourceId_.isEmpty() || secondarySourceId_.isEmpty()) {
        setStatus_(QStringLiteral("Select two market sessions"));
    } else if ((primaryRows_.empty() && primaryCandles_.empty()) || (secondaryRows_.empty() && secondaryCandles_.empty())) {
        setStatus_(QStringLiteral("Waiting for market rows from both sessions"));
    } else if (rateLimitVisible_ && rateLimitUsage_.empty()) {
        setStatus_(QStringLiteral("No rate-limit usage in selected result"));
    } else if (!lowerPaneState_.hasData) {
        setStatus_(QStringLiteral("Not enough compatible quotes or candles to build spread"));
    } else {
        setStatus_(comparisonReadyStatus_());
    }
    emit dataChanged();
}

void BookTickerCompareController::updateLowerPane_() {
    lowerPaneState_ = selectCompareLowerPane(strategyOverlay_,
                                             strategyIndicator_,
                                             rateLimitVisible_,
                                             !rateLimitUsage_.empty(),
                                             !spreadPoints_.empty(),
                                             !candleSpreadPoints_.empty());
}

void BookTickerCompareController::resetValueScale_() noexcept {
    priceZoom_ = 1.0;
    pricePan_ = 0.0;
    spreadZoom_ = 1.0;
    spreadPan_ = 0.0;
}

void BookTickerCompareController::updateFullRange_() noexcept {
    bool hasTs = false;
    auto absorb = [&](qint64 ts) noexcept {
        if (!hasTs) {
            fullTsMin_ = ts;
            fullTsMax_ = ts;
            hasTs = true;
            return;
        }
        if (ts < fullTsMin_) fullTsMin_ = ts;
        if (ts > fullTsMax_) fullTsMax_ = ts;
    };
    const auto absorbRows = [&](const auto& rows) noexcept {
        for (const auto& row : rows) absorb(row.tsNs);
    };

    if (!primaryRows_.empty() || !secondaryRows_.empty()) {
        absorbRows(primaryRows_);
        absorbRows(secondaryRows_);
    } else {
        for (const auto& point : strategyOverlay_.spreadPoints) absorb(point.tsNs);
        for (const auto& point : strategyIndicator_.points) absorb(point.tsNs);
        for (const auto& point : rateLimitUsage_.points) absorb(point.tsNs);
        for (const auto& point : spreadPoints_) absorb(point.tsNs);
        for (const auto& point : candleSpreadPoints_) absorb(point.tsNs);
        if (!hasTs) {
            absorbRows(primaryCandles_);
            absorbRows(secondaryCandles_);
        }
    }

    if (!hasTs) {
        fullTsMin_ = 0;
        fullTsMax_ = 1;
    } else if (fullTsMax_ <= fullTsMin_) {
        fullTsMax_ = fullTsMin_ + 1000000;
    }
    if (tsMax_ <= tsMin_) {
        tsMin_ = fullTsMin_;
        tsMax_ = fullTsMax_;
    }
}

void BookTickerCompareController::initializeViewportIfNeeded_() noexcept {
    if (!lowerPaneState_.hasData || viewportInitialized_) return;
    tsMin_ = fullTsMin_;
    tsMax_ = fullTsMax_;
    viewportInitialized_ = true;
    emit viewportChanged();
}

QString BookTickerCompareController::comparisonReadyStatus_() const {
    QString status = QStringLiteral("Comparison ready: %1").arg(lowerPaneState_.title);
    const QString health = sourceHealthStatus_();
    if (!health.isEmpty()) status += QStringLiteral(" | %1").arg(health);
    return status;
}

QString BookTickerCompareController::sourceHealthStatus_() const {
    QStringList parts;
    if (!primary_.healthLabel.isEmpty()) parts.push_back(QStringLiteral("A %1").arg(primary_.healthLabel));
    if (!secondary_.healthLabel.isEmpty()) parts.push_back(QStringLiteral("B %1").arg(secondary_.healthLabel));
    return parts.isEmpty() ? QString{} : QStringLiteral("source %1").arg(parts.join(QStringLiteral("; ")));
}

void BookTickerCompareController::setStatus_(const QString& statusText) {
    if (statusText_ == statusText) return;
    statusText_ = statusText;
    emit statusChanged();
}

}  // namespace hftrec::gui::viewer










