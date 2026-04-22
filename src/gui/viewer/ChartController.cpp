#include "gui/viewer/ChartController.hpp"

#include <filesystem>
#include <memory>
#include <utility>

namespace hftrec::gui::viewer {

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

}  // namespace

ChartController::ChartController(QObject* parent)
    : QObject(parent) {
    gpuRendererAvailable_ = !envForcesSoftwareRenderer();
    liveDataProvider_ = std::make_unique<JsonTailLiveDataProvider>();
    liveDataTimer_ = new QTimer(this);
    liveDataTimer_->setInterval(liveUpdateIntervalMs_);
    liveDataTimer_->setTimerType(Qt::PreciseTimer);
    connect(liveDataTimer_, &QTimer::timeout, this, [this]() { pollLiveData_(); });
    liveDataTimer_->start();
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

void ChartController::setLiveDataProvider(std::unique_ptr<ILiveDataProvider> provider) {
    if (liveDataProvider_ != nullptr) liveDataProvider_->stop();
    liveDataProvider_ = provider != nullptr ? std::move(provider) : std::make_unique<JsonTailLiveDataProvider>();
    liveProviderFromRegistry_ = dynamic_cast<InMemoryLiveDataProvider*>(liveDataProvider_.get()) != nullptr;
    liveProviderSourceId_ = liveProviderFromRegistry_ ? currentSourceId_ : QString{};
    clearLiveDataCache_();
    if (liveProviderFromRegistry_) {
        liveDataProvider_->start(LiveDataProviderConfig{{}, {}, currentSourceId_.toStdString()});
    } else if (!sessionDir_.isEmpty()) {
        liveDataProvider_->start(LiveDataProviderConfig{providerSessionPath(sessionDir_), {}, {}});
    }
}

void ChartController::refreshLiveDataWindow(std::int64_t tsMin, std::int64_t tsMax) {
    if (liveDataProvider_ == nullptr || tsMax <= tsMin) return;
    if (liveProviderFromRegistry_) {
        const bool hasCachedRows = !liveDataCache_.visibleRows.trades.empty()
            || !liveDataCache_.visibleRows.bookTickers.empty()
            || !liveDataCache_.visibleRows.depths.empty()
            || !liveDataCache_.visibleRows.snapshots.empty()
            || !liveDataCache_.lastBatch.trades.empty()
            || !liveDataCache_.lastBatch.bookTickers.empty()
            || !liveDataCache_.lastBatch.depths.empty()
            || !liveDataCache_.lastBatch.snapshots.empty();
        if (hasCachedRows) {
            liveDataCache_.visibleRows = LiveDataBatch{};
            liveDataCache_.lastBatch = LiveDataBatch{};
            ++liveDataCache_.version;
        }
        return;
    }
    if (liveWindowVersion_ == liveDataCache_.version
        && liveWindowTsMin_ == tsMin
        && liveWindowTsMax_ == tsMax) {
        return;
    }
    liveDataCache_.visibleRows = liveDataProvider_->materializeRange(
        LiveDataRangeRequest{{}, tsMin, tsMax},
        liveDataCache_.version);
    liveWindowTsMin_ = tsMin;
    liveWindowTsMax_ = tsMax;
    liveWindowVersion_ = liveDataCache_.version;
}

void ChartController::refreshProviderFromRegistry_() {
    const bool wantsLiveRegistry = currentSourceKind_ == QStringLiteral("live") && !currentSourceId_.isEmpty();
    if (wantsLiveRegistry && LiveDataRegistry::instance().hasSource(currentSourceId_.toStdString())) {
        if (!liveProviderFromRegistry_ || liveProviderSourceId_ != currentSourceId_) {
            auto provider = LiveDataRegistry::instance().makeProvider(currentSourceId_.toStdString());
            if (provider != nullptr) {
                if (liveDataProvider_ != nullptr) liveDataProvider_->stop();
                liveDataProvider_ = std::move(provider);
                liveProviderFromRegistry_ = true;
                liveProviderSourceId_ = currentSourceId_;
                liveDataProvider_->start(LiveDataProviderConfig{{}, {}, currentSourceId_.toStdString()});
                clearLiveDataCache_();
            }
        }
        return;
    }

    if (liveProviderFromRegistry_) {
        if (liveDataProvider_ != nullptr) liveDataProvider_->stop();
        liveDataProvider_ = std::make_unique<JsonTailLiveDataProvider>();
        liveProviderFromRegistry_ = false;
        liveProviderSourceId_.clear();
        clearLiveDataCache_();
        if (currentSourceKind_ == QStringLiteral("recorded") && !sessionDir_.isEmpty()) {
            liveDataProvider_->start(LiveDataProviderConfig{providerSessionPath(sessionDir_), {}, {}});
        }
    }
}

bool ChartController::absorbRegistryBatchIntoReplay_(const LiveDataBatch& batch) {
    if (!liveProviderFromRegistry_) return false;
    if (batch.trades.empty() && batch.bookTickers.empty() && batch.depths.empty() && batch.snapshots.empty()) return false;

    for (const auto& snapshot : batch.snapshots) replay_.appendSnapshotDocument(snapshot);
    for (const auto& row : batch.trades) replay_.appendTradeRow(row);
    for (const auto& row : batch.bookTickers) replay_.appendBookTickerRow(row);
    for (const auto& row : batch.depths) replay_.appendDepthRow(row);
    replay_.refreshLiveTimeline();
    loaded_ = !replay_.buckets().empty()
        || !replay_.trades().empty()
        || !replay_.bookTickers().empty()
        || !replay_.depths().empty()
        || !replay_.book().bids().empty()
        || !replay_.book().asks().empty();
    if (loaded_ && (tsMax_ <= tsMin_ || priceMaxE8_ <= priceMinE8_)) {
        computeInitialViewport_();
    }
    currentBookTickerIndex_ = -1;
    return true;
}

}  // namespace hftrec::gui::viewer

