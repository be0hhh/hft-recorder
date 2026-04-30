#pragma once

#include <QObject>
#include <QString>

#include <functional>
#include <thread>

namespace hftrec::gui {

class TradingControlViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString symbol READ symbol WRITE setSymbol NOTIFY symbolChanged)
    Q_PROPERTY(int apiSlot READ apiSlot WRITE setApiSlot NOTIFY apiSlotChanged)
    Q_PROPERTY(bool cxetAvailable READ cxetAvailable CONSTANT)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool instrumentInfoLoaded READ instrumentInfoLoaded NOTIFY instrumentInfoChanged)
    Q_PROPERTY(bool bookTickerRunning READ bookTickerRunning NOTIFY bookTickerRunningChanged)
    Q_PROPERTY(bool userStreamRunning READ userStreamRunning NOTIFY userStreamRunningChanged)
    Q_PROPERTY(bool orderSessionRunning READ orderSessionRunning NOTIFY orderSessionRunningChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusChanged)
    Q_PROPERTY(QString instrumentInfoText READ instrumentInfoText NOTIFY instrumentInfoChanged)
    Q_PROPERTY(QString symbolConfigText READ symbolConfigText NOTIFY symbolConfigChanged)
    Q_PROPERTY(QString marginTypeText READ marginTypeText NOTIFY symbolConfigChanged)
    Q_PROPERTY(QString leverageText READ leverageText NOTIFY symbolConfigChanged)
    Q_PROPERTY(QString maxNotionalText READ maxNotionalText NOTIFY symbolConfigChanged)
    Q_PROPERTY(QString lastBookTickerText READ lastBookTickerText NOTIFY bookTickerChanged)
    Q_PROPERTY(QString bidPriceText READ bidPriceText NOTIFY bookTickerChanged)
    Q_PROPERTY(QString bidQtyText READ bidQtyText NOTIFY bookTickerChanged)
    Q_PROPERTY(QString askPriceText READ askPriceText NOTIFY bookTickerChanged)
    Q_PROPERTY(QString askQtyText READ askQtyText NOTIFY bookTickerChanged)
    Q_PROPERTY(QString spreadText READ spreadText NOTIFY bookTickerChanged)
    Q_PROPERTY(QString lastUserEventText READ lastUserEventText NOTIFY userEventChanged)
    Q_PROPERTY(QString lastOrderText READ lastOrderText NOTIFY orderEventChanged)
    Q_PROPERTY(QString lastRateLimitText READ lastRateLimitText NOTIFY orderEventChanged)
    Q_PROPERTY(QString walletBalanceText READ walletBalanceText NOTIFY accountChanged)
    Q_PROPERTY(QString availableBalanceText READ availableBalanceText NOTIFY accountChanged)
    Q_PROPERTY(QString equityText READ equityText NOTIFY accountChanged)
    Q_PROPERTY(QString unrealizedPnlText READ unrealizedPnlText NOTIFY accountChanged)
    Q_PROPERTY(QString realizedPnlText READ realizedPnlText NOTIFY accountChanged)
    Q_PROPERTY(QString openOrderCountText READ openOrderCountText NOTIFY orderEventChanged)
    Q_PROPERTY(QString filledTradeCountText READ filledTradeCountText NOTIFY orderEventChanged)
    Q_PROPERTY(QString canceledOrderCountText READ canceledOrderCountText NOTIFY orderEventChanged)
    Q_PROPERTY(QString rejectedOrderCountText READ rejectedOrderCountText NOTIFY orderEventChanged)

  public:
    explicit TradingControlViewModel(QObject* parent = nullptr);
    ~TradingControlViewModel() override;

    QString symbol() const { return symbol_; }
    int apiSlot() const noexcept { return apiSlot_; }
    bool cxetAvailable() const noexcept;
    bool busy() const noexcept { return busy_; }
    bool instrumentInfoLoaded() const noexcept { return instrumentInfoLoaded_; }
    bool bookTickerRunning() const noexcept { return bookTickerRunning_; }
    bool userStreamRunning() const noexcept { return userStreamRunning_; }
    bool orderSessionRunning() const noexcept { return orderSessionRunning_; }
    QString statusText() const { return statusText_; }
    QString instrumentInfoText() const { return instrumentInfoText_; }
    QString symbolConfigText() const { return symbolConfigText_; }
    QString marginTypeText() const { return marginTypeText_; }
    QString leverageText() const { return leverageText_; }
    QString maxNotionalText() const { return maxNotionalText_; }
    QString lastBookTickerText() const { return lastBookTickerText_; }
    QString bidPriceText() const { return bidPriceText_; }
    QString bidQtyText() const { return bidQtyText_; }
    QString askPriceText() const { return askPriceText_; }
    QString askQtyText() const { return askQtyText_; }
    QString spreadText() const { return spreadText_; }
    QString lastUserEventText() const { return lastUserEventText_; }
    QString lastOrderText() const { return lastOrderText_; }
    QString lastRateLimitText() const { return lastRateLimitText_; }
    QString walletBalanceText() const { return walletBalanceText_; }
    QString availableBalanceText() const { return availableBalanceText_; }
    QString equityText() const { return equityText_; }
    QString unrealizedPnlText() const { return unrealizedPnlText_; }
    QString realizedPnlText() const { return realizedPnlText_; }
    QString openOrderCountText() const { return openOrderCountText_; }
    QString filledTradeCountText() const { return filledTradeCountText_; }
    QString canceledOrderCountText() const { return canceledOrderCountText_; }
    QString rejectedOrderCountText() const { return rejectedOrderCountText_; }

    Q_INVOKABLE void setSymbol(const QString& symbol);
    Q_INVOKABLE void setApiSlot(int apiSlot);
    Q_INVOKABLE void fetchInstrumentInfo();
    Q_INVOKABLE void refreshSymbolConfig();
    Q_INVOKABLE void setLeverage(int leverage);
    Q_INVOKABLE void setMarginType(const QString& marginType);
    Q_INVOKABLE void startBookTicker();
    Q_INVOKABLE void stopBookTicker();
    Q_INVOKABLE void startUserStream();
    Q_INVOKABLE void stopUserStream();
    Q_INVOKABLE void startOrderSession();
    Q_INVOKABLE void stopOrderSession();
    Q_INVOKABLE void hardStopAll();
    Q_INVOKABLE void cancelOrder(const QString& origClientOrderId);
    Q_INVOKABLE void refreshSnapshot();

  signals:
    void symbolChanged();
    void apiSlotChanged();
    void busyChanged();
    void instrumentInfoChanged();
    void symbolConfigChanged();
    void bookTickerRunningChanged();
    void bookTickerChanged();
    void userStreamRunningChanged();
    void userEventChanged();
    void orderSessionRunningChanged();
    void orderEventChanged();
    void accountChanged();
    void statusChanged();

  private:
    void runAsync_(std::function<void()> action);
    void stopActionThread_();
    void setBusy_(bool busy);
    void setStatus_(const QString& text);
    bool configureCxet_();
    QString normalizedSymbol_() const;

    QString symbol_{QStringLiteral("BTCUSDT")};
    int apiSlot_{1};
    bool busy_{false};
    bool instrumentInfoLoaded_{false};
    bool bookTickerRunning_{false};
    bool userStreamRunning_{false};
    bool orderSessionRunning_{false};
    QString statusText_{QStringLiteral("Ready")};
    QString instrumentInfoText_{QStringLiteral("not loaded")};
    QString symbolConfigText_{QStringLiteral("not loaded")};
    QString marginTypeText_{QStringLiteral("-")};
    QString leverageText_{QStringLiteral("-")};
    QString maxNotionalText_{QStringLiteral("-")};
    QString lastBookTickerText_{QStringLiteral("no data")};
    QString bidPriceText_{QStringLiteral("-")};
    QString bidQtyText_{QStringLiteral("-")};
    QString askPriceText_{QStringLiteral("-")};
    QString askQtyText_{QStringLiteral("-")};
    QString spreadText_{QStringLiteral("-")};
    QString lastUserEventText_{QStringLiteral("no data")};
    QString lastOrderText_{QStringLiteral("no data")};
    QString lastRateLimitText_{QStringLiteral("no data")};
    QString walletBalanceText_{QStringLiteral("0")};
    QString availableBalanceText_{QStringLiteral("0")};
    QString equityText_{QStringLiteral("0")};
    QString unrealizedPnlText_{QStringLiteral("0")};
    QString realizedPnlText_{QStringLiteral("0")};
    QString openOrderCountText_{QStringLiteral("0")};
    QString filledTradeCountText_{QStringLiteral("0")};
    QString canceledOrderCountText_{QStringLiteral("0")};
    QString rejectedOrderCountText_{QStringLiteral("0")};

    std::thread actionThread_{};
};

}  // namespace hftrec::gui
