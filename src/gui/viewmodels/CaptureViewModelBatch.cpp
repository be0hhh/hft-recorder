#include "gui/viewmodels/CaptureViewModel.hpp"

#include <QVariantMap>
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

}  // namespace

bool CaptureViewModel::startTrades() {
    if (!ensureCoordinatorBatch_()) {
        refreshState();
        return false;
    }

    const auto configs = makeConfigs();
    for (std::size_t i = 0; i < coordinators_.size() && i < configs.size(); ++i) {
        const auto status = coordinators_[i]->startTrades(configs[i]);
        if (!isOk(status)) {
            abortCoordinatorBatch_(joinCoordinatorErrors_());
            refreshState();
            return false;
        }
    }

    setStatusText(QStringLiteral("Trades capture started for %1 symbol(s)").arg(coordinators_.size()));
    registerLiveSources_();
    refreshState();
    return true;
}

void CaptureViewModel::stopTrades() {
    for (auto& coordinator : coordinators_) {
        if (coordinator) coordinator->requestStopTrades();
    }
    setStatusText(QStringLiteral("Trades stop requested"));
    refreshState();
}

bool CaptureViewModel::startLiquidations() {
    if (!ensureCoordinatorBatch_()) {
        refreshState();
        return false;
    }

    const auto configs = makeConfigs();
    for (std::size_t i = 0; i < coordinators_.size() && i < configs.size(); ++i) {
        const auto status = coordinators_[i]->startLiquidations(configs[i]);
        if (!isOk(status)) {
            abortCoordinatorBatch_(joinCoordinatorErrors_());
            refreshState();
            return false;
        }
    }

    setStatusText(QStringLiteral("Liquidations capture started for %1 symbol(s)").arg(coordinators_.size()));
    registerLiveSources_();
    refreshState();
    return true;
}

void CaptureViewModel::stopLiquidations() {
    for (auto& coordinator : coordinators_) {
        if (coordinator) coordinator->requestStopLiquidations();
    }
    setStatusText(QStringLiteral("Liquidations stop requested"));
    refreshState();
}
bool CaptureViewModel::startBookTicker() {
    if (!ensureCoordinatorBatch_()) {
        refreshState();
        return false;
    }

    const auto configs = makeConfigs();
    for (std::size_t i = 0; i < coordinators_.size() && i < configs.size(); ++i) {
        const auto status = coordinators_[i]->startBookTicker(configs[i]);
        if (!isOk(status)) {
            abortCoordinatorBatch_(joinCoordinatorErrors_());
            refreshState();
            return false;
        }
    }

    setStatusText(QStringLiteral("BookTicker capture started for %1 symbol(s)").arg(coordinators_.size()));
    registerLiveSources_();
    refreshState();
    return true;
}

void CaptureViewModel::stopBookTicker() {
    for (auto& coordinator : coordinators_) {
        if (coordinator) coordinator->requestStopBookTicker();
    }
    setStatusText(QStringLiteral("BookTicker stop requested"));
    refreshState();
}

bool CaptureViewModel::startOrderbook() {
    if (!ensureCoordinatorBatch_()) {
        refreshState();
        return false;
    }

    const auto configs = makeConfigs();
    for (std::size_t i = 0; i < coordinators_.size() && i < configs.size(); ++i) {
        const auto status = coordinators_[i]->startOrderbook(configs[i]);
        if (!isOk(status)) {
            abortCoordinatorBatch_(joinCoordinatorErrors_());
            refreshState();
            return false;
        }
    }

    setStatusText(QStringLiteral("Orderbook capture started for %1 symbol(s)").arg(coordinators_.size()));
    registerLiveSources_();
    refreshState();
    return true;
}

void CaptureViewModel::stopOrderbook() {
    for (auto& coordinator : coordinators_) {
        if (coordinator) coordinator->requestStopOrderbook();
    }
    setStatusText(QStringLiteral("Orderbook stop requested"));
    refreshState();
}

bool CaptureViewModel::startAllChannels() {
    if (!ensureCoordinatorBatch_()) {
        refreshState();
        return false;
    }

    const auto configs = makeConfigs();
    for (std::size_t i = 0; i < coordinators_.size() && i < configs.size(); ++i) {
        const auto tradesStatus = coordinators_[i]->startTrades(configs[i]);
        if (!isOk(tradesStatus)) {
            abortCoordinatorBatch_(joinCoordinatorErrors_());
            refreshState();
            return false;
        }

        const auto liquidationsStatus = coordinators_[i]->startLiquidations(configs[i]);
        if (!isOk(liquidationsStatus)) {
            abortCoordinatorBatch_(joinCoordinatorErrors_());
            refreshState();
            return false;
        }

        const auto bookTickerStatus = coordinators_[i]->startBookTicker(configs[i]);
        if (!isOk(bookTickerStatus)) {
            abortCoordinatorBatch_(joinCoordinatorErrors_());
            refreshState();
            return false;
        }

        const auto orderbookStatus = coordinators_[i]->startOrderbook(configs[i]);
        if (!isOk(orderbookStatus)) {
            abortCoordinatorBatch_(joinCoordinatorErrors_());
            refreshState();
            return false;
        }
    }

    setStatusText(QStringLiteral("All capture channels started for %1 symbol(s)").arg(coordinators_.size()));
    registerLiveSources_();
    refreshState();
    return true;
}

void CaptureViewModel::stopAllChannels() {
    for (auto& coordinator : coordinators_) {
        if (!coordinator) continue;
        coordinator->requestStopTrades();
        coordinator->requestStopLiquidations();
        coordinator->requestStopBookTicker();
        coordinator->requestStopOrderbook();
    }
    setStatusText(QStringLiteral("All capture channels stop requested"));
    refreshState();
}

void CaptureViewModel::finalizeSession() {
    bool ok = true;
    for (auto& coordinator : coordinators_) {
        if (!coordinator) continue;
        const auto status = coordinator->finalizeSession();
        if (!isOk(status)) ok = false;
    }
    setStatusText(ok ? QStringLiteral("Session batch finalized") : joinCoordinatorErrors_());
    viewer::LiveDataRegistry::instance().clear();
    publishActiveLiveSources_();
    clearCoordinatorBatch_();
    refreshState();
}

bool CaptureViewModel::ensureCoordinatorBatch_() {
    if (!coordinators_.empty()) return true;

    const auto configs = makeConfigs();
    if (configs.empty()) {
        setStatusText(QStringLiteral("Enter at least one symbol, e.g. ETH or BTC ETH"));
        return false;
    }

    coordinators_.clear();
    coordinators_.reserve(configs.size());
    for (std::size_t i = 0; i < configs.size(); ++i) {
        coordinators_.push_back(std::make_unique<capture::CaptureCoordinator>());
    }
    return true;
}

void CaptureViewModel::registerLiveSources_() {
    std::vector<viewer::LiveDataRegistry::RegisteredSource> sources;
    QVariantList descriptors;
    sources.reserve(coordinators_.size());
    descriptors.reserve(static_cast<qsizetype>(coordinators_.size()));

    for (const auto& coordinator : coordinators_) {
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
    for (auto& coordinator : coordinators_) {
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
    for (const auto& coordinator : coordinators_) {
        if (!coordinator) continue;
        const auto error = QString::fromStdString(coordinator->lastError()).trimmed();
        if (!error.isEmpty() && !errors.contains(error)) errors.push_back(error);
    }
    if (errors.isEmpty()) return QStringLiteral("Operation failed");
    return errors.join(QStringLiteral(" | "));
}

}  // namespace hftrec::gui
