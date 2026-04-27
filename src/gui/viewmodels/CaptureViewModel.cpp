#include "gui/viewmodels/CaptureViewModel.hpp"

#include "gui/viewmodels/CaptureViewModelInternal.hpp"

#include <QCoreApplication>
#include <QDir>

namespace hftrec::gui {

CaptureViewModel::CaptureViewModel(QObject* parent)
    : QObject(parent) {
    outputDirectory_ = QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(QStringLiteral("../../recordings"));
    tradesAvailableAliases_ = detail::loadAliasesForChannel("trades");
    liquidationsAvailableAliases_ = detail::loadAliasesForChannel("liquidations");
    bookTickerAvailableAliases_ = detail::loadAliasesForChannel("bookticker");
    orderbookAvailableAliases_ = detail::loadAliasesForChannel("orderbook");

    refreshTimer_.setInterval(250);
    connect(&refreshTimer_, &QTimer::timeout, this, &CaptureViewModel::refreshState);
    refreshTimer_.start();
    refreshState();
}

QString CaptureViewModel::outputDirectory() const { return outputDirectory_; }
QString CaptureViewModel::symbolsText() const { return symbolsText_; }
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
qulonglong CaptureViewModel::tradesCount() const { return lastTradesCount_; }
qulonglong CaptureViewModel::liquidationsCount() const { return lastLiquidationsCount_; }
qulonglong CaptureViewModel::bookTickerCount() const { return lastBookTickerCount_; }
qulonglong CaptureViewModel::depthCount() const { return lastDepthCount_; }
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
                                       symbolsText_);
}

QString CaptureViewModel::liquidationsRequestPreview() const {
    return detail::buildRequestPreview(QStringLiteral("liquidations"),
                                       liquidationsAvailableAliases_,
                                       selectedLiquidationsAliases_,
                                       symbolsText_);
}

QString CaptureViewModel::bookTickerRequestPreview() const {
    return detail::buildRequestPreview(QStringLiteral("bookticker"),
                                       bookTickerAvailableAliases_,
                                       selectedBookTickerAliases_,
                                       symbolsText_);
}

QString CaptureViewModel::orderbookRequestPreview() const {
    return detail::buildRequestPreview(QStringLiteral("orderbook"),
                                       orderbookAvailableAliases_,
                                       selectedOrderbookAliases_,
                                       symbolsText_);
}

void CaptureViewModel::setOutputDirectory(const QString& outputDirectory) {
    const auto normalized = outputDirectory.trimmed();
    if (normalized.isEmpty() || normalized == outputDirectory_) return;
    outputDirectory_ = normalized;
    emit outputDirectoryChanged();
}

void CaptureViewModel::setSymbolsText(const QString& symbolsText) {
    const auto normalized = symbolsText.simplified();
    if (normalized == symbolsText_) return;
    symbolsText_ = normalized;
    emit symbolsTextChanged();
    emit requestBuilderChanged();
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
                               symbolsText_,
                               tradesAvailableAliases_,
                               liquidationsAvailableAliases_,
                               bookTickerAvailableAliases_,
                               orderbookAvailableAliases_,
                               selectedTradesAliases_,
                               selectedLiquidationsAliases_,
                               selectedBookTickerAliases_,
                               selectedOrderbookAliases_);
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
