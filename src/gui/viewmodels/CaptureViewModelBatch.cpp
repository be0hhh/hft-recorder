#include "gui/viewmodels/CaptureViewModel.hpp"

#include <QDateTime>
#include <QStringList>
#include <QVariantMap>
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <iterator>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "gui/viewer/LiveDataProvider.hpp"
#include "gui/viewmodels/CaptureViewModelInternal.hpp"

namespace hftrec::gui {

namespace {

constexpr qint64 kNsPerMs = 1'000'000ll;

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

qint64 currentUtcNs() {
    return QDateTime::currentMSecsSinceEpoch() * kNsPerMs;
}

bool retryableDetailedCandlesWindowError(const QString& error) {
    return error.contains(QStringLiteral("fetch returned no valid OHLCV rows")) ||
           error.contains(QStringLiteral("parsed_rows=0"));
}

QString endLabelForStatus(std::int64_t endNs) {
    if (endNs <= 0) return QStringLiteral("now");
    return QDateTime::fromMSecsSinceEpoch(endNs / kNsPerMs, Qt::UTC).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss'Z'"));
}

QString configKey(const capture::CaptureConfig& config) {
    const QString symbol = QString::fromStdString(config.symbols.empty() ? std::string{} : config.symbols.front());
    const int apiSlot = config.apiSlot == 0u ? 1 : static_cast<int>(config.apiSlot);
    return QStringLiteral("%1|%2|%3|%4|%5|%6|%7")
        .arg(QString::fromStdString(config.exchange).toLower(),
             QString::fromStdString(config.market).toLower(),
             symbol,
             QString::fromStdString(config.envPath.string()),
             QString::number(apiSlot),
             QString::fromStdString(config.outputDir.string()),
             QString::fromStdString(config.detailedCandlesTimeframe));
}

bool configsMatch(const capture::CaptureConfig& lhs, const capture::CaptureConfig& rhs) {
    return configKey(lhs) == configKey(rhs);
}

bool hasRunningChannel(const capture::CaptureCoordinator& coordinator) noexcept {
    return coordinator.tradesRunning()
        || coordinator.liquidationsRunning()
        || coordinator.bookTickerRunning()
        || coordinator.orderbookRunning()
        || coordinator.markPriceRunning()
        || coordinator.indexPriceRunning()
        || coordinator.fundingRunning()
        || coordinator.priceLimitRunning();
}

bool startDesiredChannels(capture::CaptureCoordinator& coordinator,
                          const capture::CaptureConfig& config,
                          bool trades,
                          bool liquidations,
                          bool bookTicker,
                          bool orderbook,
                          bool markPrice,
                          bool indexPrice,
                          bool funding,
                          bool priceLimit) {
    const bool requested = trades || liquidations || bookTicker || orderbook || markPrice || indexPrice || funding || priceLimit;
    bool running = hasRunningChannel(coordinator);

    if (trades && !coordinator.tradesRunning()) {
        if (isOk(coordinator.startTrades(config))) running = true;
    }
    if (liquidations && !coordinator.liquidationsRunning()) {
        if (isOk(coordinator.startLiquidations(config))) running = true;
    }
    if (bookTicker && !coordinator.bookTickerRunning()) {
        if (isOk(coordinator.startBookTicker(config))) running = true;
    }
    if (orderbook && !coordinator.orderbookRunning()) {
        if (isOk(coordinator.startOrderbook(config))) running = true;
    }
    if (markPrice && !coordinator.markPriceRunning()) {
        if (isOk(coordinator.startMarkPrice(config))) running = true;
    }
    if (indexPrice && !coordinator.indexPriceRunning()) {
        if (isOk(coordinator.startIndexPrice(config))) running = true;
    }
    if (funding && !coordinator.fundingRunning()) {
        if (isOk(coordinator.startFunding(config))) running = true;
    }
    if (priceLimit && !coordinator.priceLimitRunning()) {
        if (isOk(coordinator.startPriceLimit(config))) running = true;
    }
    return !requested || running;
}

}  // namespace

bool CaptureViewModel::startTrades() {
    desiredTradesRunning_ = true;
    if (!reconcileCoordinatorBatch_()) return false;
    setStatusText(QStringLiteral("Trades capture desired for %1 stream(s)").arg(coordinators_.size()));
    registerLiveSources_();
    refreshState(detail::CaptureRefreshMode::Full);
    return true;
}

void CaptureViewModel::stopTrades() {
    desiredTradesRunning_ = false;
    for (auto& entry : coordinators_) {
        if (entry.coordinator) entry.coordinator->requestStopTrades();
    }
    setStatusText(QStringLiteral("Trades stop requested"));
    refreshState(detail::CaptureRefreshMode::Full);
}

bool CaptureViewModel::startTradesHistory() {
    if (!ensureCoordinatorBatch_()) return false;

    bool ok = true;
    for (auto& entry : coordinators_) {
        if (!entry.coordinator) continue;
        const auto historyStatus = entry.coordinator->captureTradesHistoryOnce(entry.config);
        if (!isOk(historyStatus)) ok = false;
    }

    setStatusText(ok
        ? QStringLiteral("Trades history fetched for %1 stream(s)").arg(coordinators_.size())
        : joinCoordinatorErrors_());
    registerLiveSources_();
    refreshState(detail::CaptureRefreshMode::Full);
    return ok;
}

bool CaptureViewModel::startLiquidations() {
    desiredLiquidationsRunning_ = true;
    if (!reconcileCoordinatorBatch_()) return false;
    setStatusText(QStringLiteral("Liquidations capture desired for %1 stream(s)").arg(coordinators_.size()));
    registerLiveSources_();
    refreshState(detail::CaptureRefreshMode::Full);
    return true;
}

void CaptureViewModel::stopLiquidations() {
    desiredLiquidationsRunning_ = false;
    for (auto& entry : coordinators_) {
        if (entry.coordinator) entry.coordinator->requestStopLiquidations();
    }
    setStatusText(QStringLiteral("Liquidations stop requested"));
    refreshState(detail::CaptureRefreshMode::Full);
}
bool CaptureViewModel::startBookTicker() {
    desiredBookTickerRunning_ = true;
    if (!reconcileCoordinatorBatch_()) return false;
    setStatusText(QStringLiteral("BookTicker capture desired for %1 stream(s)").arg(coordinators_.size()));
    registerLiveSources_();
    refreshState(detail::CaptureRefreshMode::Full);
    return true;
}

void CaptureViewModel::stopBookTicker() {
    desiredBookTickerRunning_ = false;
    for (auto& entry : coordinators_) {
        if (entry.coordinator) entry.coordinator->requestStopBookTicker();
    }
    setStatusText(QStringLiteral("BookTicker stop requested"));
    refreshState(detail::CaptureRefreshMode::Full);
}


bool CaptureViewModel::startCandles() {
    if (!ensureCoordinatorBatch_()) return false;

    bool ok = true;
    for (auto& entry : coordinators_) {
        if (!entry.coordinator) continue;
        const auto sessionStatus = entry.coordinator->ensureSession(entry.config);
        if (!isOk(sessionStatus)) {
            ok = false;
            continue;
        }
        const auto candleStatus = entry.coordinator->captureCandlesOnce(entry.config);
        if (!isOk(candleStatus)) ok = false;
    }

    setStatusText(ok
        ? QStringLiteral("Candles history requested for %1 stream(s)").arg(coordinators_.size())
        : joinCoordinatorErrors_());
    registerLiveSources_();
    refreshState(detail::CaptureRefreshMode::Full);
    return ok;
}

bool CaptureViewModel::startDetailedCandles() {
    QString errorText;
    const auto endCandidates = detail::detailedCandlesEndCandidatesNs(detailedCandlesEndMode_,
                                                                      detailedCandlesEndUtcText_,
                                                                      detailedCandlesVenueKey_,
                                                                      detailedCandlesLeg2SymbolsText_.trimmed().isEmpty()
                                                                          ? QString{}
                                                                          : detailedCandlesLeg2VenueKey_,
                                                                      currentUtcNs(),
                                                                      nullptr,
                                                                      &errorText);
    if (endCandidates.empty()) {
        setStatusText(errorText.isEmpty() ? QStringLiteral("Enter detailed candles end time") : errorText);
        return false;
    }

    std::vector<capture::CaptureConfig> configs;
    std::int64_t selectedEndNs = 0;
    QStringList probeFailures;
    const bool directNowPath = endCandidates.size() == 1u && endCandidates.front() == 0;
    if (directNowPath) {
        errorText.clear();
        configs = detail::makeDetailedCandlesConfigs(outputDirectory_,
                                                     envPath_,
                                                     apiSlot_,
                                                     detailedCandlesVenueKey_,
                                                     detailedCandlesSymbolsText_,
                                                     detailedCandlesLeg2VenueKey_,
                                                     detailedCandlesLeg2SymbolsText_,
                                                     detailedCandlesTimeframe_,
                                                     detailedCandlesLimit_,
                                                     &errorText,
                                                     0);
        if (configs.empty()) {
            setStatusText(errorText.isEmpty() ? QStringLiteral("Enter detailed candles symbol") : errorText);
            return false;
        }
    } else {
        for (const auto endNs : endCandidates) {
            errorText.clear();
            auto candidateConfigs = detail::makeDetailedCandlesConfigs(outputDirectory_,
                                                                       envPath_,
                                                                       apiSlot_,
                                                                       detailedCandlesVenueKey_,
                                                                       detailedCandlesSymbolsText_,
                                                                       detailedCandlesLeg2VenueKey_,
                                                                       detailedCandlesLeg2SymbolsText_,
                                                                       detailedCandlesTimeframe_,
                                                                       detailedCandlesLimit_,
                                                                       &errorText,
                                                                       endNs);
            if (candidateConfigs.empty()) {
                setStatusText(errorText.isEmpty() ? QStringLiteral("Enter detailed candles symbol") : errorText);
                return false;
            }

            bool candidateOk = true;
            QStringList candidateFailures;
            for (const auto& config : candidateConfigs) {
                capture::CaptureCoordinator probe{};
                const auto probeStatus = probe.probeDetailedCandlesOnce(config);
                if (isOk(probeStatus)) continue;

                candidateOk = false;
                const auto symbol = config.symbols.empty() ? QString{} : QString::fromStdString(config.symbols.front());
                const auto failure = QString::fromStdString(probe.lastError()).trimmed();
                candidateFailures.push_back(QStringLiteral("%1/%2/%3/%4 end=%5: %6")
                    .arg(QString::fromStdString(config.exchange),
                         QString::fromStdString(config.market),
                         symbol,
                         QString::fromStdString(config.detailedCandlesTimeframe),
                         endLabelForStatus(endNs),
                         failure.isEmpty() ? QString::fromUtf8(hftrec::statusToString(probeStatus).data()) : failure));
            }

            if (candidateOk) {
                configs = std::move(candidateConfigs);
                selectedEndNs = endNs;
                break;
            }

            probeFailures = candidateFailures;
            bool retryable = true;
            for (const auto& failure : candidateFailures) {
                if (!retryableDetailedCandlesWindowError(failure)) {
                    retryable = false;
                    break;
                }
            }
            if (!retryable) break;
        }
    }

    if (configs.empty()) {
        setStatusText(probeFailures.isEmpty()
            ? QStringLiteral("Detailed candles2 failed: no valid smart end candidate")
            : QStringLiteral("Detailed candles2 failed: %1").arg(probeFailures.join(QStringLiteral(" | "))));
        return false;
    }

    bool ok = true;
    QStringList failures;
    QStringList successes;
    for (const auto& config : configs) {
        auto existing = std::find_if(coordinators_.begin(), coordinators_.end(), [&](const CoordinatorEntry& entry) {
            return configsMatch(entry.config, config);
        });
        if (existing == coordinators_.end()) {
            CoordinatorEntry entry{};
            entry.config = config;
            entry.coordinator = std::make_unique<capture::CaptureCoordinator>();
            coordinators_.push_back(std::move(entry));
            existing = std::prev(coordinators_.end());
        }
        if (!existing->coordinator) {
            ok = false;
            failures.push_back(QStringLiteral("missing coordinator"));
            continue;
        }
        existing->config = config;
        const auto beforeRows = existing->coordinator->candles2Count();
        const auto status = existing->coordinator->captureDetailedCandlesOnce(existing->config);
        const auto afterRows = existing->coordinator->candles2Count();
        const auto symbol = config.symbols.empty() ? QString{} : QString::fromStdString(config.symbols.front());
        const auto venue = QStringLiteral("%1/%2/%3/%4")
            .arg(QString::fromStdString(config.exchange),
                 QString::fromStdString(config.market),
                 symbol,
                 QString::fromStdString(config.detailedCandlesTimeframe));
        if (!isOk(status)) {
            ok = false;
            const auto error = QString::fromStdString(existing->coordinator->lastError()).trimmed();
            const auto statusText = QString::fromUtf8(hftrec::statusToString(status).data());
            failures.push_back(error.isEmpty()
                ? QStringLiteral("%1: %2").arg(venue, statusText)
                : QStringLiteral("%1: %2").arg(venue, error));
            continue;
        }
        const auto written = afterRows >= beforeRows ? (afterRows - beforeRows) : afterRows;
        successes.push_back(QStringLiteral("%1 rows=%2/%3")
            .arg(venue,
                 QString::number(written),
                 QString::number(config.detailedCandlesLimit)));
    }

    if (ok) {
        setStatusText(QStringLiteral("Detailed candles2 downloaded end=%1: %2")
            .arg(endLabelForStatus(selectedEndNs), successes.join(QStringLiteral(" | "))));
    } else if (!failures.isEmpty()) {
        setStatusText(QStringLiteral("Detailed candles2 failed: %1").arg(failures.join(QStringLiteral(" | "))));
    } else {
        setStatusText(joinCoordinatorErrors_());
    }
    refreshState(detail::CaptureRefreshMode::Full);
    return ok;
}

bool CaptureViewModel::startOrderbook() {
    desiredOrderbookRunning_ = true;
    if (!reconcileCoordinatorBatch_()) return false;
    setStatusText(QStringLiteral("Orderbook capture desired for %1 stream(s)").arg(coordinators_.size()));
    registerLiveSources_();
    refreshState(detail::CaptureRefreshMode::Full);
    return true;
}

void CaptureViewModel::stopOrderbook() {
    desiredOrderbookRunning_ = false;
    for (auto& entry : coordinators_) {
        if (entry.coordinator) entry.coordinator->requestStopOrderbook();
    }
    setStatusText(QStringLiteral("Orderbook stop requested"));
    refreshState(detail::CaptureRefreshMode::Full);
}

bool CaptureViewModel::startMarkPrice() {
    desiredMarkPriceRunning_ = true;
    if (!reconcileCoordinatorBatch_()) return false;
    setStatusText(QStringLiteral("MarkPrice capture desired for %1 stream(s)").arg(coordinators_.size()));
    registerLiveSources_();
    refreshState(detail::CaptureRefreshMode::Full);
    return true;
}

void CaptureViewModel::stopMarkPrice() {
    desiredMarkPriceRunning_ = false;
    for (auto& entry : coordinators_) {
        if (entry.coordinator) entry.coordinator->requestStopMarkPrice();
    }
    setStatusText(QStringLiteral("MarkPrice stop requested"));
    refreshState(detail::CaptureRefreshMode::Full);
}

bool CaptureViewModel::startIndexPrice() {
    desiredIndexPriceRunning_ = true;
    if (!reconcileCoordinatorBatch_()) return false;
    setStatusText(QStringLiteral("IndexPrice capture desired for %1 stream(s)").arg(coordinators_.size()));
    registerLiveSources_();
    refreshState(detail::CaptureRefreshMode::Full);
    return true;
}

void CaptureViewModel::stopIndexPrice() {
    desiredIndexPriceRunning_ = false;
    for (auto& entry : coordinators_) {
        if (entry.coordinator) entry.coordinator->requestStopIndexPrice();
    }
    setStatusText(QStringLiteral("IndexPrice stop requested"));
    refreshState(detail::CaptureRefreshMode::Full);
}

bool CaptureViewModel::startFunding() {
    desiredFundingRunning_ = true;
    if (!reconcileCoordinatorBatch_()) return false;
    setStatusText(QStringLiteral("Funding capture desired for %1 stream(s)").arg(coordinators_.size()));
    registerLiveSources_();
    refreshState(detail::CaptureRefreshMode::Full);
    return true;
}

void CaptureViewModel::stopFunding() {
    desiredFundingRunning_ = false;
    for (auto& entry : coordinators_) {
        if (entry.coordinator) entry.coordinator->requestStopFunding();
    }
    setStatusText(QStringLiteral("Funding stop requested"));
    refreshState(detail::CaptureRefreshMode::Full);
}

bool CaptureViewModel::startPriceLimit() {
    desiredPriceLimitRunning_ = true;
    if (!reconcileCoordinatorBatch_()) return false;
    setStatusText(QStringLiteral("PriceLimit capture desired for %1 stream(s)").arg(coordinators_.size()));
    registerLiveSources_();
    refreshState(detail::CaptureRefreshMode::Full);
    return true;
}

void CaptureViewModel::stopPriceLimit() {
    desiredPriceLimitRunning_ = false;
    for (auto& entry : coordinators_) {
        if (entry.coordinator) entry.coordinator->requestStopPriceLimit();
    }
    setStatusText(QStringLiteral("PriceLimit stop requested"));
    refreshState(detail::CaptureRefreshMode::Full);
}

bool CaptureViewModel::startOpenInterest() {
    setStatusText(QStringLiteral("OpenInterest display is disabled; no data file was created"));
    return false;
}

bool CaptureViewModel::startAllChannels() {
    desiredTradesRunning_ = true;
    desiredLiquidationsRunning_ = true;
    desiredBookTickerRunning_ = true;
    desiredOrderbookRunning_ = true;
    desiredMarkPriceRunning_ = true;
    desiredIndexPriceRunning_ = true;
    desiredFundingRunning_ = true;
    desiredPriceLimitRunning_ = true;
    if (!reconcileCoordinatorBatch_()) return false;

    bool candlesOk = true;
    for (auto& entry : coordinators_) {
        if (!entry.coordinator) continue;
        const auto candleStatus = entry.coordinator->captureCandlesOnce(entry.config);
        if (!isOk(candleStatus)) candlesOk = false;
    }

    setStatusText(candlesOk
        ? QStringLiteral("All available capture channels desired for %1 stream(s)").arg(coordinators_.size())
        : joinCoordinatorErrors_());
    registerLiveSources_();
    refreshState(detail::CaptureRefreshMode::Full);
    return candlesOk;
}

void CaptureViewModel::stopAllChannels() {
    desiredTradesRunning_ = false;
    desiredLiquidationsRunning_ = false;
    desiredBookTickerRunning_ = false;
    desiredOrderbookRunning_ = false;
    desiredMarkPriceRunning_ = false;
    desiredIndexPriceRunning_ = false;
    desiredFundingRunning_ = false;
    desiredPriceLimitRunning_ = false;
    for (auto& entry : coordinators_) {
        if (!entry.coordinator) continue;
        entry.coordinator->requestStopTrades();
        entry.coordinator->requestStopLiquidations();
        entry.coordinator->requestStopBookTicker();
        entry.coordinator->requestStopOrderbook();
        entry.coordinator->requestStopMarkPrice();
        entry.coordinator->requestStopIndexPrice();
        entry.coordinator->requestStopFunding();
        entry.coordinator->requestStopPriceLimit();
    }
    setStatusText(QStringLiteral("All capture channels stop requested"));
    refreshState(detail::CaptureRefreshMode::Full);
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
    refreshState(detail::CaptureRefreshMode::Full);
}

bool CaptureViewModel::ensureCoordinatorBatch_() {
    return reconcileCoordinatorBatch_();
}

bool CaptureViewModel::reconcileCoordinatorBatch_() {
    const auto configs = makeConfigs();
    const QString missingSymbols = detail::missingVenueSymbolsText(selectedVenueKeys_, venueSymbolsTexts_);
    if (configs.empty()) {
        for (auto& entry : coordinators_) {
            if (entry.coordinator) (void)entry.coordinator->finalizeSession();
        }
        coordinators_.clear();
        viewer::LiveDataRegistry::instance().clear();
        publishActiveLiveSources_();
        setStatusText(missingSymbols.isEmpty() ? QStringLiteral("Enter at least one venue symbol") : missingSymbols);
        return false;
    }
    if (!missingSymbols.isEmpty()) {
        setStatusText(missingSymbols);
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

    bool anyRunning = false;
    bool anyFailed = false;
    for (auto& entry : coordinators_) {
        if (!entry.coordinator) continue;
        if (!startDesiredChannels(*entry.coordinator,
                                  entry.config,
                                  desiredTradesRunning_,
                                  desiredLiquidationsRunning_,
                                  desiredBookTickerRunning_,
                                  desiredOrderbookRunning_,
                                  desiredMarkPriceRunning_,
                                  desiredIndexPriceRunning_,
                                  desiredFundingRunning_,
                                  desiredPriceLimitRunning_)) {
            anyFailed = true;
        }
        anyRunning = anyRunning || hasRunningChannel(*entry.coordinator);
    }

    if (anyFailed && !anyRunning) {
        setStatusText(joinCoordinatorErrors_());
        refreshState(detail::CaptureRefreshMode::Full);
        return false;
    }

    return true;
}

void CaptureViewModel::reconcileActiveChannels_() {
    if (!(desiredTradesRunning_ || desiredLiquidationsRunning_ || desiredBookTickerRunning_ || desiredOrderbookRunning_
          || desiredMarkPriceRunning_ || desiredIndexPriceRunning_ || desiredFundingRunning_ || desiredPriceLimitRunning_)) return;
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
        const bool hasLiveChannel = coordinator->tradesRunning() || coordinator->liquidationsRunning()
            || coordinator->bookTickerRunning() || coordinator->orderbookRunning()
            || coordinator->markPriceRunning() || coordinator->indexPriceRunning()
            || coordinator->fundingRunning() || coordinator->priceLimitRunning();
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
        descriptor.insert(QStringLiteral("startedAtNs"), static_cast<qlonglong>(manifest.startedAtNs));
        descriptor.insert(QStringLiteral("liveAvailable"), true);
        descriptor.insert(QStringLiteral("bookTickerRunning"), coordinator->bookTickerRunning());
        descriptor.insert(QStringLiteral("bookTickerCount"), static_cast<int>(coordinator->bookTickerCount()));
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
        coordinator->stopMarkPrice();
        coordinator->stopIndexPrice();
        coordinator->stopFunding();
        coordinator->stopPriceLimit();
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


