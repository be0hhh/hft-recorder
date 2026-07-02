#include "gui/viewer/MoexBasisController.hpp"

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QStringList>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <string>
#include <system_error>
#include <utility>

#include "core/arbitrage/PriceBasis.hpp"
#include "core/recordings/RecordingDiscovery.hpp"
#include "core/recordings/RecordingRoot.hpp"
#include "core/recordings/BasisChainSeries.hpp"
#include "gui/backtests/BacktestSessionSummary.hpp"
#include "gui/viewer/moex/MoexBasisDataLoad.hpp"

namespace hftrec::gui::viewer {
namespace {

QString cleanPath(const std::filesystem::path& path) {
    return QDir::cleanPath(QString::fromStdString(path.string()));
}

QString lower(QString value) {
    return value.trimmed().toLower();
}

bool isSpotMarket(const QString& market) {
    const QString m = lower(market);
    return m.contains(QStringLiteral("spot")) || m.contains(QStringLiteral("share")) ||
           m.contains(QStringLiteral("stock")) || m.contains(QStringLiteral("misx"));
}

bool isFutureMarket(const QString& market) {
    const QString m = lower(market);
    return m.contains(QStringLiteral("future")) || m.contains(QStringLiteral("forts")) ||
           m.contains(QStringLiteral("rtsx"));
}

std::int64_t normalizePrice(std::int64_t nativePriceE8, std::int64_t priceBasisQtyE8) noexcept {
    return hftrec::arbitrage::normalizeNativePriceE8(nativePriceE8, priceBasisQtyE8 <= 0 ? 100000000LL : priceBasisQtyE8);
}

hftrec::replay::CandleRow normalizedCandle(const hftrec::replay::CandleRow& row,
                                           const QString& symbol,
                                           std::int64_t priceBasisQtyE8) {
    hftrec::replay::CandleRow out = row;
    out.symbol = symbol.toStdString();
    out.openE8 = normalizePrice(row.openE8, priceBasisQtyE8);
    out.highE8 = normalizePrice(row.highE8, priceBasisQtyE8);
    out.lowE8 = normalizePrice(row.lowE8, priceBasisQtyE8);
    out.closeE8 = normalizePrice(moexBasisClosePriceE8(row), priceBasisQtyE8);
    out.hasOhlc = out.openE8 > 0 && out.highE8 > 0 && out.lowE8 > 0 && out.closeE8 > 0;
    return out;
}

QJsonObject readJsonObject(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    return doc.isObject() ? doc.object() : QJsonObject{};
}

double clampZoom(double value) noexcept {
    if (!std::isfinite(value) || value < 1.0) return 1.0;
    return value > 4096.0 ? 4096.0 : value;
}

double clampPan(double pan, double zoom) noexcept {
    zoom = clampZoom(zoom);
    const double limit = zoom <= 1.0 ? 0.0 : (1.0 - (1.0 / zoom)) * 0.5;
    if (pan < -limit) return -limit;
    if (pan > limit) return limit;
    return pan;
}

}  // namespace

MoexBasisController::MoexBasisController(QObject* parent)
    : QObject(parent) {
    rebuildGroupRows_();
}

QVariantList MoexBasisController::legRows() const {
    QVariantList rows;
    if (!spot_.label.isEmpty()) {
        QVariantMap spot;
        spot.insert(QStringLiteral("role"), QStringLiteral("spot"));
        spot.insert(QStringLiteral("label"), spot_.label);
        spot.insert(QStringLiteral("rightText"), spot_.status);
        spot.insert(QStringLiteral("enabled"), true);
        spot.insert(QStringLiteral("selectable"), false);
        rows.push_back(spot);
    }
    for (std::size_t i = 0; i < futures_.size(); ++i) {
        const auto& future = futures_[i];
        QVariantMap row;
        row.insert(QStringLiteral("role"), QStringLiteral("future"));
        row.insert(QStringLiteral("futureIndex"), static_cast<int>(i));
        row.insert(QStringLiteral("label"), future.label);
        row.insert(QStringLiteral("symbol"), future.symbol);
        row.insert(QStringLiteral("rightText"), future.status);
        row.insert(QStringLiteral("enabled"), future.enabled);
        row.insert(QStringLiteral("valid"), future.metadataReady && !future.candles.empty());
        row.insert(QStringLiteral("basisPoints"), static_cast<int>(future.basisPoints.size()));
        rows.push_back(row);
    }
    return rows;
}

QVariantList MoexBasisController::displayModeChoices() const {
    QVariantList rows;
    auto append = [&](const QString& id, const QString& label, const QString& rightText) {
        QVariantMap row;
        row.insert(QStringLiteral("id"), id);
        row.insert(QStringLiteral("value"), id);
        row.insert(QStringLiteral("label"), label);
        row.insert(QStringLiteral("rightText"), rightText);
        rows.push_back(row);
    };
    append(QStringLiteral("checked_raw"), QStringLiteral("Checked futures"), QStringLiteral("selected contracts"));
    append(QStringLiteral("all_raw"), QStringLiteral("All futures"), QStringLiteral("all contracts"));
    return rows;
}

int MoexBasisController::enabledFutureCount() const noexcept {
    int count = 0;
    for (const auto& future : renderFutures_) {
        if (future.enabled && future.metadataReady && !future.candles.empty() && !future.basisPoints.empty()) ++count;
    }
    return count;
}

int MoexBasisController::basisPointCount() const noexcept {
    int count = 0;
    for (const auto& future : renderFutures_) {
        if (!future.enabled) continue;
        count += static_cast<int>(future.basisPoints.size());
    }
    return count;
}

int MoexBasisController::activeFutureCandleCount() const noexcept {
    int count = 0;
    for (const auto& future : renderFutures_) {
        if (!future.enabled || !future.metadataReady) continue;
        count += static_cast<int>(future.candles.size());
    }
    return count;
}

bool MoexBasisController::hasFutureConflicts() const {
    return !futureConflicts_().empty();
}

QVariantList MoexBasisController::conflictRows() const {
    QVariantList rows;
    for (const auto& conflict : futureConflicts_()) {
        QVariantMap row;
        row.insert(QStringLiteral("expiryUtcNs"), static_cast<qint64>(conflict.expiryUtcNs));
        QStringList symbols;
        for (const auto& symbol : conflict.symbols) symbols.push_back(QString::fromStdString(symbol));
        row.insert(QStringLiteral("symbols"), symbols.join(QStringLiteral(", ")));
        row.insert(QStringLiteral("rightText"), QStringLiteral("same expiry"));
        rows.push_back(row);
    }
    return rows;
}

QVariantList MoexBasisController::enabledFutureSessionPaths() const {
    QVariantList rows;
    for (const auto& future : futures_) {
        if (!future.enabled || !future.metadataReady || future.candles.empty() || future.sessionPath.trimmed().isEmpty()) continue;
        rows.push_back(future.sessionPath);
    }
    return rows;
}

void MoexBasisController::reloadGroups() {
    rebuildGroupRows_();
}

bool MoexBasisController::loadGroup(const QString& path) {
    const QString requested = QDir::cleanPath(path);
    if (requested.trimmed().isEmpty()) {
        clear();
        return false;
    }

    const auto discovery = hftrec::recordings::discoverRecordings(hftrec::recordings::defaultRecordingsRoot());
    const hftrec::recordings::RecordingGroupInfo* selected = nullptr;
    for (const auto& group : discovery.groups) {
        if (cleanPath(group.path) == requested) {
            selected = &group;
            break;
        }
    }
    if (selected == nullptr) {
        setStatus_(QStringLiteral("Basis group not found"));
        return false;
    }

    spot_ = {};
    futures_.clear();
    groupPath_ = requested;
    resetValueScale_();

    const std::filesystem::path selectedPath = selected->path;
    std::vector<hftrec::recordings::BasisChainSeriesRow> seriesRows;
    std::string seriesError;
    std::error_code seriesEc;
    const bool hasSeriesFile = std::filesystem::is_regular_file(selectedPath / "basis_chain_series.jsonl", seriesEc) && !seriesEc;
    const bool useSeriesRows = hasSeriesFile &&
        hftrec::recordings::readBasisChainSeries(selectedPath, seriesRows, &seriesError) &&
        !seriesRows.empty();
    const moex::LegLoadMode legLoadMode = useSeriesRows ? moex::LegLoadMode::MetadataOnly : moex::LegLoadMode::FullCandles;

    for (const auto& session : selected->sessions) {
        const QString market = QString::fromStdString(session.market);
        if (spot_.label.isEmpty() && isSpotMarket(market)) {
            spot_ = moex::loadLeg(session, QStringLiteral("spot"), legLoadMode);
            continue;
        }
    }
    for (const auto& session : selected->sessions) {
        const QString market = QString::fromStdString(session.market);
        if (isFutureMarket(market)) futures_.push_back(moex::loadLeg(session, QStringLiteral("future"), legLoadMode));
    }
    std::sort(futures_.begin(), futures_.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.expiryUtcNs != rhs.expiryUtcNs) return lhs.expiryUtcNs < rhs.expiryUtcNs;
        return lhs.symbol < rhs.symbol;
    });

    bool seriesApplied = false;
    if (useSeriesRows) {
        seriesApplied = moex::applyBasisChainSeriesRows(seriesRows, spot_, futures_);
        if (!seriesApplied) {
            spot_ = {};
            futures_.clear();
            for (const auto& session : selected->sessions) {
                const QString market = QString::fromStdString(session.market);
                if (spot_.label.isEmpty() && isSpotMarket(market)) {
                    spot_ = moex::loadLeg(session, QStringLiteral("spot"), moex::LegLoadMode::FullCandles);
                    continue;
                }
            }
            for (const auto& session : selected->sessions) {
                const QString market = QString::fromStdString(session.market);
                if (isFutureMarket(market)) futures_.push_back(moex::loadLeg(session, QStringLiteral("future"), moex::LegLoadMode::FullCandles));
            }
            std::sort(futures_.begin(), futures_.end(), [](const auto& lhs, const auto& rhs) {
                if (lhs.expiryUtcNs != rhs.expiryUtcNs) return lhs.expiryUtcNs < rhs.expiryUtcNs;
                return lhs.symbol < rhs.symbol;
            });
        }
    }
    rebuildBasis_();
    rebuildRenderFutures_();
    updateFullRange_();
    reloadStrategyResults_();
    if (spot_.label.isEmpty()) {
        setStatus_(QStringLiteral("Selected group has no spot leg"));
    } else if (futures_.empty()) {
        setStatus_(QStringLiteral("Selected group has no futures legs"));
    } else if (hasFutureConflicts()) {
        setStatus_(QStringLiteral("Resolve duplicate-expiry futures before running strategy"));
    } else if (enabledFutureCount() == 0) {
        if (hasSeriesFile && !useSeriesRows) {
            setStatus_(QStringLiteral("Series load failed, fallback candles used: %1")
                           .arg(QString::fromStdString(seriesError.empty() ? std::string{"empty series"} : seriesError)));
        } else {
            setStatus_(QStringLiteral("No valid futures basis lines; check expiry and price_basis metadata"));
        }
    } else {
        setStatus_(QStringLiteral("MOEX basis ready: %1 futures, %2 points%3")
                       .arg(enabledFutureCount())
                       .arg(basisPointCount())
                       .arg(seriesApplied ? QStringLiteral(" | series") : QString{}));
    }
    emit dataChanged();
    emit viewportChanged();
    emit strategyResultsChanged();
    return ready();
}

void MoexBasisController::clear() {
    groupPath_.clear();
    spot_ = {};
    futures_.clear();
    renderFutures_.clear();
    strategyResultRows_.clear();
    selectedStrategyResult_.clear();
    strategyOverlay_ = {};
    strategyStatusText_ = QStringLiteral("No strategy result selected");
    fullTsMin_ = 0;
    fullTsMax_ = 1;
    tsMin_ = 0;
    tsMax_ = 1;
    resetValueScale_();
    setStatus_(QStringLiteral("Select a MOEX basis group"));
    emit dataChanged();
    emit viewportChanged();
    emit strategyResultsChanged();
}

void MoexBasisController::setFutureEnabled(int index, bool enabled) {
    if (index < 0 || index >= static_cast<int>(futures_.size())) return;
    auto& future = futures_[static_cast<std::size_t>(index)];
    if (future.enabled == enabled) return;
    future.enabled = enabled;
    rebuildRenderFutures_();
    updateFullRange_();
    reloadStrategyResults_();
    setReadyStatus_();
    emit dataChanged();
    emit viewportChanged();
    emit strategyResultsChanged();
}

void MoexBasisController::setDisplayMode(const QString& mode) {
    const QString next = [&]() {
        const QString value = mode.trimmed().toLower();
        if (value == QStringLiteral("all_raw") ||
            value == QStringLiteral("checked_raw")) {
            return value;
        }
        return QStringLiteral("checked_raw");
    }();
    if (displayMode_ == next) return;
    displayMode_ = next;
    rebuildRenderFutures_();
    updateFullRange_();
    setReadyStatus_();
    emit dataChanged();
    emit viewportChanged();
}

void MoexBasisController::setFrontRank(int rank) {
    const int next = std::clamp(rank, 1, 16);
    if (frontRank_ == next) return;
    frontRank_ = next;
    if (displayMode_ == QStringLiteral("front_rank")) {
        rebuildRenderFutures_();
        updateFullRange_();
        setReadyStatus_();
        emit dataChanged();
        emit viewportChanged();
    } else {
        emit dataChanged();
    }
}

void MoexBasisController::reloadStrategyResults() {
    reloadStrategyResults_();
    emit strategyResultsChanged();
}

void MoexBasisController::setStrategyResult(const QString& path) {
    const QString requested = QDir::cleanPath(path);
    if (requested.trimmed().isEmpty()) {
        clearStrategyResult_(QStringLiteral("No strategy result selected"));
        emit strategyResultsChanged();
        emit dataChanged();
        return;
    }

    bool listed = false;
    for (const QVariant& value : strategyResultRows_) {
        if (value.toMap().value(QStringLiteral("id")).toString() == requested) {
            listed = true;
            break;
        }
    }
    if (!listed) {
        reloadStrategyResults_();
        for (const QVariant& value : strategyResultRows_) {
            if (value.toMap().value(QStringLiteral("id")).toString() == requested) {
                listed = true;
                break;
            }
        }
    }
    if (!listed) {
        clearStrategyResult_(QStringLiteral("Strategy result does not match selected spot/future"));
        emit strategyResultsChanged();
        emit dataChanged();
        return;
    }

    StrategyOverlayData overlay;
    std::string error;
    if (!loadStrategyOverlayFromResult(std::filesystem::path{requested.toStdString()},
                                       static_cast<std::int64_t>(tsMax_),
                                       overlay,
                                       error)) {
        clearStrategyResult_(QStringLiteral("Strategy load failed: %1").arg(QString::fromStdString(error)));
        emit strategyResultsChanged();
        emit dataChanged();
        return;
    }
    if (overlay.spreadPoints.empty()) {
        clearStrategyResult_(QStringLiteral("Strategy result has no strategy_spread rows"));
        emit strategyResultsChanged();
        emit dataChanged();
        return;
    }

    selectedStrategyResult_ = requested;
    strategyOverlay_ = std::move(overlay);
    strategyStatusText_ = QStringLiteral("%1 | spread points %2")
        .arg(strategyOverlay_.strategy.isEmpty() ? QStringLiteral("strategy") : strategyOverlay_.strategy)
        .arg(static_cast<qulonglong>(strategyOverlay_.spreadPoints.size()));
    emit strategyResultsChanged();
    emit dataChanged();
}

void MoexBasisController::clearStrategyResult() {
    clearStrategyResult_(QStringLiteral("No strategy result selected"));
    emit strategyResultsChanged();
    emit dataChanged();
}

void MoexBasisController::autoFit() {
    tsMin_ = fullTsMin_;
    tsMax_ = fullTsMax_;
    resetValueScale_();
    emit viewportChanged();
    emit dataChanged();
}

void MoexBasisController::panTime(double fraction) {
    const qint64 span = tsMax_ - tsMin_;
    if (span <= 0) return;
    const qint64 delta = static_cast<qint64>(static_cast<double>(span) * fraction);
    const qint64 nextMin = tsMin_ + delta;
    const qint64 nextMax = tsMax_ + delta;
    if (nextMax <= nextMin) return;
    tsMin_ = nextMin;
    tsMax_ = nextMax;
    emit viewportChanged();
}

void MoexBasisController::zoomTimeAt(double factor, double anchorFraction) {
    if (!std::isfinite(factor) || factor <= 0.0) return;
    anchorFraction = std::clamp(anchorFraction, 0.0, 1.0);
    const double currentSpan = static_cast<double>(tsMax_ - tsMin_);
    if (currentSpan <= 0.0) return;
    const double nextSpan = std::max(currentSpan / factor, 1000000.0);
    const double anchor = static_cast<double>(tsMin_) + currentSpan * anchorFraction;
    qint64 nextMin = static_cast<qint64>(anchor - nextSpan * anchorFraction);
    qint64 nextMax = static_cast<qint64>(static_cast<double>(nextMin) + nextSpan);
    if (nextMax <= nextMin) return;
    tsMin_ = nextMin;
    tsMax_ = nextMax;
    emit viewportChanged();
}

void MoexBasisController::panPrice(double fraction) {
    pricePan_ = clampPan(pricePan_ + fraction / clampZoom(priceZoom_), priceZoom_);
    emit viewportChanged();
}

void MoexBasisController::zoomPriceAt(double factor, double anchorFraction) {
    (void)anchorFraction;
    priceZoom_ = clampZoom(priceZoom_ * factor);
    pricePan_ = clampPan(pricePan_, priceZoom_);
    emit viewportChanged();
}

void MoexBasisController::panBasis(double fraction) {
    basisPan_ = clampPan(basisPan_ + fraction / clampZoom(basisZoom_), basisZoom_);
    emit viewportChanged();
}

void MoexBasisController::zoomBasisAt(double factor, double anchorFraction) {
    (void)anchorFraction;
    basisZoom_ = clampZoom(basisZoom_ * factor);
    basisPan_ = clampPan(basisPan_, basisZoom_);
    emit viewportChanged();
}

void MoexBasisController::setStatus_(const QString& statusText) {
    if (statusText_ == statusText) return;
    statusText_ = statusText;
    emit statusChanged();
}

void MoexBasisController::rebuildGroupRows_() {
    QVariantList rows;
    const auto discovery = hftrec::recordings::discoverRecordings(hftrec::recordings::defaultRecordingsRoot());
    for (const auto& group : discovery.groups) {
        int spotCount = 0;
        int futureCount = 0;
        for (const auto& session : group.sessions) {
            const QString market = QString::fromStdString(session.market);
            if (isSpotMarket(market)) ++spotCount;
            if (isFutureMarket(market)) ++futureCount;
        }
        const bool hasBasisManifest = QFileInfo::exists(QDir(cleanPath(group.path)).absoluteFilePath(QStringLiteral("basis_chain_manifest.json")));
        if (!hasBasisManifest && (spotCount == 0 || futureCount == 0)) continue;

        QVariantMap row;
        row.insert(QStringLiteral("id"), cleanPath(group.path));
        row.insert(QStringLiteral("label"), QString::fromStdString(group.title.empty() ? group.id : group.title));
        row.insert(QStringLiteral("rightText"), QStringLiteral("spot %1 | fut %2").arg(spotCount).arg(futureCount));
        row.insert(QStringLiteral("selectable"), true);
        row.insert(QStringLiteral("isGroup"), false);
        row.insert(QStringLiteral("groupPath"), cleanPath(group.path));
        row.insert(QStringLiteral("searchText"), QStringLiteral("%1 %2").arg(QString::fromStdString(group.id), cleanPath(group.path)));
        rows.push_back(row);
    }
    groupRows_ = rows;
    emit groupsChanged();
}

void MoexBasisController::rebuildBasis_() {
    MoexBasisLegSeries spotSeries{};
    spotSeries.candles = spot_.candles;
    spotSeries.priceBasisQtyE8 = spot_.priceBasisQtyE8;
    spotSeries.expiryUtcNs = spot_.expiryUtcNs;
    for (auto& future : futures_) {
        MoexBasisLegSeries futureSeries{};
        futureSeries.candles = future.candles;
        futureSeries.priceBasisQtyE8 = future.priceBasisQtyE8;
        futureSeries.expiryUtcNs = future.expiryUtcNs;
        future.basisPoints = buildMoexBasisPoints(spotSeries, futureSeries);
    }
}

MoexBasisController::LegState MoexBasisController::buildFrontRankLeg_(int rank) const {
    LegState leg{};
    leg.role = QStringLiteral("future");
    leg.symbol = QStringLiteral("F%1").arg(rank);
    leg.label = QStringLiteral("F%1 continuous").arg(rank);
    leg.exchange = spot_.exchange;
    leg.market = QStringLiteral("futures");
    leg.enabled = true;
    leg.priceBasisQtyE8 = 100000000LL;
    leg.metadataReady = true;
    leg.expiryUtcNs = std::numeric_limits<std::int64_t>::max() / 4;

    struct Candidate {
        const LegState* leg{nullptr};
        const hftrec::replay::CandleRow* candle{nullptr};
    };
    std::map<std::int64_t, std::vector<Candidate>> byTs;
    for (const auto& future : futures_) {
        if (!future.enabled || !future.metadataReady || future.expiryUtcNs <= 0 || future.priceBasisQtyE8 <= 0) continue;
        for (const auto& candle : future.candles) {
            if (candle.tsNs <= 0 || future.expiryUtcNs <= candle.tsNs || moexBasisClosePriceE8(candle) <= 0) continue;
            byTs[candle.tsNs].push_back(Candidate{&future, &candle});
        }
    }

    for (auto& entry : byTs) {
        auto& candidates = entry.second;
        std::sort(candidates.begin(), candidates.end(), [](const Candidate& lhs, const Candidate& rhs) {
            if (lhs.leg->expiryUtcNs != rhs.leg->expiryUtcNs) return lhs.leg->expiryUtcNs < rhs.leg->expiryUtcNs;
            return lhs.leg->symbol < rhs.leg->symbol;
        });
        if (rank < 1 || rank > static_cast<int>(candidates.size())) continue;
        const Candidate& selected = candidates[static_cast<std::size_t>(rank - 1)];
        leg.candles.push_back(normalizedCandle(*selected.candle, leg.symbol, selected.leg->priceBasisQtyE8));
        leg.expiryUtcNs = std::max(leg.expiryUtcNs == std::numeric_limits<std::int64_t>::max() / 4 ? 0 : leg.expiryUtcNs,
                                  selected.leg->expiryUtcNs);
    }

    MoexBasisLegSeries spotSeries{};
    spotSeries.candles = spot_.candles;
    spotSeries.priceBasisQtyE8 = spot_.priceBasisQtyE8;
    spotSeries.expiryUtcNs = spot_.expiryUtcNs;
    MoexBasisLegSeries futureSeries{};
    futureSeries.candles = leg.candles;
    futureSeries.priceBasisQtyE8 = leg.priceBasisQtyE8;
    futureSeries.expiryUtcNs = leg.expiryUtcNs;
    leg.basisPoints = buildMoexBasisPoints(spotSeries, futureSeries);
    leg.status = QStringLiteral("%1 candles").arg(static_cast<qulonglong>(leg.candles.size()));
    if (leg.candles.empty() || leg.basisPoints.empty()) leg.metadataReady = false;
    return leg;
}

void MoexBasisController::rebuildRenderFutures_() {
    renderFutures_.clear();
    if (displayMode_ == QStringLiteral("front_rank")) {
        LegState leg = buildFrontRankLeg_(frontRank_);
        if (!leg.candles.empty()) renderFutures_.push_back(std::move(leg));
        return;
    }
    renderFutures_.reserve(futures_.size());
    for (const auto& future : futures_) {
        if (displayMode_ == QStringLiteral("checked_raw") && !future.enabled) continue;
        renderFutures_.push_back(future);
    }
}

std::vector<MoexBasisFutureConflict> MoexBasisController::futureConflicts_() const {
    std::vector<MoexBasisFutureConflictInput> inputs;
    inputs.reserve(futures_.size());
    for (const auto& future : futures_) {
        inputs.push_back(MoexBasisFutureConflictInput{
            future.symbol.toStdString(),
            future.expiryUtcNs,
            future.enabled,
            future.metadataReady && !future.candles.empty(),
        });
    }
    return findMoexBasisFutureConflicts(inputs);
}

void MoexBasisController::clearStrategyResult_(const QString& statusText) {
    selectedStrategyResult_.clear();
    strategyOverlay_ = {};
    strategyStatusText_ = statusText;
}

void MoexBasisController::reloadStrategyResults_() {
    QVariantList rows;
    QVariantMap none;
    none.insert(QStringLiteral("id"), QString{});
    none.insert(QStringLiteral("value"), QString{});
    none.insert(QStringLiteral("label"), QStringLiteral("No strategy"));
    none.insert(QStringLiteral("rightText"), QStringLiteral("lower spread only"));
    none.insert(QStringLiteral("selectable"), true);
    rows.push_back(none);

    if (groupPath_.trimmed().isEmpty() || spot_.sessionPath.trimmed().isEmpty() || hasFutureConflicts()) {
        strategyResultRows_ = rows;
        if (selectedStrategyResult_.isEmpty()) {
            strategyStatusText_ = hasFutureConflicts()
                ? QStringLiteral("Resolve duplicate-expiry futures before loading strategy")
                : QStringLiteral("No strategy result selected");
        } else {
            clearStrategyResult_(QStringLiteral("Strategy cleared: current futures are conflicting"));
        }
        return;
    }

    QStringList futurePaths;
    QHash<QString, QString> futureSymbolByPath;
    for (const auto& future : futures_) {
        if (!future.enabled || !future.metadataReady || future.candles.empty() || future.sessionPath.trimmed().isEmpty()) continue;
        const QString clean = QDir::cleanPath(future.sessionPath);
        if (futurePaths.contains(clean)) continue;
        futurePaths.push_back(clean);
        futureSymbolByPath.insert(clean, future.symbol);
    }

    QStringList roots;
    auto addBacktestsRoot = [&](const QString& ownerPath) {
        if (ownerPath.trimmed().isEmpty()) return;
        const QString root = QDir(QDir::cleanPath(ownerPath)).absoluteFilePath(QStringLiteral("backtests"));
        if (QFileInfo::exists(root) && !roots.contains(root)) roots.push_back(root);
    };
    addBacktestsRoot(groupPath_);
    addBacktestsRoot(spot_.sessionPath);
    for (const QString& path : futurePaths) addBacktestsRoot(path);

    QSet<QString> seenResults;
    for (const QString& root : roots) {
        QDirIterator it(root,
                        QStringList{QStringLiteral("manifest.json")},
                        QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString manifestPath = QDir::cleanPath(it.next());
            const QString resultPath = QDir::cleanPath(QFileInfo(manifestPath).absolutePath());
            if (seenResults.contains(resultPath)) continue;
            seenResults.insert(resultPath);
            if (!QFileInfo::exists(QDir(resultPath).absoluteFilePath(QStringLiteral("strategy_spread.jsonl")))) continue;

            const QJsonObject manifest = readJsonObject(manifestPath);
            if (manifest.value(QStringLiteral("type")).toString() != QStringLiteral("run.result.v2")) continue;

            QString matchedFuturePath;
            for (const QString& futurePath : futurePaths) {
                if (hftrec::gui::backtestManifestMatchesLegs(manifestPath, QStringList{spot_.sessionPath, futurePath})) {
                    matchedFuturePath = futurePath;
                    break;
                }
            }
            if (matchedFuturePath.isEmpty()) continue;

            const QString runId = manifest.value(QStringLiteral("run_id")).toString(QFileInfo(resultPath).fileName());
            const QString strategy = manifest.value(QStringLiteral("strategy")).toString();
            QVariantMap row;
            row.insert(QStringLiteral("id"), resultPath);
            row.insert(QStringLiteral("value"), resultPath);
            row.insert(QStringLiteral("label"), runId);
            row.insert(QStringLiteral("rightText"), QStringLiteral("%1 | %2")
                           .arg(strategy.isEmpty() ? QStringLiteral("strategy") : strategy,
                                futureSymbolByPath.value(matchedFuturePath)));
            row.insert(QStringLiteral("strategy"), strategy);
            row.insert(QStringLiteral("futureSessionPath"), matchedFuturePath);
            row.insert(QStringLiteral("resultPath"), resultPath);
            row.insert(QStringLiteral("searchText"), QStringLiteral("%1 %2 %3")
                           .arg(runId, strategy, resultPath));
            rows.push_back(row);
        }
    }

    bool selectedStillValid = selectedStrategyResult_.isEmpty();
    if (!selectedStrategyResult_.isEmpty()) {
        for (const QVariant& value : rows) {
            if (value.toMap().value(QStringLiteral("id")).toString() == selectedStrategyResult_) {
                selectedStillValid = true;
                break;
            }
        }
    }
    strategyResultRows_ = rows;
    if (!selectedStillValid) {
        clearStrategyResult_(QStringLiteral("Strategy cleared: result no longer matches selected futures"));
        return;
    }
    if (selectedStrategyResult_.isEmpty()) {
        strategyStatusText_ = rows.size() > 0
            ? QStringLiteral("%1 matching strategy result(s)").arg(static_cast<qulonglong>(rows.size()))
            : QStringLiteral("No matching strategy result");
    }
}

void MoexBasisController::setReadyStatus_() {
    if (spot_.label.isEmpty()) {
        setStatus_(QStringLiteral("Selected group has no spot leg"));
    } else if (futures_.empty()) {
        setStatus_(QStringLiteral("Selected group has no futures legs"));
    } else if (hasFutureConflicts()) {
        setStatus_(QStringLiteral("Resolve duplicate-expiry futures before running strategy"));
    } else if (enabledFutureCount() == 0) {
        setStatus_(QStringLiteral("No valid futures basis lines; check expiry and price_basis metadata"));
    } else {
        setStatus_(QStringLiteral("MOEX basis ready: %1 futures, %2 points")
                       .arg(enabledFutureCount())
                       .arg(basisPointCount()));
    }
}

void MoexBasisController::updateFullRange_() noexcept {
    std::vector<MoexBasisLegSeries> futureSeries;
    futureSeries.reserve(renderFutures_.size());
    for (const auto& future : renderFutures_) {
        if (!future.enabled || !future.metadataReady) continue;
        MoexBasisLegSeries series;
        series.candles = future.candles;
        futureSeries.push_back(std::move(series));
    }
    const MoexBasisTimeRange range = moexBasisLoadedTimeRange(spot_.candles, futureSeries);
    if (!range.hasData) {
        fullTsMin_ = 0;
        fullTsMax_ = 1;
    } else {
        fullTsMin_ = static_cast<qint64>(range.minTsNs);
        fullTsMax_ = static_cast<qint64>(range.maxTsNs);
    }
    tsMin_ = fullTsMin_;
    tsMax_ = fullTsMax_;
}

void MoexBasisController::resetValueScale_() noexcept {
    priceZoom_ = 1.0;
    pricePan_ = 0.0;
    basisZoom_ = 1.0;
    basisPan_ = 0.0;
}

}  // namespace hftrec::gui::viewer
