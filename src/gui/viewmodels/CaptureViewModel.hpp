#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QVariantList>
#include <memory>
#include <vector>

#include "core/capture/CaptureCoordinator.hpp"

namespace hftrec::gui {

class CaptureViewModel;

namespace detail {
enum class CaptureRefreshMode;
struct CaptureBatchSnapshot;
CaptureBatchSnapshot collectBatchSnapshot(const CaptureViewModel& viewModel, CaptureRefreshMode mode);
}

class CaptureViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString outputDirectory READ outputDirectory WRITE setOutputDirectory NOTIFY outputDirectoryChanged)
    Q_PROPERTY(QString envPath READ envPath WRITE setEnvPath NOTIFY envSettingsChanged)
    Q_PROPERTY(int apiSlot READ apiSlot WRITE setApiSlot NOTIFY envSettingsChanged)
    Q_PROPERTY(QStringList selectedVenueKeys READ selectedVenueKeys NOTIFY venueChanged)
    Q_PROPERTY(QVariantList venueChoices READ venueChoices CONSTANT)
    Q_PROPERTY(QString symbolsText READ symbolsText WRITE setSymbolsText NOTIFY symbolsTextChanged)
    Q_PROPERTY(QString normalizedSymbolsText READ normalizedSymbolsText NOTIFY symbolsTextChanged)
    Q_PROPERTY(int tradesHistoryWarmupSec READ tradesHistoryWarmupSec WRITE setTradesHistoryWarmupSec NOTIFY tradesHistoryWarmupSecChanged)
    Q_PROPERTY(QStringList tradesAvailableAliases READ tradesAvailableAliases NOTIFY requestBuilderChanged)
    Q_PROPERTY(QStringList liquidationsAvailableAliases READ liquidationsAvailableAliases NOTIFY requestBuilderChanged)
    Q_PROPERTY(QStringList bookTickerAvailableAliases READ bookTickerAvailableAliases NOTIFY requestBuilderChanged)
    Q_PROPERTY(QStringList orderbookAvailableAliases READ orderbookAvailableAliases NOTIFY requestBuilderChanged)
    Q_PROPERTY(QString tradesRequestPreview READ tradesRequestPreview NOTIFY requestBuilderChanged)
    Q_PROPERTY(QString liquidationsRequestPreview READ liquidationsRequestPreview NOTIFY requestBuilderChanged)
    Q_PROPERTY(QString bookTickerRequestPreview READ bookTickerRequestPreview NOTIFY requestBuilderChanged)
    Q_PROPERTY(QString orderbookRequestPreview READ orderbookRequestPreview NOTIFY requestBuilderChanged)
    Q_PROPERTY(QString sessionId READ sessionId NOTIFY sessionStateChanged)
    Q_PROPERTY(QString sessionPath READ sessionPath NOTIFY sessionStateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(QVariantList activeLiveSources READ activeLiveSources NOTIFY activeLiveSourcesChanged)
    Q_PROPERTY(bool captureAvailable READ captureAvailable CONSTANT)
    Q_PROPERTY(QString captureUnavailableReason READ captureUnavailableReason CONSTANT)
    Q_PROPERTY(bool sessionOpen READ sessionOpen NOTIFY sessionStateChanged)
    Q_PROPERTY(bool tradesRunning READ tradesRunning NOTIFY channelStateChanged)
    Q_PROPERTY(bool liquidationsRunning READ liquidationsRunning NOTIFY channelStateChanged)
    Q_PROPERTY(bool bookTickerRunning READ bookTickerRunning NOTIFY channelStateChanged)
    Q_PROPERTY(bool orderbookRunning READ orderbookRunning NOTIFY channelStateChanged)
    Q_PROPERTY(bool markPriceRunning READ markPriceRunning NOTIFY channelStateChanged)
    Q_PROPERTY(bool indexPriceRunning READ indexPriceRunning NOTIFY channelStateChanged)
    Q_PROPERTY(bool fundingRunning READ fundingRunning NOTIFY channelStateChanged)
    Q_PROPERTY(bool priceLimitRunning READ priceLimitRunning NOTIFY channelStateChanged)
    Q_PROPERTY(qulonglong tradesCount READ tradesCount NOTIFY countersChanged)
    Q_PROPERTY(qulonglong liquidationsCount READ liquidationsCount NOTIFY countersChanged)
    Q_PROPERTY(qulonglong bookTickerCount READ bookTickerCount NOTIFY countersChanged)
    Q_PROPERTY(qulonglong markPriceCount READ markPriceCount NOTIFY countersChanged)
    Q_PROPERTY(qulonglong indexPriceCount READ indexPriceCount NOTIFY countersChanged)
    Q_PROPERTY(qulonglong fundingCount READ fundingCount NOTIFY countersChanged)
    Q_PROPERTY(qulonglong priceLimitCount READ priceLimitCount NOTIFY countersChanged)
    Q_PROPERTY(qulonglong candlesCount READ candlesCount NOTIFY countersChanged)
    Q_PROPERTY(qulonglong depthCount READ depthCount NOTIFY countersChanged)

  public:
    explicit CaptureViewModel(QObject* parent = nullptr);

    QString outputDirectory() const;
    QString envPath() const;
    int apiSlot() const noexcept;
    QStringList selectedVenueKeys() const;
    QVariantList venueChoices() const;
    QString symbolsText() const;
    QString normalizedSymbolsText() const;
    int tradesHistoryWarmupSec() const noexcept;
    QStringList tradesAvailableAliases() const;
    QStringList liquidationsAvailableAliases() const;
    QStringList bookTickerAvailableAliases() const;
    QStringList orderbookAvailableAliases() const;
    QString tradesRequestPreview() const;
    QString liquidationsRequestPreview() const;
    QString bookTickerRequestPreview() const;
    QString orderbookRequestPreview() const;
    QString sessionId() const;
    QString sessionPath() const;
    QString statusText() const;
    QVariantList activeLiveSources() const;
    bool captureAvailable() const noexcept;
    QString captureUnavailableReason() const;
    bool sessionOpen() const;
    bool tradesRunning() const;
    bool liquidationsRunning() const;
    bool bookTickerRunning() const;
    bool orderbookRunning() const;
    bool markPriceRunning() const;
    bool indexPriceRunning() const;
    bool fundingRunning() const;
    bool priceLimitRunning() const;
    qulonglong tradesCount() const;
    qulonglong liquidationsCount() const;
    qulonglong bookTickerCount() const;
    qulonglong markPriceCount() const;
    qulonglong indexPriceCount() const;
    qulonglong fundingCount() const;
    qulonglong priceLimitCount() const;
    qulonglong candlesCount() const;
    qulonglong depthCount() const;

    Q_INVOKABLE void setOutputDirectory(const QString& outputDirectory);
    Q_INVOKABLE void setEnvPath(const QString& envPath);
    Q_INVOKABLE void setApiSlot(int apiSlot);
    Q_INVOKABLE void toggleVenue(const QString& venueKey);
    Q_INVOKABLE bool isVenueSelected(const QString& venueKey) const;
    Q_INVOKABLE QString venueSymbolsText(const QString& venueKey) const;
    Q_INVOKABLE void setVenueSymbolsText(const QString& venueKey, const QString& symbolsText);
    Q_INVOKABLE void setSymbolsText(const QString& symbolsText);
    Q_INVOKABLE void applyGlobalSymbolsToVenues();
    Q_INVOKABLE void setTradesHistoryWarmupSec(int seconds);
    Q_INVOKABLE void toggleAlias(const QString& channel, const QString& alias);
    Q_INVOKABLE bool isAliasSelected(const QString& channel, const QString& alias) const;
    Q_INVOKABLE bool isRequiredAlias(const QString& channel, const QString& alias) const;
    Q_INVOKABLE QString aliasDisplayText(const QString& channel, const QString& alias) const;
    Q_INVOKABLE QString channelWeightSummary(const QString& channel) const;
    Q_INVOKABLE bool startTrades();
    Q_INVOKABLE void stopTrades();
    Q_INVOKABLE bool startLiquidations();
    Q_INVOKABLE void stopLiquidations();
    Q_INVOKABLE bool startBookTicker();
    Q_INVOKABLE void stopBookTicker();
    Q_INVOKABLE bool startCandles();
    Q_INVOKABLE bool startOrderbook();
    Q_INVOKABLE void stopOrderbook();
    Q_INVOKABLE bool startMarkPrice();
    Q_INVOKABLE void stopMarkPrice();
    Q_INVOKABLE bool startIndexPrice();
    Q_INVOKABLE void stopIndexPrice();
    Q_INVOKABLE bool startFunding();
    Q_INVOKABLE void stopFunding();
    Q_INVOKABLE bool startPriceLimit();
    Q_INVOKABLE void stopPriceLimit();
    Q_INVOKABLE bool startOpenInterest();
    Q_INVOKABLE bool startAllChannels();
    Q_INVOKABLE void stopAllChannels();
    Q_INVOKABLE void finalizeSession();
    Q_INVOKABLE void refreshStats();

  signals:
    void outputDirectoryChanged();
    void envSettingsChanged();
    void venueChanged();
    void symbolsTextChanged();
    void tradesHistoryWarmupSecChanged();
    void requestBuilderChanged();
    void sessionStateChanged();
    void statusTextChanged();
    void activeLiveSourcesChanged();
    void channelStateChanged();
    void countersChanged();

  private:
    friend detail::CaptureBatchSnapshot detail::collectBatchSnapshot(const CaptureViewModel& viewModel, detail::CaptureRefreshMode mode);

    struct CoordinatorEntry {
        capture::CaptureConfig config{};
        std::unique_ptr<capture::CaptureCoordinator> coordinator{};
    };

    std::vector<capture::CaptureConfig> makeConfigs() const;
    QStringList* selectedAliasesForChannel_(const QString& channel);
    const QStringList* selectedAliasesForChannel_(const QString& channel) const;
    const QStringList* availableAliasesForChannel_(const QString& channel) const;
    bool ensureCoordinatorBatch_();
    bool reconcileCoordinatorBatch_();
    void reconcileActiveChannels_();
    void registerLiveSources_();
    void abortCoordinatorBatch_(const QString& fallbackStatus);
    void clearCoordinatorBatch_();
    bool anyChannelRunning_() const noexcept;
    void refreshState(detail::CaptureRefreshMode mode);
    void setStatusText(const QString& statusText);
    void setStatusFromStatus(hftrec::Status status, const QString& okText);
    QString joinCoordinatorErrors_() const;
    void publishActiveLiveSources_();

    std::vector<CoordinatorEntry> coordinators_{};
    QTimer refreshTimer_{};
    QString outputDirectory_{"./recordings"};
    QString envPath_{"./.env"};
    int apiSlot_{1};
    QStringList selectedVenueKeys_{
        QStringLiteral("binance_futures"),
        QStringLiteral("binance_spot"),
        QStringLiteral("bybit_futures"),
        QStringLiteral("kucoin_futures"),
        QStringLiteral("gate_futures"),
        QStringLiteral("bitget_futures"),
    };
    QStringList venueSymbolsTexts_{};
    QString symbolsText_{"ETHUSDT"};
    int tradesHistoryWarmupSec_{300};
    QStringList tradesAvailableAliases_{};
    QStringList liquidationsAvailableAliases_{};
    QStringList bookTickerAvailableAliases_{};
    QStringList orderbookAvailableAliases_{};
    QStringList selectedTradesAliases_{};
    QStringList selectedLiquidationsAliases_{};
    QStringList selectedBookTickerAliases_{};
    QStringList selectedOrderbookAliases_{};
    QString statusText_{"Ready to capture symbols into canonical JSON session folders"};
    QVariantList activeLiveSources_{};
    QString lastSessionId_{};
    QString lastSessionPath_{};
    bool lastTradesRunning_{false};
    bool lastLiquidationsRunning_{false};
    bool lastBookTickerRunning_{false};
    bool lastOrderbookRunning_{false};
    bool lastMarkPriceRunning_{false};
    bool lastIndexPriceRunning_{false};
    bool lastFundingRunning_{false};
    bool lastPriceLimitRunning_{false};
    bool desiredTradesRunning_{false};
    bool desiredLiquidationsRunning_{false};
    bool desiredBookTickerRunning_{false};
    bool desiredOrderbookRunning_{false};
    bool desiredMarkPriceRunning_{false};
    bool desiredIndexPriceRunning_{false};
    bool desiredFundingRunning_{false};
    bool desiredPriceLimitRunning_{false};
    qulonglong lastTradesCount_{0};
    qulonglong lastLiquidationsCount_{0};
    qulonglong lastBookTickerCount_{0};
    qulonglong lastMarkPriceCount_{0};
    qulonglong lastIndexPriceCount_{0};
    qulonglong lastFundingCount_{0};
    qulonglong lastPriceLimitCount_{0};
    qulonglong lastCandlesCount_{0};
    qulonglong lastDepthCount_{0};
};

}  // namespace hftrec::gui
