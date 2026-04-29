#pragma once

#include <QByteArray>
#include <QHash>
#include <QObject>
#include <QTcpServer>
#include <QString>

#include <string_view>

#include "core/local_exchange/LocalMarketDataBus.hpp"

class QTcpSocket;

namespace hftrec::gui::api {

class LocalMarketDataWsServer final : public QObject, public hftrec::local_exchange::ILocalMarketDataSink {
    Q_OBJECT

  public:
    explicit LocalMarketDataWsServer(QObject* parent = nullptr);
    ~LocalMarketDataWsServer() override;

    bool startFromEnvironment();
    void stop();
    bool running() const noexcept { return server_.isListening(); }

    void onLocalMarketDataFrame(std::string_view channel,
                                std::string_view symbol,
                                std::string_view payload) noexcept override;

  private slots:
    void acceptConnection_();

  private:
    struct ClientState {
        QByteArray buffer{};
        bool handshaken{false};
        QString channel{};
        QString symbol{};
    };

    void handleReadyRead_(QTcpSocket* socket);
    bool handleHandshake_(QTcpSocket* socket, ClientState& state);
    bool handleFrames_(QTcpSocket* socket, ClientState& state);
    void broadcast_(const QString& channel, const QString& symbol, const QByteArray& payload);
    void sendText_(QTcpSocket* socket, const QByteArray& payload);
    void sendPong_(QTcpSocket* socket, const QByteArray& payload);
    void sendClose_(QTcpSocket* socket);

    QTcpServer server_{};
    QHash<QTcpSocket*, ClientState> clients_{};
};

}  // namespace hftrec::gui::api
