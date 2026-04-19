#pragma once

#include <QObject>
#include <QString>
#include <QSettings>
#include <QTimer>

namespace hftrec::gui {

class AppViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(qreal tradeAmountScale READ tradeAmountScale WRITE setTradeAmountScale NOTIFY tradeAmountScaleChanged)
    Q_PROPERTY(qreal bookBrightnessUsdRef READ bookBrightnessUsdRef WRITE setBookBrightnessUsdRef NOTIFY bookBrightnessUsdRefChanged)
    Q_PROPERTY(qreal bookMinVisibleUsd READ bookMinVisibleUsd WRITE setBookMinVisibleUsd NOTIFY bookMinVisibleUsdChanged)

  public:
    explicit AppViewModel(QObject* parent = nullptr);

    QString statusText() const;
    qreal tradeAmountScale() const noexcept { return tradeAmountScale_; }
    qreal bookBrightnessUsdRef() const noexcept { return bookBrightnessUsdRef_; }
    qreal bookMinVisibleUsd() const noexcept { return bookMinVisibleUsd_; }

    void setTradeAmountScale(qreal value);
    void setBookBrightnessUsdRef(qreal value);
    void setBookMinVisibleUsd(qreal value);

  signals:
    void statusTextChanged();
    void tradeAmountScaleChanged();
    void bookBrightnessUsdRefChanged();
    void bookMinVisibleUsdChanged();

  private:
    void loadSettings_();
    void flushSettings_();
    void markDirty_() noexcept { settingsDirty_ = true; }

    QString statusText_{"Ready for GUI-first capture and compression lab work"};
    qreal tradeAmountScale_{0.45};
    qreal bookBrightnessUsdRef_{15000.0};
    qreal bookMinVisibleUsd_{5000.0};
    bool settingsDirty_{false};
    QSettings settings_{};
    QTimer saveTimer_{};
};

}  // namespace hftrec::gui
