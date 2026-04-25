#pragma once

#include <QObject>
#include <QTcpServer>

class QTcpSocket;

namespace hftrec::gui::viewer {
class ChartController;
}

namespace hftrec::gui::api {

class ChartApiServer final : public QObject {
    Q_OBJECT

  public:
    explicit ChartApiServer(QObject* parent = nullptr);
    ~ChartApiServer() override;

    void setChartController(hftrec::gui::viewer::ChartController* controller) noexcept;
    bool startFromEnvironment();
    void stop();
    bool running() const noexcept { return server_.isListening(); }

  private slots:
    void acceptConnection_();

  private:
    void handleReadyRead_(QTcpSocket* socket);
    void respond_(QTcpSocket* socket, int statusCode, const QByteArray& statusText,
                  const QByteArray& contentType, const QByteArray& body);
    QByteArray healthBody_() const;
    QByteArray markerPost_(const QByteArray& body, int& statusCode, QByteArray& statusText);

    QTcpServer server_{};
    hftrec::gui::viewer::ChartController* controller_{nullptr};
};

}  // namespace hftrec::gui::api