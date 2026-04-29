#pragma once

#include <QObject>
#include <QTcpServer>
#include <QHash>
#include <QSet>
#include <QString>

#include <cstdint>

#include "core/execution/ExecutionVenue.hpp"

class QTcpSocket;

namespace hftrec::gui::api {

class LocalVenueWsServer final : public QObject, public hftrec::execution::IExecutionEventSink {
    Q_OBJECT

  public:
    explicit LocalVenueWsServer(QObject* parent = nullptr);
    ~LocalVenueWsServer() override;

    bool startFromEnvironment();
    void stop();
    bool running() const noexcept { return server_.isListening(); }
    void setDownstreamSink(hftrec::execution::IExecutionEventSink* sink) noexcept { downstreamSink_ = sink; }
    void onExecutionEvent(const hftrec::execution::ExecutionEvent& event) noexcept override;

  private slots:
    void acceptConnection_();

  private:
    struct ClientState;

    void handleReadyRead_(QTcpSocket* socket);
    bool handleHandshake_(QTcpSocket* socket, ClientState& state);
    bool handleFrames_(QTcpSocket* socket, ClientState& state);
    void handleTextFrame_(QTcpSocket* socket, const QByteArray& payload);
    void sendText_(QTcpSocket* socket, const QByteArray& payload);
    void broadcastUserEvent_(const hftrec::execution::ExecutionEvent& event);
    void appendAuditEvent_(const hftrec::execution::ExecutionEvent& event, const QByteArray& payload);
    void sendPong_(QTcpSocket* socket, const QByteArray& payload);
    void sendClose_(QTcpSocket* socket);

    QTcpServer server_{};
    QHash<QTcpSocket*, bool> authenticatedClients_{};
    QHash<QTcpSocket*, QString> clientApiKeys_{};
    QHash<QString, QSet<QString>> nonceCache_{};
    QHash<QTcpSocket*, bool> userStreamClients_{};
    hftrec::execution::IExecutionEventSink* downstreamSink_{nullptr};
};

}  // namespace hftrec::gui::api
