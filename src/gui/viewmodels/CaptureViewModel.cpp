#include "gui/viewmodels/CaptureViewModel.hpp"

#include "gui/viewmodels/CaptureViewModelInternal.hpp"

#include <QCoreApplication>
#include <QDir>

namespace hftrec::gui {

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
    };

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

void CaptureViewModel::setOutputDirectory(const QString& outputDirectory) {
    const auto normalized = outputDirectory.trimmed();
    if (normalized.isEmpty() || normalized == outputDirectory_) return;
    outputDirectory_ = normalized;
    emit outputDirectoryChanged();
}

void CaptureViewModel::setEnvPath(const QString& envPath) {
    const auto normalized = envPath.trimmed();
    if (normalized.isEmpty() || normalized == envPath_) return;
    envPath_ = normalized;
    emit envSettingsChanged();
    emit requestBuilderChanged();
    reconcileActiveChannels_();
}

void CaptureViewModel::setApiSlot(int apiSlot) {
    if (apiSlot < 1) apiSlot = 1;
    if (apiSlot > 255) apiSlot = 255;
    if (apiSlot == apiSlot_) return;
    apiSlot_ = apiSlot;
    emit envSettingsChanged();
    emit requestBuilderChanged();
    reconcileActiveChannels_();
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

    emit symbolsTextChanged();
    emit requestBuilderChanged();
    reconcileActiveChannels_();
}

void CaptureViewModel::setTradesHistoryWarmupSec(int seconds) {
    if (seconds < 0) seconds = 0;
    if (seconds > 86400) seconds = 86400;
    if (seconds == tradesHistoryWarmupSec_) return;
    tradesHistoryWarmupSec_ = seconds;
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

}  // namespace hftrec::gui
