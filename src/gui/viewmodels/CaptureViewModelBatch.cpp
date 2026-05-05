#include "gui/viewmodels/CaptureViewModel.hpp"

#include <QVariantMap>
#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gui/viewer/LiveDataProvider.hpp"
#include "gui/viewmodels/CaptureViewModelInternal.hpp"

namespace hftrec::gui {

namespace {

QString buildViewerSourceId(const QString& exchange, const QString& market, const QString& symbol) {
    return QStringLiteral("live:%1:%2:%3")
        .arg(exchange.trimmed().toLower(), market.trimmed().toLower(), symbol.trimmed().toUpper());
}

QString buildLiveLabel(const QString& exchange, const QString& market, const QString& symbol) {
    const auto normalizedSymbol = symbol.trimmed().toUpper();
    return QStringLiteral("LIVE | %1 | %2 | %3")
        .arg(exchange.trimmed().isEmpty() ? QStringLiteral("Unknown Exchange") : exchange.trimmed(),
             market.trimmed().isEmpty() ? QStringLiteral("Unknown Market") : market.trimmed(),
             normalizedSymbol.isEmpty() ? QStringLiteral("Unknown Symbol") : normalizedSymbol);
}

QString configKey(const capture::CaptureConfig& config) {
    const QString symbol = QString::fromStdString(config.symbols.empty() ? std::string{} : config.symbols.front());
    return QStringLiteral("%1|%2|%3|%4")
        .arg(QString::fromStdString(config.exchange).toLower(),
             QString::fromStdString(config.market).toLower(),
             symbol,
             QString::fromStdString(config.outputDir.string()));
}

bool configsMatch(const capture::CaptureConfig& lhs, const capture::CaptureConfig& rhs) {
    return configKey(lhs) == configKey(rhs);
}

void stopRequestedChannels(capture::CaptureCoordinator& coordinator,
                           bool trades,
                           bool liquidations,
                           bool bookTicker,
                           bool orderbook) {
    if (trades) coordinator.requestStopTrades();
    if (liquidations) coordinator.requestStopLiquidations();
    if (bookTicker) coordinator.requestStopBookTicker();
    if (orderbook) coordinator.requestStopOrderbook();
}

bool startDesiredChannels(capture::CaptureCoordinator& coordinator,
                          const capture::CaptureConfig& config,
                          bool trades,
                          bool liquidations,
                          bool bookTicker,
                          bool orderbook) {
    if (trades && !coordinator.tradesRunning()) {
        if (!isOk(coordinator.startTrades(config))) return false;
    }
    if (liquidations && !coordinator.liquidationsRunning()) {
        if (!isOk(coordinator.startLiquidations(config))) return false;
    }
    if (bookTicker && !coordinator.bookTickerRunning()) {
        if (!isOk(coordinator.startBookTicker(config))) return false;
    }
    if (orderbook && !coordinator.orderbookRunning()) {
        if (!isOk(coordinator.startOrderbook(config))) return false;
    }
    return true;
}

}  // namespace

bool CaptureViewModel::startTrades() {
    desiredTradesRunning_ = true;
    if (!reconcileCoordinatorBatch_()) return false;
    setStatusText(QStringLiteral("Trades capture desired for %1 stream(s)").arg(coordinators_.size()));
    registerLiveSources_();
    refreshState();
    return true;
}

void CaptureViewModel::stopTrades() {
    desiredTradesRunning_ = false;
    for (auto& entry : coordinators_) {
        if (entry.coordinator) entry.coordinator->requestStopTrades();
    }
    setStatusText(QStringLiteral("Trades stop requested"));
    refreshState();
}

bool CaptureViewModel::startLiquidations() {
    desiredLiquidationsRunning_ = true;
    if (!reconcileCoordinatorBatch_()) return false;
    setStatusText(QStringLiteral("Liquidations capture desired for %1 stream(s)").arg(coordinators_.size()));
    registerLiveSources_();
    refreshState();
    return true;
}

void CaptureViewModel::stopLiquidations() {
    desiredLiquidationsRunning_ = false;
    for (auto& entry : coordinators_) {
        if (entry.coordinator) entry.coordinator->requestStopLiquidations();
    }
    setStatusText(QStringLiteral("Liquidations stop requested"));
    refreshState();
}
bool CaptureViewModel::startBookTicker() {
    desiredBookTickerRunning_ = true;
    if (!reconcileCoordinatorBatch_()) return false;
    setStatusText(QStringLiteral("BookTicker capture desired for %1 stream(s)").arg(coordinators_.size()));
    registerLiveSources_();
    refreshState();
    return true;
}

void CaptureViewModel::stopBookTicker() {
    desiredBookTickerRunning_ = false;
    for (auto& entry : coordinators_) {
        if (entry.coordinator) entry.coordinator->requestStopBookTicker();
    }
    setStatusText(QStringLiteral("BookTicker stop requested"));
    refreshState();
}

bool CaptureViewModel::startOrderbook() {
    desiredOrderbookRunning_ = true;
    if (!reconcileCoordinatorBatch_()) return false;
    setStatusText(QStringLiteral("Orderbook capture desired for %1 stream(s)").arg(coordinators_.size()));
    registerLiveSources_();
    refreshState();
    return true;
}

void CaptureViewModel::stopOrderbook() {
    desiredOrderbookRunning_ = false;
    for (auto& entry : coordinators_) {
        if (entry.coordinator) entry.coordinator->requestStopOrderbook();
    }
    setStatusText(QStringLiteral("Orderbook stop requested"));
    refreshState();
}

bool CaptureViewModel::startAllChannels() {
    desiredTradesRunning_ = true;
    desiredBookTickerRunning_ = true;
    desiredOrderbookRunning_ = true;
    if (!reconcileCoordinatorBatch_()) return false;
    setStatusText(QStringLiteral("All available capture channels desired for %1 stream(s)").arg(coordinators_.size()));
    registerLiveSources_();
    refreshState();
    return true;
}

void CaptureViewModel::stopAllChannels() {
    desiredTradesRunning_ = false;
    desiredLiquidationsRunning_ = false;
    desiredBookTickerRunning_ = false;
    desiredOrderbookRunning_ = false;
    for (auto& entry : coordinators_) {
        if (!entry.coordinator) continue;
        entry.coordinator->requestStopTrades();
        entry.coordinator->requestStopLiquidations();
        entry.coordinator->requestStopBookTicker();
        entry.coordinator->requestStopOrderbook();
    }
    setStatusText(QStringLiteral("All capture channels stop requested"));
    refreshState();
}

void CaptureViewModel::finalizeSession() {
    bool ok = true;
    for (auto& entry : coordinators_) {
        if (!entry.coordinator) continue;
        const auto status = entry.coordinator->finalizeSession();
        if (!isOk(status)) ok = false;
    }
    setStatusText(ok ? QStringLiteral("Session batch finalized") : joinCoordinatorErrors_());
    viewer::LiveDataRegistry::instance().clear();
    publishActiveLiveSources_();
    clearCoordinatorBatch_();
    refreshState();
}

bool CaptureViewModel::ensureCoordinatorBatch_() {
    return reconcileCoordinatorBatch_();
}

bool CaptureViewModel::reconcileCoordinatorBatch_() {
    const auto configs = makeConfigs();
    if (configs.empty()) {
        for (auto& entry : coordinators_) {
            if (entry.coordinator) (void)entry.coordinator->finalizeSession();
        }
        coordinators_.clear();
        viewer::LiveDataRegistry::instance().clear();
        publishActiveLiveSources_();
        setStatusText(QStringLiteral("Enter at least one venue symbol"));
        return false;
    }

    for (auto it = coordinators_.begin(); it != coordinators_.end();) {
        const bool stillDesired = std::any_of(configs.begin(), configs.end(), [&](const capture::CaptureConfig& config) {
            return configsMatch(it->config, config);
        });
        if (stillDesired) {
            ++it;
            continue;
        }
        if (it->coordinator) (void)it->coordinator->finalizeSession();
        it = coordinators_.erase(it);
    }

    for (const auto& config : configs) {
        const auto existing = std::find_if(coordinators_.begin(), coordinators_.end(), [&](const CoordinatorEntry& entry) {
            return configsMatch(entry.config, config);
        });
        if (existing != coordinators_.end()) continue;

        CoordinatorEntry entry{};
        entry.config = config;
        entry.coordinator = std::make_unique<capture::CaptureCoordinator>();
        coordinators_.push_back(std::move(entry));
    }

    for (auto& entry : coordinators_) {
        if (!entry.coordinator) continue;
        if (!startDesiredChannels(*entry.coordinator,
                                  entry.config,
                                  desiredTradesRunning_,
                                  desiredLiquidationsRunning_,
                                  desiredBookTickerRunning_,
                                  desiredOrderbookRunning_)) {
            setStatusText(joinCoordinatorErrors_());
            refreshState();
            return false;
        }
    }

    return true;
}

void CaptureViewModel::reconcileActiveChannels_() {
    if (!(desiredTradesRunning_ || desiredLiquidationsRunning_ || desiredBookTickerRunning_ || desiredOrderbookRunning_)) return;
    (void)reconcileCoordinatorBatch_();
    registerLiveSources_();
}

void CaptureViewModel::registerLiveSources_() {
    std::vector<viewer::LiveDataRegistry::RegisteredSource> sources;
    QVariantList descriptors;
    sources.reserve(coordinators_.size());
    descriptors.reserve(static_cast<qsizetype>(coordinators_.size()));

    for (const auto& entry : coordinators_) {
        const auto& coordinator = entry.coordinator;
        if (!coordinator) continue;
        const auto manifest = coordinator->manifestCopy();
        const bool hasLiveChannel = coordinator->tradesRunning() || coordinator->liquidationsRunning() || coordinator->bookTickerRunning() || coordinator->orderbookRunning();
        if (!hasLiveChannel) continue;

        const QString exchange = QString::fromStdString(manifest.exchange);
        const QString market = QString::fromStdString(manifest.market);
        const QString symbol = QString::fromStdString(manifest.symbols.empty() ? std::string{} : manifest.symbols.front()).trimmed().toUpper();
        const QString sourceId = buildViewerSourceId(exchange, market, symbol);

        sources.push_back(viewer::LiveDataRegistry::RegisteredSource{
            sourceId.toStdString(),
            exchange.toStdString(),
            market.toStdString(),
            symbol.toStdString(),
            manifest.sessionId,
            coordinator->sessionDirCopy(),
            coordinator.get()});

        QVariantMap descriptor;
        descriptor.insert(QStringLiteral("id"), sourceId);
        descriptor.insert(QStringLiteral("label"), buildLiveLabel(exchange, market, symbol));
        descriptor.insert(QStringLiteral("exchange"), exchange);
        descriptor.insert(QStringLiteral("market"), market);
        descriptor.insert(QStringLiteral("symbol"), symbol);
        descriptor.insert(QStringLiteral("sessionId"), QString::fromStdString(manifest.sessionId));
        descriptor.insert(QStringLiteral("sessionPath"), QString::fromStdString(coordinator->sessionDirCopy().string()));
        descriptor.insert(QStringLiteral("liveAvailable"), true);
        descriptors.push_back(descriptor);
    }

    viewer::LiveDataRegistry::instance().setSources(std::move(sources));
    if (activeLiveSources_ != descriptors) {
        activeLiveSources_ = descriptors;
        emit activeLiveSourcesChanged();
    }
}

void CaptureViewModel::publishActiveLiveSources_() {
    if (activeLiveSources_.isEmpty()) return;
    activeLiveSources_.clear();
    emit activeLiveSourcesChanged();
}

void CaptureViewModel::clearCoordinatorBatch_() {
    coordinators_.clear();
}

void CaptureViewModel::abortCoordinatorBatch_(const QString& fallbackStatus) {
    QStringList errors;
    for (auto& entry : coordinators_) {
        const auto& coordinator = entry.coordinator;
        if (!coordinator) continue;
        const auto preFinalizeError = QString::fromStdString(coordinator->lastError()).trimmed();
        coordinator->stopTrades();
        coordinator->stopLiquidations();
        coordinator->stopBookTicker();
        coordinator->stopOrderbook();
        const auto status = coordinator->finalizeSession();
        if (!preFinalizeError.isEmpty() && !errors.contains(preFinalizeError)) errors.push_back(preFinalizeError);
        if (!isOk(status) && preFinalizeError.isEmpty()) {
            const auto statusText = QString::fromUtf8(hftrec::statusToString(status).data());
            if (!errors.contains(statusText)) errors.push_back(statusText);
        }
    }

    viewer::LiveDataRegistry::instance().clear();
    publishActiveLiveSources_();
    clearCoordinatorBatch_();
    setStatusText(errors.isEmpty() ? fallbackStatus : errors.join(QStringLiteral(" | ")));
}

QString CaptureViewModel::joinCoordinatorErrors_() const {
    QStringList errors;
    for (const auto& entry : coordinators_) {
        const auto& coordinator = entry.coordinator;
        if (!coordinator) continue;
        const auto error = QString::fromStdString(coordinator->lastError()).trimmed();
        if (!error.isEmpty() && !errors.contains(error)) errors.push_back(error);
    }
    if (errors.isEmpty()) return QStringLiteral("Operation failed");
    return errors.join(QStringLiteral(" | "));
}

}  // namespace hftrec::gui
