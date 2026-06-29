#include "gui/viewer/MoexBasisController.hpp"

#include <QDir>
#include <QFileInfo>
#include <QVariantMap>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>
#include <map>
#include <sstream>
#include <string>
#include <string_view>

#include "core/arbitrage/PriceBasis.hpp"
#include "core/capture/SessionManifest.hpp"
#include "core/corpus/InstrumentMetadata.hpp"
#include "core/recordings/RecordingDiscovery.hpp"
#include "core/recordings/RecordingRoot.hpp"
#include "core/replay/SessionReplay.hpp"

namespace hftrec::gui::viewer {
namespace {

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::in | std::ios::binary);
    if (!in) return {};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

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

bool readSessionManifest(const std::filesystem::path& sessionPath,
                         hftrec::capture::SessionManifest& manifest) {
    const std::string text = readFile(sessionPath / "manifest.json");
    return !text.empty() && isOk(hftrec::capture::parseManifestJson(text, manifest));
}

std::filesystem::path existingSessionFile(const std::filesystem::path& sessionPath,
                                          std::string_view manifestPath,
                                          std::string_view fallback) {
    if (!manifestPath.empty()) {
        std::filesystem::path path{std::string{manifestPath}};
        if (path.is_relative()) path = sessionPath / path;
        std::error_code ec;
        if (std::filesystem::exists(path, ec) && !ec) return path;
    }
    const auto fallbackPath = sessionPath / std::string{fallback};
    std::error_code ec;
    return std::filesystem::exists(fallbackPath, ec) && !ec ? fallbackPath : std::filesystem::path{};
}

std::filesystem::path candlePathFor(const std::filesystem::path& sessionPath,
                                    const hftrec::capture::SessionManifest& manifest,
                                    bool& detailed) {
    detailed = true;
    auto path = existingSessionFile(sessionPath, manifest.candles2Path, "jsonl/candles2.jsonl");
    if (!path.empty()) return path;
    path = existingSessionFile(sessionPath, {}, "jsonl/candlesv2.jsonl");
    if (!path.empty()) return path;
    detailed = false;
    return existingSessionFile(sessionPath, manifest.candlesPath, "jsonl/candles.jsonl");
}

bool loadMetadata(const std::filesystem::path& sessionPath,
                  std::int64_t& priceBasisQtyE8,
                  std::int64_t& expiryUtcNs) {
    hftrec::corpus::InstrumentMetadata metadata{};
    const std::string text = readFile(sessionPath / "instrument_metadata.json");
    if (text.empty() || !isOk(hftrec::corpus::parseInstrumentMetadataJson(text, metadata))) return false;
    if (metadata.priceBasisQtyE8.has_value()) priceBasisQtyE8 = *metadata.priceBasisQtyE8;
    if (metadata.expiryUtcNs.has_value()) expiryUtcNs = *metadata.expiryUtcNs;
    return true;
}

MoexBasisController::LegState loadLeg(const hftrec::recordings::RecordedSessionInfo& session,
                                      const QString& role) {
    MoexBasisController::LegState leg{};
    leg.role = role;
    leg.label = QStringLiteral("%1/%2 %3")
                    .arg(QString::fromStdString(session.exchange),
                         QString::fromStdString(session.market),
                         QString::fromStdString(session.symbols.empty() ? session.normalizedSymbol : session.symbols.front()));
    leg.symbol = QString::fromStdString(session.symbols.empty() ? session.normalizedSymbol : session.symbols.front());
    leg.exchange = QString::fromStdString(session.exchange);
    leg.market = QString::fromStdString(session.market);
    leg.sessionPath = cleanPath(session.path);
    leg.priceBasisQtyE8 = 100000000LL;
    leg.metadataReady = role == QStringLiteral("spot");

    const std::filesystem::path path = session.path;
    (void)loadMetadata(path, leg.priceBasisQtyE8, leg.expiryUtcNs);
    if (role == QStringLiteral("future")) {
        leg.metadataReady = leg.priceBasisQtyE8 > 0 && leg.expiryUtcNs > 0;
    }

    hftrec::capture::SessionManifest manifest{};
    if (!readSessionManifest(path, manifest)) {
        leg.status = QStringLiteral("missing manifest");
        return leg;
    }

    bool detailed = true;
    const auto candlePath = candlePathFor(path, manifest, detailed);
    if (candlePath.empty()) {
        leg.status = QStringLiteral("missing candles");
        return leg;
    }

    hftrec::replay::SessionReplay replay{};
    const auto status = detailed ? replay.addCandles2File(candlePath) : replay.addCandlesFile(candlePath);
    if (!isOk(status)) {
        leg.status = QStringLiteral("failed to load candles");
        return leg;
    }
    const auto& rows = detailed ? replay.candles2() : replay.candles();
    leg.candles = selectMoexBasisCandles(rows);
    if (leg.candles.empty()) leg.status = QStringLiteral("no valid candles");
    else if (!leg.metadataReady) leg.status = QStringLiteral("missing expiry/price basis");
    else leg.status = QStringLiteral("%1 candles").arg(static_cast<qulonglong>(leg.candles.size()));
    return leg;
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
    append(QStringLiteral("front_rank"), QStringLiteral("Front rank"), QStringLiteral("continuous F1/F2"));
    append(QStringLiteral("checked_raw"), QStringLiteral("Checked raw"), QStringLiteral("selected contracts"));
    append(QStringLiteral("all_raw"), QStringLiteral("All raw"), QStringLiteral("all contracts"));
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

    for (const auto& session : selected->sessions) {
        const QString market = QString::fromStdString(session.market);
        if (spot_.label.isEmpty() && isSpotMarket(market)) {
            spot_ = loadLeg(session, QStringLiteral("spot"));
            continue;
        }
    }
    for (const auto& session : selected->sessions) {
        const QString market = QString::fromStdString(session.market);
        if (isFutureMarket(market)) futures_.push_back(loadLeg(session, QStringLiteral("future")));
    }
    std::sort(futures_.begin(), futures_.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.expiryUtcNs != rhs.expiryUtcNs) return lhs.expiryUtcNs < rhs.expiryUtcNs;
        return lhs.symbol < rhs.symbol;
    });

    rebuildBasis_();
    rebuildRenderFutures_();
    updateFullRange_();
    if (spot_.label.isEmpty()) {
        setStatus_(QStringLiteral("Selected group has no spot leg"));
    } else if (futures_.empty()) {
        setStatus_(QStringLiteral("Selected group has no futures legs"));
    } else if (enabledFutureCount() == 0) {
        setStatus_(QStringLiteral("No valid futures basis lines; check expiry and price_basis metadata"));
    } else {
        setStatus_(QStringLiteral("MOEX basis ready: %1 futures, %2 points")
                       .arg(enabledFutureCount())
                       .arg(basisPointCount()));
    }
    emit dataChanged();
    emit viewportChanged();
    return ready();
}

void MoexBasisController::clear() {
    groupPath_.clear();
    spot_ = {};
    futures_.clear();
    renderFutures_.clear();
    fullTsMin_ = 0;
    fullTsMax_ = 1;
    tsMin_ = 0;
    tsMax_ = 1;
    resetValueScale_();
    setStatus_(QStringLiteral("Select a MOEX basis group"));
    emit dataChanged();
    emit viewportChanged();
}

void MoexBasisController::setFutureEnabled(int index, bool enabled) {
    if (index < 0 || index >= static_cast<int>(futures_.size())) return;
    auto& future = futures_[static_cast<std::size_t>(index)];
    if (future.enabled == enabled) return;
    future.enabled = enabled;
    rebuildRenderFutures_();
    updateFullRange_();
    setStatus_(QStringLiteral("MOEX basis ready: %1 futures, %2 points")
                   .arg(enabledFutureCount())
                   .arg(basisPointCount()));
    emit dataChanged();
    emit viewportChanged();
}

void MoexBasisController::setDisplayMode(const QString& mode) {
    const QString next = [&]() {
        const QString value = mode.trimmed().toLower();
        if (value == QStringLiteral("all_raw") ||
            value == QStringLiteral("checked_raw") ||
            value == QStringLiteral("front_rank")) {
            return value;
        }
        return QStringLiteral("front_rank");
    }();
    if (displayMode_ == next) return;
    displayMode_ = next;
    rebuildRenderFutures_();
    updateFullRange_();
    setStatus_(QStringLiteral("MOEX basis ready: %1 futures, %2 points")
                   .arg(enabledFutureCount())
                   .arg(basisPointCount()));
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
        setStatus_(QStringLiteral("MOEX basis ready: %1 futures, %2 points")
                       .arg(enabledFutureCount())
                       .arg(basisPointCount()));
        emit dataChanged();
        emit viewportChanged();
    } else {
        emit dataChanged();
    }
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
    qint64 nextMin = tsMin_ + delta;
    qint64 nextMax = tsMax_ + delta;
    if (nextMin < fullTsMin_) {
        nextMax += fullTsMin_ - nextMin;
        nextMin = fullTsMin_;
    }
    if (nextMax > fullTsMax_) {
        nextMin -= nextMax - fullTsMax_;
        nextMax = fullTsMax_;
    }
    if (nextMax <= nextMin) return;
    tsMin_ = nextMin;
    tsMax_ = nextMax;
    emit viewportChanged();
}

void MoexBasisController::zoomTimeAt(double factor, double anchorFraction) {
    if (!std::isfinite(factor) || factor <= 0.0 || fullTsMax_ <= fullTsMin_) return;
    anchorFraction = std::clamp(anchorFraction, 0.0, 1.0);
    const double currentSpan = static_cast<double>(tsMax_ - tsMin_);
    const double nextSpan = std::clamp(currentSpan / factor, 1000000.0, static_cast<double>(fullTsMax_ - fullTsMin_));
    const double anchor = static_cast<double>(tsMin_) + currentSpan * anchorFraction;
    qint64 nextMin = static_cast<qint64>(anchor - nextSpan * anchorFraction);
    qint64 nextMax = static_cast<qint64>(static_cast<double>(nextMin) + nextSpan);
    if (nextMin < fullTsMin_) {
        nextMax += fullTsMin_ - nextMin;
        nextMin = fullTsMin_;
    }
    if (nextMax > fullTsMax_) {
        nextMin -= nextMax - fullTsMax_;
        nextMax = fullTsMax_;
    }
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

    QString previousSymbol;
    for (auto& [ts, candidates] : byTs) {
        std::sort(candidates.begin(), candidates.end(), [](const Candidate& lhs, const Candidate& rhs) {
            if (lhs.leg->expiryUtcNs != rhs.leg->expiryUtcNs) return lhs.leg->expiryUtcNs < rhs.leg->expiryUtcNs;
            return lhs.leg->symbol < rhs.leg->symbol;
        });
        if (rank < 1 || rank > static_cast<int>(candidates.size())) continue;
        const Candidate& selected = candidates[static_cast<std::size_t>(rank - 1)];
        leg.candles.push_back(normalizedCandle(*selected.candle, leg.symbol, selected.leg->priceBasisQtyE8));
        leg.expiryUtcNs = std::max(leg.expiryUtcNs == std::numeric_limits<std::int64_t>::max() / 4 ? 0 : leg.expiryUtcNs,
                                  selected.leg->expiryUtcNs);
        if (!previousSymbol.isEmpty() && previousSymbol != selected.leg->symbol) {
            LegState::RollMarker marker;
            marker.tsNs = ts;
            marker.label = selected.leg->symbol;
            leg.rollMarkers.push_back(std::move(marker));
        }
        previousSymbol = selected.leg->symbol;
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

void MoexBasisController::updateFullRange_() noexcept {
    bool hasTs = false;
    auto absorb = [&](qint64 ts) noexcept {
        if (ts <= 0) return;
        if (!hasTs) {
            fullTsMin_ = ts;
            fullTsMax_ = ts;
            hasTs = true;
            return;
        }
        if (ts < fullTsMin_) fullTsMin_ = ts;
        if (ts > fullTsMax_) fullTsMax_ = ts;
    };
    for (const auto& row : spot_.candles) absorb(row.tsNs);
    for (const auto& future : renderFutures_) {
        if (!future.enabled) continue;
        for (const auto& point : future.basisPoints) absorb(point.tsNs);
        if (future.expiryUtcNs > 0) absorb(future.expiryUtcNs);
    }
    if (!hasTs) {
        fullTsMin_ = 0;
        fullTsMax_ = 1;
    } else if (fullTsMax_ <= fullTsMin_) {
        fullTsMax_ = fullTsMin_ + 1000000;
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
