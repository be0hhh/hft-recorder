#pragma once

#include <QObject>
#include <QTcpServer>

#include <cstdint>

class QTcpSocket;

namespace hftrec::gui::api {

class LocalVenueWsServer final : public QObject {
    Q_OBJECT

  public:
    explicit LocalVenueWsServer(QObject* parent = nullptr);
    ~LocalVenueWsServer() override;

    bool startFromEnvironment();
    void stop();
    bool running() const noexcept { return server_.isListening(); }

  private slots:
    void acceptConnection_();

  private:
    struct ClientState;

    void handleReadyRead_(QTcpSocket* socket);
    bool handleHandshake_(QTcpSocket* socket, ClientState& state);
    bool handleFrames_(QTcpSocket* socket, ClientState& state);
    void handleTextFrame_(QTcpSocket* socket, const QByteArray& payload);
    void sendText_(QTcpSocket* socket, const QByteArray& payload);
    void sendPong_(QTcpSocket* socket, const QByteArray& payload);
    void sendClose_(QTcpSocket* socket);

    QTcpServer server_{};
};

}  // namespace hftrec::gui::api
