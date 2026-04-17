#pragma once

#include <QObject>
#include <QString>
#include <QTimer>

#include "core/capture/CaptureCoordinator.hpp"

namespace hftrec::gui {

class CaptureViewModel : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString outputDirectory READ outputDirectory WRITE setOutputDirectory NOTIFY outputDirectoryChanged)
    Q_PROPERTY(QString sessionId READ sessionId NOTIFY sessionStateChanged)
    Q_PROPERTY(QString sessionPath READ sessionPath NOTIFY sessionStateChanged)
    Q_PROPERTY(QString statusText READ statusText NOTIFY statusTextChanged)
    Q_PROPERTY(bool tradesRunning READ tradesRunning NOTIFY channelStateChanged)
    Q_PROPERTY(bool bookTickerRunning READ bookTickerRunning NOTIFY channelStateChanged)
    Q_PROPERTY(bool orderbookRunning READ orderbookRunning NOTIFY channelStateChanged)
    Q_PROPERTY(qulonglong tradesCount READ tradesCount NOTIFY countersChanged)
    Q_PROPERTY(qulonglong bookTickerCount READ bookTickerCount NOTIFY countersChanged)
    Q_PROPERTY(qulonglong depthCount READ depthCount NOTIFY countersChanged)

  public:
    explicit CaptureViewModel(QObject* parent = nullptr);

    QString outputDirectory() const;
    QString sessionId() const;
    QString sessionPath() const;
    QString statusText() const;
    bool tradesRunning() const;
    bool bookTickerRunning() const;
    bool orderbookRunning() const;
    qulonglong tradesCount() const;
    qulonglong bookTickerCount() const;
    qulonglong depthCount() const;

    Q_INVOKABLE void setOutputDirectory(const QString& outputDirectory);
    Q_INVOKABLE bool startTrades();
    Q_INVOKABLE void stopTrades();
    Q_INVOKABLE bool startBookTicker();
    Q_INVOKABLE void stopBookTicker();
    Q_INVOKABLE bool startOrderbook();
    Q_INVOKABLE void stopOrderbook();
    Q_INVOKABLE void finalizeSession();

  signals:
    void outputDirectoryChanged();
    void sessionStateChanged();
    void statusTextChanged();
    void channelStateChanged();
    void countersChanged();

  private:
    capture::CaptureConfig makeConfig() const;
    void refreshState();
    void setStatusText(const QString& statusText);
    void setStatusFromStatus(hftrec::Status status, const QString& okText);

    capture::CaptureCoordinator coordinator_{};
    QTimer refreshTimer_{};
    QString outputDirectory_{"./recordings"};
    QString statusText_{"Ready to capture ETHUSDT into canonical JSON session folders"};
    QString lastSessionId_{};
    QString lastSessionPath_{};
    bool lastTradesRunning_{false};
    bool lastBookTickerRunning_{false};
    bool lastOrderbookRunning_{false};
    qulonglong lastTradesCount_{0};
    qulonglong lastBookTickerCount_{0};
    qulonglong lastDepthCount_{0};
};

}  // namespace hftrec::gui
