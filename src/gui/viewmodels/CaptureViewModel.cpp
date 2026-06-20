#include "gui/viewmodels/CaptureViewModel.hpp"

#include "gui/viewmodels/CaptureViewModelInternal.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QVariantMap>

namespace hftrec::gui {

namespace {

constexpr int kDetailedCandlesMaxLimit = 1'000'000;

QString normalizeDetailedCandlesTimeframeForVenue(const QString& venueKey, QString timeframe) {
    timeframe = timeframe.trimmed();
    if (timeframe != QStringLiteral("1M")) timeframe = timeframe.toLower();
    const auto choices = detail::detailedCandlesTimeframeChoices(venueKey);
    for (const auto& rawChoice : choices) {
        const auto choice = rawChoice.toMap();
        if (choice.value(QStringLiteral("value")).toString() == timeframe) return timeframe;
    }
    return detail::defaultDetailedCandlesTimeframe(venueKey);
}

QString normalizeDetailedCandlesVenueKey(QString venueKey) {
    venueKey = venueKey.trimmed().toLower();
    if (venueKey.isEmpty()) return {};
    const auto choices = detail::detailedCandlesVenueChoices();
    for (const auto& rawChoice : choices) {
        const auto choice = rawChoice.toMap();
        if (choice.value(QStringLiteral("key")).toString() == venueKey) return venueKey;
    }
    return {};
}

void syncDetailedVenueFields(const QString& venueKey, QString& exchange, QString& market) {
    const auto key = venueKey.trimmed().toLower();
    const auto parts = key.split(QLatin1Char('_'), Qt::SkipEmptyParts);
    if (!parts.isEmpty()) exchange = parts.front();
    if (parts.size() >= 2) market = parts.back() == QStringLiteral("spot") ? QStringLiteral("spot") : QStringLiteral("futures");
}

}  // namespace

CaptureViewModel::CaptureViewModel(QObject* parent)
    : QObject(parent) {
    outputDirectory_ = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../../recordings"));
    envPath_ = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../../.env"));
    tradesAvailableAliases_ = detail::loadAliasesForChannel("trades");
    liquidationsAvailableAliases_ = detail::loadAliasesForChannel("liquidations");
    bookTickerAvailableAliases_ = detail::loadAliasesForChannel("bookticker");
    orderbookAvailableAliases_ = detail::loadAliasesForChannel("orderbook");
    venueSymbolsTexts_ = {
        QStringLiteral("BTCUSDT"),
        QStringLiteral("BTCUSDT"),
        QStringLiteral("BTCUSDT"),
        QStringLiteral("BTCUSDT"),
        QStringLiteral("XBTUSDTM"),
        QStringLiteral("BTC-USDT"),
        QStringLiteral("BTC_USDT"),
        QStringLiteral("BTC_USDT"),
        QStringLiteral("BTCUSDT"),
        QStringLiteral("BTCUSDT"),
        QStringLiteral("BTCUSDT"),
        QStringLiteral("BTCUSDT"),
        QStringLiteral("BTC-USDT-SWAP"),
        QStringLiteral("BTC-USDT"),
        QStringLiteral("SBER@MISX"),
        QStringLiteral("SBER@MISX"),
        QStringLiteral("BTCUSDT"),
    };
    loadSettings_();

    refreshTimer_.setInterval(1000);
    connect(&refreshTimer_, &QTimer::timeout, this, [this]() {
        refreshState(detail::CaptureRefreshMode::Light);
    });
    refreshTimer_.start();
    refreshState(detail::CaptureRefreshMode::Full);
}

QString CaptureViewModel::outputDirectory() const { return outputDirectory_; }
QString CaptureViewModel::envPath() const { return envPath_; }
int CaptureViewModel::apiSlot() const noexcept { return apiSlot_; }
QStringList CaptureViewModel::selectedVenueKeys() const { return selectedVenueKeys_; }
QVariantList CaptureViewModel::venueChoices() const { return detail::venueChoices(); }
QVariantList CaptureViewModel::detailedCandlesVenueChoices() const { return detail::detailedCandlesVenueChoices(); }
QString CaptureViewModel::symbolsText() const { return symbolsText_; }
int CaptureViewModel::tradesHistoryWarmupSec() const noexcept { return tradesHistoryWarmupSec_; }
QString CaptureViewModel::sessionId() const { return lastSessionId_; }
QString CaptureViewModel::sessionPath() const { return lastSessionPath_; }
QString CaptureViewModel::statusText() const { return statusText_; }
QVariantList CaptureViewModel::activeLiveSources() const { return activeLiveSources_; }

bool CaptureViewModel::captureAvailable() const noexcept {
#if HFTREC_WITH_CXET
    return true;
#else
    return false;
#endif
}

QString CaptureViewModel::captureUnavailableReason() const {
#if HFTREC_WITH_CXET
    return {};
#else
    return QStringLiteral("Built without CXETCPP: live capture and exchange parsing are unavailable.");
#endif
}
bool CaptureViewModel::sessionOpen() const { return !coordinators_.empty(); }
bool CaptureViewModel::tradesRunning() const { return lastTradesRunning_; }
bool CaptureViewModel::liquidationsRunning() const { return lastLiquidationsRunning_; }
bool CaptureViewModel::bookTickerRunning() const { return lastBookTickerRunning_; }
bool CaptureViewModel::orderbookRunning() const { return lastOrderbookRunning_; }
bool CaptureViewModel::markPriceRunning() const { return lastMarkPriceRunning_; }
bool CaptureViewModel::indexPriceRunning() const { return lastIndexPriceRunning_; }
bool CaptureViewModel::fundingRunning() const { return lastFundingRunning_; }
bool CaptureViewModel::priceLimitRunning() const { return lastPriceLimitRunning_; }
qulonglong CaptureViewModel::tradesCount() const { return lastTradesCount_; }
qulonglong CaptureViewModel::liquidationsCount() const { return lastLiquidationsCount_; }
qulonglong CaptureViewModel::bookTickerCount() const { return lastBookTickerCount_; }
qulonglong CaptureViewModel::markPriceCount() const { return lastMarkPriceCount_; }
qulonglong CaptureViewModel::indexPriceCount() const { return lastIndexPriceCount_; }
qulonglong CaptureViewModel::fundingCount() const { return lastFundingCount_; }
qulonglong CaptureViewModel::priceLimitCount() const { return lastPriceLimitCount_; }
qulonglong CaptureViewModel::candlesCount() const { return lastCandlesCount_; }
qulonglong CaptureViewModel::candles2Count() const { return lastCandles2Count_; }
qulonglong CaptureViewModel::depthCount() const { return lastDepthCount_; }
void CaptureViewModel::refreshStats() { refreshState(detail::CaptureRefreshMode::Full); }
QStringList CaptureViewModel::tradesAvailableAliases() const { return tradesAvailableAliases_; }
QStringList CaptureViewModel::liquidationsAvailableAliases() const { return liquidationsAvailableAliases_; }
QStringList CaptureViewModel::bookTickerAvailableAliases() const { return bookTickerAvailableAliases_; }
QStringList CaptureViewModel::orderbookAvailableAliases() const { return orderbookAvailableAliases_; }

QString CaptureViewModel::normalizedSymbolsText() const {
    const auto symbols = detail::normalizedSymbols(symbolsText_);
    QStringList parts;
    parts.reserve(static_cast<qsizetype>(symbols.size()));
    for (const auto& symbol : symbols) parts.push_back(QString::fromStdString(symbol));
    return parts.join(' ');
}

QString CaptureViewModel::tradesRequestPreview() const {
    return detail::buildRequestPreview(QStringLiteral("trades"),
                                       tradesAvailableAliases_,
                                       selectedTradesAliases_,
                                       selectedVenueKeys_,
                                       venueSymbolsTexts_,
                                       symbolsText_,
                                       apiSlot_);
}

QString CaptureViewModel::liquidationsRequestPreview() const {
    return detail::buildRequestPreview(QStringLiteral("liquidations"),
                                       liquidationsAvailableAliases_,
                                       selectedLiquidationsAliases_,
                                       selectedVenueKeys_,
                                       venueSymbolsTexts_,
                                       symbolsText_,
                                       apiSlot_);
}

QString CaptureViewModel::bookTickerRequestPreview() const {
    return detail::buildRequestPreview(QStringLiteral("bookticker"),
                                       bookTickerAvailableAliases_,
                                       selectedBookTickerAliases_,
                                       selectedVenueKeys_,
                                       venueSymbolsTexts_,
                                       symbolsText_,
                                       apiSlot_);
}

QString CaptureViewModel::orderbookRequestPreview() const {
    return detail::buildRequestPreview(QStringLiteral("orderbook"),
                                       orderbookAvailableAliases_,
                                       selectedOrderbookAliases_,
                                       selectedVenueKeys_,
                                       venueSymbolsTexts_,
                                       symbolsText_,
                                       apiSlot_);
}

QString CaptureViewModel::detailedCandlesVenueKey() const { return detailedCandlesVenueKey_; }
QString CaptureViewModel::detailedCandlesExchange() const { return detailedCandlesExchange_; }
QString CaptureViewModel::detailedCandlesMarket() const { return detailedCandlesMarket_; }
QString CaptureViewModel::detailedCandlesSymbolsText() const { return detailedCandlesSymbolsText_; }
QString CaptureViewModel::detailedCandlesTimeframe() const { return detailedCandlesTimeframe_; }
int CaptureViewModel::detailedCandlesLimit() const noexcept { return detailedCandlesLimit_; }
QVariantList CaptureViewModel::detailedCandlesTimeframeChoices() const {
    return detail::detailedCandlesTimeframeChoices(detailedCandlesVenueKey_);
}

QString CaptureViewModel::detailedCandlesLimitWarning() const {
    return QStringLiteral("Paged REST download stops when the exchange returns no older candles; written rows can be lower than requested.");
}

QString CaptureViewModel::detailedCandlesRequestPreview() const {
    return detail::buildDetailedCandlesPreview(detailedCandlesVenueKey_,
                                               detailedCandlesSymbolsText_,
                                               detailedCandlesTimeframe_,
                                               detailedCandlesLimit_);
}

void CaptureViewModel::setOutputDirectory(const QString& outputDirectory) {
    const auto normalized = outputDirectory.trimmed();
    if (normalized.isEmpty() || normalized == outputDirectory_) return;
    outputDirectory_ = normalized;
    saveSettings_();
    emit outputDirectoryChanged();
}

void CaptureViewModel::setEnvPath(const QString& envPath) {
    const auto normalized = envPath.trimmed();
    if (normalized.isEmpty() || normalized == envPath_) return;
    envPath_ = normalized;
    saveSettings_();
    emit envSettingsChanged();
    emit requestBuilderChanged();
    reconcileActiveChannels_();
}

void CaptureViewModel::setApiSlot(int apiSlot) {
    if (apiSlot < 1) apiSlot = 1;
    if (apiSlot > 255) apiSlot = 255;
    if (apiSlot == apiSlot_) return;
    apiSlot_ = apiSlot;
    saveSettings_();
    emit envSettingsChanged();
    emit requestBuilderChanged();
    reconcileActiveChannels_();
}

void CaptureViewModel::setDetailedCandlesVenueKey(const QString& venueKey) {
    const auto normalized = normalizeDetailedCandlesVenueKey(venueKey);
    if (normalized.isEmpty() || normalized == detailedCandlesVenueKey_) return;
    detailedCandlesVenueKey_ = normalized;
    syncDetailedVenueFields(detailedCandlesVenueKey_, detailedCandlesExchange_, detailedCandlesMarket_);
    detailedCandlesTimeframe_ = normalizeDetailedCandlesTimeframeForVenue(detailedCandlesVenueKey_, detailedCandlesTimeframe_);
    saveSettings_();
    emit detailedCandlesChanged();
}

void CaptureViewModel::setDetailedCandlesExchange(const QString& exchange) {
    const auto normalized = exchange.trimmed().toLower();
    if (normalized.isEmpty() || normalized == detailedCandlesExchange_) return;
    detailedCandlesExchange_ = normalized;
    saveSettings_();
    emit detailedCandlesChanged();
}

void CaptureViewModel::setDetailedCandlesMarket(const QString& market) {
    const auto normalized = market.trimmed().toLower();
    if (normalized.isEmpty() || normalized == detailedCandlesMarket_) return;
    detailedCandlesMarket_ = normalized;
    saveSettings_();
    emit detailedCandlesChanged();
}

void CaptureViewModel::setDetailedCandlesSymbolsText(const QString& symbolsText) {
    const auto normalized = symbolsText.trimmed();
    if (normalized == detailedCandlesSymbolsText_) return;
    detailedCandlesSymbolsText_ = normalized;
    saveSettings_();
    emit detailedCandlesChanged();
}

void CaptureViewModel::setDetailedCandlesTimeframe(const QString& timeframe) {
    const auto normalized = normalizeDetailedCandlesTimeframeForVenue(detailedCandlesVenueKey_, timeframe);
    if (normalized.isEmpty() || normalized == detailedCandlesTimeframe_) return;
    detailedCandlesTimeframe_ = normalized;
    saveSettings_();
    emit detailedCandlesChanged();
}

void CaptureViewModel::setDetailedCandlesLimit(int limit) {
    if (limit < 1) limit = 1;
    if (limit > kDetailedCandlesMaxLimit) limit = kDetailedCandlesMaxLimit;
    if (limit == detailedCandlesLimit_) return;
    detailedCandlesLimit_ = limit;
    saveSettings_();
    emit detailedCandlesChanged();
}

void CaptureViewModel::toggleVenue(const QString& venueKey) {
    const auto normalized = venueKey.trimmed().toLower();
    if (normalized.isEmpty()) return;
    const auto existingIndex = selectedVenueKeys_.indexOf(normalized);
    if (existingIndex >= 0) {
        if (selectedVenueKeys_.size() == 1) return;
        selectedVenueKeys_.removeAt(existingIndex);
    } else {
        selectedVenueKeys_.push_back(normalized);
    }
    saveSettings_();
    emit venueChanged();
    emit requestBuilderChanged();
    reconcileActiveChannels_();
}

bool CaptureViewModel::isVenueSelected(const QString& venueKey) const {
    return selectedVenueKeys_.contains(venueKey.trimmed().toLower());
}

QString CaptureViewModel::venueSymbolsText(const QString& venueKey) const {
    const auto choices = venueChoices();
    const auto key = venueKey.trimmed().toLower();
    for (qsizetype i = 0; i < choices.size() && i < venueSymbolsTexts_.size(); ++i) {
        const auto row = choices[i].toMap();
        if (row.value(QStringLiteral("key")).toString() == key) return venueSymbolsTexts_[i];
    }
    return {};
}

void CaptureViewModel::setVenueSymbolsText(const QString& venueKey, const QString& symbolsText) {
    const auto choices = venueChoices();
    const auto key = venueKey.trimmed().toLower();
    for (qsizetype i = 0; i < choices.size(); ++i) {
        const auto row = choices[i].toMap();
        if (row.value(QStringLiteral("key")).toString() != key) continue;
        while (venueSymbolsTexts_.size() <= i) venueSymbolsTexts_.push_back({});
        const auto normalized = symbolsText.trimmed();
        if (venueSymbolsTexts_[i] == normalized) return;
        venueSymbolsTexts_[i] = normalized;
        saveSettings_();
        emit symbolsTextChanged();
        emit requestBuilderChanged();
        reconcileActiveChannels_();
        return;
    }
}

void CaptureViewModel::setSymbolsText(const QString& symbolsText) {
    const auto normalized = symbolsText.trimmed();
    if (normalized == symbolsText_) return;
    symbolsText_ = normalized;
    saveSettings_();
    emit symbolsTextChanged();
    emit requestBuilderChanged();
    reconcileActiveChannels_();
}

QString CaptureViewModel::venueSymbolPlaceholder(const QString& venueKey) const {
    return detail::venueSymbolPlaceholder(venueKey);
}

void CaptureViewModel::applyGlobalSymbolsToVenues() {
    if (detail::normalizedSymbols(symbolsText_).empty()) return;

    const auto choices = venueChoices();
    while (venueSymbolsTexts_.size() < choices.size()) venueSymbolsTexts_.push_back({});
    for (qsizetype i = 0; i < choices.size(); ++i) {
        const auto venueKey = choices[i].toMap().value(QStringLiteral("key")).toString();
        venueSymbolsTexts_[i] = detail::venueSymbolsFromGlobalInput(venueKey, symbolsText_);
    }

    saveSettings_();
    emit symbolsTextChanged();
    emit requestBuilderChanged();
    reconcileActiveChannels_();
}

void CaptureViewModel::setTradesHistoryWarmupSec(int seconds) {
    if (seconds < 0) seconds = 0;
    if (seconds > 86400) seconds = 86400;
    if (seconds == tradesHistoryWarmupSec_) return;
    tradesHistoryWarmupSec_ = seconds;
    saveSettings_();
    emit tradesHistoryWarmupSecChanged();
    emit requestBuilderChanged();
    reconcileActiveChannels_();
}

void CaptureViewModel::toggleAlias(const QString& channel, const QString& alias) {
    auto* selectedAliases = selectedAliasesForChannel_(channel);
    const auto* availableAliases = availableAliasesForChannel_(channel);
    if (selectedAliases == nullptr || availableAliases == nullptr) return;
    if (!availableAliases->contains(alias, Qt::CaseInsensitive)) return;
    if (detail::isRequiredAliasForChannel(channel, alias)) {
        emit requestBuilderChanged();
        return;
    }

    const auto existingIndex = selectedAliases->indexOf(alias);
    if (existingIndex >= 0) selectedAliases->removeAt(existingIndex);
    else selectedAliases->push_back(alias);
    emit requestBuilderChanged();
}

bool CaptureViewModel::isAliasSelected(const QString& channel, const QString& alias) const {
    if (detail::isRequiredAliasForChannel(channel, alias)) return true;
    const auto* selectedAliases = selectedAliasesForChannel_(channel);
    return selectedAliases != nullptr && selectedAliases->contains(alias);
}

bool CaptureViewModel::isRequiredAlias(const QString& channel, const QString& alias) const {
    return detail::isRequiredAliasForChannel(channel, alias);
}

QString CaptureViewModel::aliasDisplayText(const QString& channel, const QString& alias) const {
    Q_UNUSED(channel);
    return QStringLiteral("%1 - %2B").arg(alias).arg(detail::aliasWeightBytes(alias));
}

QString CaptureViewModel::channelWeightSummary(const QString& channel) const {
    const auto* selectedAliases = selectedAliasesForChannel_(channel);
    return detail::channelWeightSummary(channel, selectedAliases != nullptr ? *selectedAliases : QStringList{});
}

std::vector<capture::CaptureConfig> CaptureViewModel::makeConfigs() const {
    return detail::makeConfigs(outputDirectory_,
                               envPath_,
                               apiSlot_,
                               selectedVenueKeys_,
                               venueSymbolsTexts_,
                               symbolsText_,
                               tradesAvailableAliases_,
                               liquidationsAvailableAliases_,
                               bookTickerAvailableAliases_,
                               orderbookAvailableAliases_,
                               selectedTradesAliases_,
                               selectedLiquidationsAliases_,
                               selectedBookTickerAliases_,
                               selectedOrderbookAliases_,
                               tradesHistoryWarmupSec_);
}

QStringList* CaptureViewModel::selectedAliasesForChannel_(const QString& channel) {
    if (channel == QStringLiteral("trades")) return &selectedTradesAliases_;
    if (channel == QStringLiteral("liquidations")) return &selectedLiquidationsAliases_;
    if (channel == QStringLiteral("bookticker")) return &selectedBookTickerAliases_;
    if (channel == QStringLiteral("orderbook")) return &selectedOrderbookAliases_;
    return nullptr;
}

const QStringList* CaptureViewModel::selectedAliasesForChannel_(const QString& channel) const {
    if (channel == QStringLiteral("trades")) return &selectedTradesAliases_;
    if (channel == QStringLiteral("liquidations")) return &selectedLiquidationsAliases_;
    if (channel == QStringLiteral("bookticker")) return &selectedBookTickerAliases_;
    if (channel == QStringLiteral("orderbook")) return &selectedOrderbookAliases_;
    return nullptr;
}

const QStringList* CaptureViewModel::availableAliasesForChannel_(const QString& channel) const {
    if (channel == QStringLiteral("trades")) return &tradesAvailableAliases_;
    if (channel == QStringLiteral("liquidations")) return &liquidationsAvailableAliases_;
    if (channel == QStringLiteral("bookticker")) return &bookTickerAvailableAliases_;
    if (channel == QStringLiteral("orderbook")) return &orderbookAvailableAliases_;
    return nullptr;
}

void CaptureViewModel::setStatusText(const QString& statusText) {
    if (statusText == statusText_) return;
    statusText_ = statusText;
    emit statusTextChanged();
}

void CaptureViewModel::setStatusFromStatus(hftrec::Status status, const QString& okText) {
    if (isOk(status)) {
        setStatusText(okText);
        return;
    }

    const auto errorText = joinCoordinatorErrors_();
    if (!errorText.isEmpty()) {
        setStatusText(errorText);
        return;
    }

    setStatusText(QString::fromUtf8(statusToString(status).data(), static_cast<int>(statusToString(status).size())));
}

void CaptureViewModel::loadSettings_() {
    const auto outputDirectory = settings_.value(QStringLiteral("capture/output_directory"), outputDirectory_).toString().trimmed();
    if (!outputDirectory.isEmpty()) outputDirectory_ = outputDirectory;

    const auto envPath = settings_.value(QStringLiteral("capture/env_path"), envPath_).toString().trimmed();
    if (!envPath.isEmpty()) envPath_ = envPath;

    int apiSlot = settings_.value(QStringLiteral("capture/api_slot"), apiSlot_).toInt();
    if (apiSlot < 1) apiSlot = 1;
    if (apiSlot > 255) apiSlot = 255;
    apiSlot_ = apiSlot;

    const auto selectedVenues = settings_.value(QStringLiteral("capture/selected_venue_keys")).toStringList();
    if (!selectedVenues.isEmpty()) selectedVenueKeys_ = selectedVenues;

    const auto venueSymbols = settings_.value(QStringLiteral("capture/venue_symbols_texts")).toStringList();
    if (!venueSymbols.isEmpty()) venueSymbolsTexts_ = venueSymbols;

    while (venueSymbolsTexts_.size() < venueChoices().size()) venueSymbolsTexts_.push_back({});

    const auto symbolsText = settings_.value(QStringLiteral("capture/symbols_text"), symbolsText_).toString().trimmed();
    if (!symbolsText.isEmpty()) symbolsText_ = symbolsText;

    int warmupSec = settings_.value(QStringLiteral("capture/trades_history_warmup_sec"), tradesHistoryWarmupSec_).toInt();
    if (warmupSec < 0) warmupSec = 0;
    if (warmupSec > 86400) warmupSec = 86400;
    tradesHistoryWarmupSec_ = warmupSec;

    const auto detailedVenueKey = normalizeDetailedCandlesVenueKey(
        settings_.value(QStringLiteral("capture/detailed_candles_venue_key"), detailedCandlesVenueKey_).toString());
    detailedCandlesVenueKey_ = detailedVenueKey.isEmpty() ? QStringLiteral("binance_futures") : detailedVenueKey;
    syncDetailedVenueFields(detailedCandlesVenueKey_, detailedCandlesExchange_, detailedCandlesMarket_);

    const auto detailedSymbol = settings_.value(QStringLiteral("capture/detailed_candles_symbol"), detailedCandlesSymbolsText_).toString().trimmed();
    if (!detailedSymbol.isEmpty()) detailedCandlesSymbolsText_ = detailedSymbol;

    const auto detailedTf = settings_.value(QStringLiteral("capture/detailed_candles_timeframe"), detailedCandlesTimeframe_).toString();
    detailedCandlesTimeframe_ = normalizeDetailedCandlesTimeframeForVenue(detailedCandlesVenueKey_, detailedTf);

    int detailedLimit = settings_.value(QStringLiteral("capture/detailed_candles_limit"), detailedCandlesLimit_).toInt();
    if (detailedLimit < 1) detailedLimit = 1;
    if (detailedLimit > kDetailedCandlesMaxLimit) detailedLimit = kDetailedCandlesMaxLimit;
    detailedCandlesLimit_ = detailedLimit;
}

void CaptureViewModel::saveSettings_() {
    settings_.setValue(QStringLiteral("capture/output_directory"), outputDirectory_);
    settings_.setValue(QStringLiteral("capture/env_path"), envPath_);
    settings_.setValue(QStringLiteral("capture/api_slot"), apiSlot_);
    settings_.setValue(QStringLiteral("capture/selected_venue_keys"), selectedVenueKeys_);
    settings_.setValue(QStringLiteral("capture/venue_symbols_texts"), venueSymbolsTexts_);
    settings_.setValue(QStringLiteral("capture/symbols_text"), symbolsText_);
    settings_.setValue(QStringLiteral("capture/trades_history_warmup_sec"), tradesHistoryWarmupSec_);
    settings_.setValue(QStringLiteral("capture/detailed_candles_venue_key"), detailedCandlesVenueKey_);
    settings_.setValue(QStringLiteral("capture/detailed_candles_symbol"), detailedCandlesSymbolsText_);
    settings_.setValue(QStringLiteral("capture/detailed_candles_timeframe"), detailedCandlesTimeframe_);
    settings_.setValue(QStringLiteral("capture/detailed_candles_limit"), detailedCandlesLimit_);
    settings_.sync();
}

}  // namespace hftrec::gui
