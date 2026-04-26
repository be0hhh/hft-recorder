#include "gui/viewer/ChartController.hpp"

#include <algorithm>
#include <limits>
#include <filesystem>
#include <memory>
#include <utility>
#include <chrono>

#include "core/metrics/Metrics.hpp"

namespace hftrec::gui::viewer {

namespace {

ChartController* g_activeChartController = nullptr;

}  // namespace

namespace {

std::filesystem::path providerSessionPath(const QString& path) {
    QString p = path;
    if (p.startsWith(QStringLiteral("file:///"))) p.remove(0, 8);
    else if (p.startsWith(QStringLiteral("file://"))) p.remove(0, 7);
    return std::filesystem::path{p.toStdString()};
}

bool envForcesSoftwareRenderer() {
    const auto qsgBackend = qEnvironmentVariable("QSG_RHI_BACKEND").trimmed().toLower();
    const auto quickBackend = qEnvironmentVariable("QT_QUICK_BACKEND").trimmed().toLower();
    const auto libglSoftware = qEnvironmentVariable("LIBGL_ALWAYS_SOFTWARE").trimmed().toLower();
    const auto xcbIntegration = qEnvironmentVariable("QT_XCB_GL_INTEGRATION").trimmed().toLower();

    return qsgBackend == QStringLiteral("software")
        || quickBackend == QStringLiteral("software")
        || libglSoftware == QStringLiteral("1")
        || xcbIntegration == QStringLiteral("none");
}

template <typename Row>
bool eventKeyLess(const Row& lhs, const Row& rhs) noexcept {
    if (lhs.tsNs != rhs.tsNs) return lhs.tsNs < rhs.tsNs;
    if (lhs.captureSeq != rhs.captureSeq) return lhs.captureSeq < rhs.captureSeq;
    return lhs.ingestSeq < rhs.ingestSeq;
}

template <typename Row>
bool appendMonotonicRows(const std::vector<Row>& src,
                         std::vector<Row>& dst,
                         QString* failureText,
                         QStringView label) {
    if (src.empty()) return true;
    if (!dst.empty() && eventKeyLess(src.front(), dst.back())) {
        if (failureText != nullptr) {
            *failureText = QStringLiteral("Live %1 ordering changed, full reload required").arg(label);
        }
        return false;
    }
    for (std::size_t i = 1; i < src.size(); ++i) {
        if (eventKeyLess(src[i], src[i - 1u])) {
            if (failureText != nullptr) {
                *failureText = QStringLiteral("Live %1 batch is out of order, full reload required").arg(label);
            }
            return false;
        }
    }
    dst.insert(dst.end(), src.begin(), src.end());
    return true;
}

template <typename Row>
void trimOverlayRows(std::vector<Row>& rows, std::size_t limit) {
    if (rows.size() <= limit) return;
    rows.erase(rows.begin(), rows.begin() + static_cast<std::ptrdiff_t>(rows.size() - limit));
}

template <typename Row>
void removeRowsPromotedToStable(std::vector<Row>& overlay, const std::vector<Row>& stable) {
    if (overlay.empty() || stable.empty()) return;
    overlay.erase(
        std::remove_if(
            overlay.begin(),
            overlay.end(),
            [&stable](const Row& row) {
                return std::binary_search(
                    stable.begin(),
                    stable.end(),
                    row,
                    [](const Row& lhs, const Row& rhs) noexcept { return eventKeyLess(lhs, rhs); });
            }),
        overlay.end());
}

template <typename Row>
void keepRowsInTimeRange(std::vector<Row>& rows, std::int64_t tsMin, std::int64_t tsMax) {
    rows.erase(
        std::remove_if(
            rows.begin(),
            rows.end(),
            [tsMin, tsMax](const Row& row) noexcept {
                return row.tsNs < tsMin || row.tsNs > tsMax;
            }),
        rows.end());
}

void keepBatchInTimeRange(LiveDataBatch& batch, std::int64_t tsMin, std::int64_t tsMax) {
    keepRowsInTimeRange(batch.trades, tsMin, tsMax);
    keepRowsInTimeRange(batch.bookTickers, tsMin, tsMax);
    keepRowsInTimeRange(batch.depths, tsMin, tsMax);
    keepRowsInTimeRange(batch.snapshots, tsMin, tsMax);
}

}  // namespace

ChartController::ChartController(QObject* parent)
    : QObject(parent) {
    gpuRendererAvailable_ = !envForcesSoftwareRenderer();
    liveDataProvider_ = std::make_unique<JsonTailLiveDataProvider>();
    liveDataTimer_ = new QTimer(this);
    liveDataTimer_->setInterval(liveUpdateIntervalMs_);
    liveDataTimer_->setTimerType(Qt::PreciseTimer);
    connect(liveDataTimer_, &QTimer::timeout, this, [this]() { pollLiveData_(); });
}

ChartController::~ChartController() {
    if (g_activeChartController == this) g_activeChartController = nullptr;
}

ChartController* ChartController::activeInstance() noexcept {
    return g_activeChartController;
}

void ChartController::setActive(bool active) {
    if (active_ == active) return;
    active_ = active;
    if (active_) g_activeChartController = this;
    else if (g_activeChartController == this) g_activeChartController = nullptr;
    if (liveDataTimer_ != nullptr) {
        if (active_) liveDataTimer_->start();
        else liveDataTimer_->stop();
    }
    emit activeChanged();
}

void ChartController::setLiveUpdateIntervalMs(int intervalMs) {
    const int clamped = intervalMs <= 16 ? 16
        : (intervalMs <= 100 ? 100
        : (intervalMs <= 250 ? 250 : 500));
    if (liveUpdateIntervalMs_ == clamped) return;
    liveUpdateIntervalMs_ = clamped;
    if (liveDataTimer_ != nullptr) liveDataTimer_->setInterval(liveUpdateIntervalMs_);
}

int ChartController::liveUpdateIntervalMs() const noexcept {
    return liveUpdateIntervalMs_;
}

void ChartController::setRenderWindowSeconds(int seconds) {
    const int clamped = std::clamp(seconds, -1, 86400);
    if (renderWindowSeconds_ == clamped) return;
    renderWindowSeconds_ = clamped;
    liveWindowVersion_ = 0;
    emit renderWindowChanged();
    emit viewportChanged();
}

bool ChartController::addVerticalMarker(qint64 tsNs, const QString& label) {
    if (!loaded_) {
        statusText_ = QStringLiteral("No active loaded chart for marker.");
        emit statusChanged();
        return false;
    }
    if (tsNs <= 0) {
        statusText_ = QStringLiteral("Marker timestamp must be positive.");
        emit statusChanged();
        return false;
    }
    constexpr std::size_t kMaxMarkers = 4096;
    verticalMarkers_.push_back(VerticalMarker{static_cast<std::int64_t>(tsNs), label.left(96)});
    if (verticalMarkers_.size() > kMaxMarkers) {
        verticalMarkers_.erase(verticalMarkers_.begin(), verticalMarkers_.begin() + static_cast<std::ptrdiff_t>(verticalMarkers_.size() - kMaxMarkers));
    }
    emit markersChanged();
    emit viewportChanged();
    return true;
}

void ChartController::clearVerticalMarkers() {
    if (verticalMarkers_.empty()) return;
    verticalMarkers_.clear();
    emit markersChanged();
    emit viewportChanged();
}

void ChartController::setLiveDataProvider(std::unique_ptr<ILiveDataProvider> provider) {
    if (liveDataProvider_ != nullptr) liveDataProvider_->stop();
    liveDataProvider_ = provider != nullptr ? std::move(provider) : std::make_unique<JsonTailLiveDataProvider>();
    liveProviderFromRegistry_ = dynamic_cast<InMemoryLiveDataProvider*>(liveDataProvider_.get()) != nullptr;
    liveProviderSourceId_ = liveProviderFromRegistry_ && currentSourceKind_ == QStringLiteral("live") ? currentSourceId_ : QString{};
    clearLiveDataCache_();
    if (currentSourceKind_ != QStringLiteral("live")) return;
    if (liveProviderFromRegistry_) {
        liveDataProvider_->start(LiveDataProviderConfig{{}, {}, currentSourceId_.toStdString()});
    } else if (!sessionDir_.isEmpty()) {
        liveDataProvider_->start(LiveDataProviderConfig{providerSessionPath(sessionDir_), {}, {}});
    }
}

void ChartController::refreshLiveDataWindow(std::int64_t tsMin, std::int64_t tsMax) {
    if (currentSourceKind_ != QStringLiteral("live")) return;
    if (liveDataProvider_ == nullptr || tsMax <= tsMin) return;
    bool emptyWindow = false;
    const std::int64_t latestTs = latestRenderableTsNs_();
    if (latestOnlyRenderWindow_()) {
        tsMin = std::max<std::int64_t>(tsMin, latestTs);
        tsMax = std::min<std::int64_t>(tsMax, latestTs);
        if (tsMax <= tsMin) {
            tsMax = latestTs;
            tsMin = std::max<std::int64_t>(0, latestTs - 1);
        }
    } else if (limitedRenderWindow_() && latestTs > 0) {
        tsMin = std::max(tsMin, effectiveRenderMinTs_(latestTs));
        tsMax = std::min(tsMax, latestTs);
        if (tsMax <= tsMin) emptyWindow = true;
    }
    if (liveWindowVersion_ == liveDataCache_.version
        && liveWindowTsMin_ == tsMin
        && liveWindowTsMax_ == tsMax) {
        return;
    }

    LiveDataBatch nextStable{};
    if (!emptyWindow) {
        const auto materializeStart = std::chrono::steady_clock::now();
        nextStable = liveDataProvider_->materializeRange(
            LiveDataRangeRequest{{}, tsMin, tsMax},
            liveDataCache_.version + 1u);
        hftrec::metrics::recordLiveMaterialize(static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - materializeStart).count()));
    }

    liveDataCache_.stableRows = std::move(nextStable);
    reconcileOverlayWithStable_();
    liveDataCache_.overlayRows = emptyWindow ? LiveDataBatch{} : liveOverlayState_;
    if (limitedRenderWindow_() && !emptyWindow) keepBatchInTimeRange(liveDataCache_.overlayRows, tsMin, tsMax);
    ++liveDataCache_.version;
    liveDataCache_.stableRows.id = liveDataCache_.version;
    liveDataCache_.overlayRows.id = liveDataCache_.version;
    liveDataCache_.hasRenderRange = !emptyWindow;
    liveDataCache_.renderTsMin = tsMin;
    liveDataCache_.renderTsMax = tsMax;
    hftrec::metrics::setLiveRows(static_cast<std::uint64_t>(liveDataCache_.stableRows.trades.size() + liveDataCache_.overlayRows.trades.size()),
                         static_cast<std::uint64_t>(liveDataCache_.stableRows.bookTickers.size() + liveDataCache_.overlayRows.bookTickers.size()),
                         static_cast<std::uint64_t>(liveDataCache_.stableRows.depths.size() + liveDataCache_.overlayRows.depths.size()),
                         static_cast<std::uint64_t>(liveDataCache_.stableRows.snapshots.size() + liveDataCache_.overlayRows.snapshots.size()));
    liveWindowTsMin_ = tsMin;
    liveWindowTsMax_ = tsMax;
    liveWindowVersion_ = liveDataCache_.version;
    refreshLoadedStateFromSources_();
}

void ChartController::refreshProviderFromRegistry_() {
    const bool wantsLiveRegistry = currentSourceKind_ == QStringLiteral("live") && !currentSourceId_.isEmpty();
    if (wantsLiveRegistry && LiveDataRegistry::instance().hasSource(currentSourceId_.toStdString())) {
        if (!liveProviderFromRegistry_ || liveProviderSourceId_ != currentSourceId_) {
            if (liveDataProvider_ != nullptr) liveDataProvider_->stop();
            liveDataProvider_ = LiveDataRegistry::instance().makeProvider(currentSourceId_.toStdString());
            if (liveDataProvider_ == nullptr) {
                liveProviderFromRegistry_ = false;
                liveProviderSourceId_.clear();
                clearLiveDataCache_();
                refreshLoadedStateFromSources_();
                return;
            }
            liveProviderFromRegistry_ = true;
            liveProviderSourceId_ = currentSourceId_;
            liveDataProvider_->start(LiveDataProviderConfig{{}, {}, currentSourceId_.toStdString()});
            clearLiveDataCache_();
            refreshLoadedStateFromSources_();
        }
        return;
    }

    if (liveProviderFromRegistry_) {
        if (liveDataProvider_ != nullptr) liveDataProvider_->stop();
        liveDataProvider_ = std::make_unique<JsonTailLiveDataProvider>();
        liveProviderFromRegistry_ = false;
        liveProviderSourceId_.clear();
        clearLiveDataCache_();
        if (currentSourceKind_ == QStringLiteral("live") && !sessionDir_.isEmpty()) {
            liveDataProvider_->start(LiveDataProviderConfig{providerSessionPath(sessionDir_), {}, {}});
        }
    }
}

bool ChartController::appendOverlayBatch_(const LiveDataBatch& batch, QString* failureText) {
    LiveDataBatch nextOverlay{};

    if (!batch.trades.empty()) nextOverlay.trades.push_back(batch.trades.back());
    if (!batch.bookTickers.empty()) nextOverlay.bookTickers.push_back(batch.bookTickers.back());
    if (!batch.depths.empty()) nextOverlay.depths.push_back(batch.depths.back());
    if (!batch.snapshots.empty()) nextOverlay.snapshots.push_back(batch.snapshots.back());

    if (!appendMonotonicRows(nextOverlay.trades, liveOverlayState_.trades, failureText, QStringLiteral("trade"))) return false;
    if (!appendMonotonicRows(nextOverlay.bookTickers, liveOverlayState_.bookTickers, failureText, QStringLiteral("bookticker"))) return false;
    if (!appendMonotonicRows(nextOverlay.depths, liveOverlayState_.depths, failureText, QStringLiteral("depth"))) return false;
    if (!appendMonotonicRows(nextOverlay.snapshots, liveOverlayState_.snapshots, failureText, QStringLiteral("snapshot"))) return false;

    if (liveOverlayState_.trades.size() > 1u) liveOverlayState_.trades.erase(liveOverlayState_.trades.begin(), liveOverlayState_.trades.end() - 1);
    if (liveOverlayState_.bookTickers.size() > 1u) liveOverlayState_.bookTickers.erase(liveOverlayState_.bookTickers.begin(), liveOverlayState_.bookTickers.end() - 1);
    if (liveOverlayState_.depths.size() > 1u) liveOverlayState_.depths.erase(liveOverlayState_.depths.begin(), liveOverlayState_.depths.end() - 1);
    if (liveOverlayState_.snapshots.size() > 1u) liveOverlayState_.snapshots.erase(liveOverlayState_.snapshots.begin(), liveOverlayState_.snapshots.end() - 1);
    liveOverlayState_.id = batch.id;
    return true;
}

void ChartController::reconcileOverlayWithStable_() {
    removeRowsPromotedToStable(liveOverlayState_.trades, liveDataCache_.stableRows.trades);
    removeRowsPromotedToStable(liveOverlayState_.bookTickers, liveDataCache_.stableRows.bookTickers);
    removeRowsPromotedToStable(liveOverlayState_.depths, liveDataCache_.stableRows.depths);
    removeRowsPromotedToStable(liveOverlayState_.snapshots, liveDataCache_.stableRows.snapshots);
}

void ChartController::refreshLoadedStateFromSources_() noexcept {
    loaded_ = !replay_.buckets().empty()
        || !replay_.trades().empty()
        || !replay_.bookTickers().empty()
        || !replay_.depths().empty()
        || !replay_.book().bids().empty()
        || !replay_.book().asks().empty()
        || !liveDataCache_.stableRows.trades.empty()
        || !liveDataCache_.stableRows.bookTickers.empty()
        || !liveDataCache_.stableRows.depths.empty()
        || !liveDataCache_.overlayRows.trades.empty()
        || !liveDataCache_.overlayRows.bookTickers.empty()
        || !liveDataCache_.overlayRows.depths.empty()
        || !liveOverlayState_.trades.empty()
        || !liveOverlayState_.bookTickers.empty()
        || !liveOverlayState_.depths.empty();
}

std::int64_t ChartController::latestOrderbookTsNs_() const noexcept {
    std::int64_t latest = 0;
    if (!replay_.depths().empty()) latest = std::max(latest, replay_.depths().back().tsNs);
    for (const auto& row : liveDataCache_.stableRows.depths) latest = std::max(latest, row.tsNs);
    for (const auto& row : liveDataCache_.overlayRows.depths) latest = std::max(latest, row.tsNs);
    for (const auto& row : liveOverlayState_.depths) latest = std::max(latest, row.tsNs);
    if (latest == 0 && !replay_.buckets().empty()) latest = replay_.lastTsNs();
    return latest;
}

std::int64_t ChartController::latestRenderableTsNs_() const noexcept {
    std::int64_t latest = std::max<std::int64_t>(replay_.lastTsNs(), 0);
    const auto absorbTradeRows = [&latest](const auto& rows) noexcept {
        if (!rows.empty()) latest = std::max(latest, rows.back().tsNs);
    };
    const auto absorbTickerRows = [&latest](const auto& rows) noexcept {
        if (!rows.empty()) latest = std::max(latest, rows.back().tsNs);
    };
    const auto absorbDepthRows = [&latest](const auto& rows) noexcept {
        if (!rows.empty()) latest = std::max(latest, rows.back().tsNs);
    };
    const auto absorbSnapshotRows = [&latest](const auto& rows) noexcept {
        if (!rows.empty()) latest = std::max(latest, rows.back().tsNs);
    };

    absorbTradeRows(liveDataCache_.stableRows.trades);
    absorbTradeRows(liveDataCache_.overlayRows.trades);
    absorbTradeRows(liveOverlayState_.trades);
    absorbTickerRows(liveDataCache_.stableRows.bookTickers);
    absorbTickerRows(liveDataCache_.overlayRows.bookTickers);
    absorbTickerRows(liveOverlayState_.bookTickers);
    absorbDepthRows(liveDataCache_.stableRows.depths);
    absorbDepthRows(liveDataCache_.overlayRows.depths);
    absorbDepthRows(liveOverlayState_.depths);
    absorbSnapshotRows(liveDataCache_.stableRows.snapshots);
    absorbSnapshotRows(liveDataCache_.overlayRows.snapshots);
    absorbSnapshotRows(liveOverlayState_.snapshots);
    return latest;
}

std::int64_t ChartController::effectiveRenderMinTs_(std::int64_t latestTsNs) const noexcept {
    if (!limitedRenderWindow_() || latestTsNs <= 0) return std::numeric_limits<std::int64_t>::min();
    constexpr std::int64_t kOneSecondNs = 1000000000ll;
    const std::int64_t windowNs = static_cast<std::int64_t>(renderWindowSeconds_) * kOneSecondNs;
    if (latestTsNs <= windowNs) return 0;
    return latestTsNs - windowNs;
}

void ChartController::initializeViewportFromLiveDataOnce_() noexcept {
    if (liveInitialViewportApplied_) return;
    if (!replay_.trades().empty() || !replay_.bookTickers().empty() || !replay_.depths().empty()
        || !replay_.book().bids().empty() || !replay_.book().asks().empty()) {
        liveInitialViewportApplied_ = true;
        return;
    }
    if (tsMax_ > tsMin_ && priceMaxE8_ > priceMinE8_) {
        liveInitialViewportApplied_ = true;
        return;
    }

    std::int64_t firstTs = 0;
    std::int64_t firstPrice = 0;
    bool found = false;

    const auto absorbCandidate = [&](std::int64_t tsNs, std::int64_t priceE8) noexcept {
        if (priceE8 <= 0) return;
        if (!found || tsNs < firstTs) {
            firstTs = tsNs;
            firstPrice = priceE8;
            found = true;
        }
    };
    const auto absorbTradeRows = [&](const auto& rows) noexcept {
        for (const auto& row : rows) absorbCandidate(row.tsNs, row.priceE8);
    };
    const auto absorbTickerRows = [&](const auto& rows) noexcept {
        for (const auto& row : rows) {
            absorbCandidate(row.tsNs, row.bidPriceE8);
            absorbCandidate(row.tsNs, row.askPriceE8);
        }
    };
    const auto absorbDepthRows = [&](const auto& rows) noexcept {
        for (const auto& row : rows) {
            for (const auto& level : row.bids) absorbCandidate(row.tsNs, level.priceE8);
            for (const auto& level : row.asks) absorbCandidate(row.tsNs, level.priceE8);
        }
    };

    absorbTradeRows(liveDataCache_.stableRows.trades);
    absorbTradeRows(liveDataCache_.overlayRows.trades);
    absorbTradeRows(liveOverlayState_.trades);
    absorbTickerRows(liveDataCache_.stableRows.bookTickers);
    absorbTickerRows(liveDataCache_.overlayRows.bookTickers);
    absorbTickerRows(liveOverlayState_.bookTickers);
    absorbDepthRows(liveDataCache_.stableRows.depths);
    absorbDepthRows(liveDataCache_.overlayRows.depths);
    absorbDepthRows(liveOverlayState_.depths);

    if (!found) return;

    static constexpr std::int64_t kInitialHalfWindowNs = 1'500'000'000ll;
    tsMin_ = firstTs - kInitialHalfWindowNs;
    tsMax_ = firstTs + kInitialHalfWindowNs;
    if (tsMax_ <= tsMin_) tsMax_ = tsMin_ + 1;

    const std::int64_t pricePad = std::max<std::int64_t>(firstPrice / 1000, 1);
    priceMinE8_ = std::max<std::int64_t>(0, firstPrice - pricePad);
    priceMaxE8_ = firstPrice + pricePad;
    if (priceMaxE8_ <= priceMinE8_) priceMaxE8_ = priceMinE8_ + 1;
    liveInitialViewportApplied_ = true;
}

}  // namespace hftrec::gui::viewer



