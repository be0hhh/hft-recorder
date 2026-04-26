#include "gui/api/LocalVenueWsServer.hpp"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QHash>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QTcpSocket>

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>

#include "composite/level_0/SendWsObject.hpp"
#include "core/local_exchange/LocalOrderEngine.hpp"
#include "network/local/hftrecorder/Protocol.hpp"

namespace hftrec::gui::api {
namespace {

constexpr const char* kProtocol = "hftrecorder.local.v1";
constexpr const char* kWsGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

struct ParsedFrame {
    std::uint8_t opcode{0};
    QByteArray payload{};
};

QHash<QTcpSocket*, bool>& handshakeState() {
    static QHash<QTcpSocket*, bool> state{};
    return state;
}

std::uint16_t wsPort() noexcept {
    const auto raw = qEnvironmentVariable("HFTREC_OBJECT_WS_PORT", "18081").trimmed();
    bool ok = false;
    const int parsed = raw.toInt(&ok);
    if (!ok || parsed <= 0 || parsed > 65535) return 18081u;
    return static_cast<std::uint16_t>(parsed);
}

QHostAddress wsHost() {
    const auto raw = qEnvironmentVariable("HFTREC_OBJECT_WS_HOST", "127.0.0.1").trimmed();
    QHostAddress address;
    if (!address.setAddress(raw)) return QHostAddress::LocalHost;
    return address;
}

bool wsDisabled() noexcept {
    return qEnvironmentVariable("HFTREC_OBJECT_WS_MODE").trimmed().toLower() == QStringLiteral("off");
}

std::uint64_t nowNs() noexcept {
    return static_cast<std::uint64_t>(QDateTime::currentMSecsSinceEpoch()) * 1000000ull;
}

QByteArray headerValue(const QByteArray& header, const QByteArray& name) {
    const QList<QByteArray> lines = header.split('\n');
    const QByteArray prefix = name.toLower() + QByteArrayLiteral(":");
    for (QByteArray line : lines) {
        line = line.trimmed();
        if (line.toLower().startsWith(prefix)) return line.mid(prefix.size()).trimmed();
    }
    return {};
}

bool parseInt64Value(const QJsonValue& value, std::int64_t& out) noexcept {
    if (value.isString()) {
        bool ok = false;
        const qint64 parsed = value.toString().trimmed().toLongLong(&ok, 10);
        if (!ok) return false;
        out = parsed;
        return true;
    }
    if (!value.isDouble()) return false;
    const qint64 invalid = std::numeric_limits<qint64>::min();
    const qint64 parsed = value.toInteger(invalid);
    if (parsed == invalid) return false;
    out = parsed;
    return true;
}

bool parseUInt64Value(const QJsonValue& value, std::uint64_t& out) noexcept {
    std::int64_t signedValue = 0;
    if (!parseInt64Value(value, signedValue) || signedValue < 0) return false;
    out = static_cast<std::uint64_t>(signedValue);
    return true;
}

bool copySymbol(const QString& symbol, char* out, std::size_t outSize) noexcept {
    if (out == nullptr || outSize == 0u) return false;
    const QByteArray bytes = symbol.trimmed().toLatin1();
    if (bytes.isEmpty()) return false;
    const qsizetype n = std::min<qsizetype>(bytes.size(), static_cast<qsizetype>(outSize - 1u));
    std::memcpy(out, bytes.constData(), static_cast<std::size_t>(n));
    out[n] = '\0';
    return true;
}

bool buildOrderRequest(const QJsonObject& root,
                       cxet::network::local::hftrecorder::OrderRequestFrame& frame,
                       QString& error) noexcept {
    if (root.value(QStringLiteral("protocol")).toString() != QString::fromLatin1(kProtocol)) {
        error = QStringLiteral("bad_protocol");
        return false;
    }
    if (root.value(QStringLiteral("op")).toString() != QStringLiteral("order.submit")) {
        error = QStringLiteral("bad_op");
        return false;
    }
    const QJsonObject order = root.value(QStringLiteral("order")).toObject();
    if (order.isEmpty()) {
        error = QStringLiteral("missing_order");
        return false;
    }

    frame = cxet::network::local::hftrecorder::OrderRequestFrame{};
    frame.operationRaw = static_cast<std::uint8_t>(cxet::UnifiedRequestSpec::Operation::SendWs);
    frame.objectRaw = static_cast<std::uint8_t>(cxet::composite::out::WsSendObject::Order);
    frame.exchangeRaw = static_cast<std::uint8_t>(order.value(QStringLiteral("exchange_raw")).toInt());
    frame.marketRaw = static_cast<std::uint8_t>(order.value(QStringLiteral("market_raw")).toInt());
    frame.typeRaw = static_cast<std::uint8_t>(order.value(QStringLiteral("type_raw")).toInt(cxet::UnifiedRequestSpec::kUnset));
    frame.subtypeRaw = static_cast<std::uint8_t>(order.value(QStringLiteral("subtype_raw")).toInt(cxet::UnifiedRequestSpec::kUnset));
    frame.apiSlotRaw = static_cast<std::uint8_t>(order.value(QStringLiteral("api_slot_raw")).toInt(1));
    if (frame.apiSlotRaw == 0u) frame.apiSlotRaw = 1u;
    frame.sideSet = static_cast<std::uint8_t>(order.value(QStringLiteral("side_set")).toInt());
    frame.sideRaw = static_cast<std::uint8_t>(order.value(QStringLiteral("side_raw")).toInt());
    if (!parseInt64Value(order.value(QStringLiteral("price_raw")), frame.priceRaw)) frame.priceRaw = 0;
    if (!parseInt64Value(order.value(QStringLiteral("quantity_raw")), frame.quantityRaw)) frame.quantityRaw = 0;
    parseUInt64Value(root.value(QStringLiteral("request_id")), frame.clientSeq);
    parseUInt64Value(root.value(QStringLiteral("client_send_ts_ns")), frame.sendTsNs);
    if (!copySymbol(order.value(QStringLiteral("symbol")).toString(), frame.symbol, sizeof(frame.symbol))) {
        error = QStringLiteral("missing_symbol");
        return false;
    }
    return true;
}

QByteArray ackJson(const QJsonObject& request,
                   bool ok,
                   const QString& error,
                   const cxet::network::local::hftrecorder::OrderAckFrame* ack) {
    QJsonObject out;
    out.insert(QStringLiteral("protocol"), QString::fromLatin1(kProtocol));
    out.insert(QStringLiteral("op"), QStringLiteral("ack"));
    out.insert(QStringLiteral("request_id"), request.value(QStringLiteral("request_id")));
    out.insert(QStringLiteral("ok"), ok);
    out.insert(QStringLiteral("recorder_ack_ts_ns"), QString::number(nowNs()));
    if (!ok) out.insert(QStringLiteral("error"), error);
    if (ack != nullptr) {
        out.insert(QStringLiteral("status_raw"), static_cast<int>(ack->statusRaw));
        out.insert(QStringLiteral("error_code"), static_cast<int>(ack->errorCode));
        out.insert(QStringLiteral("ts_ns"), QString::number(ack->tsNs));
        out.insert(QStringLiteral("symbol"), QString::fromLatin1(ack->symbol));
        out.insert(QStringLiteral("order_id"), QString::fromLatin1(ack->orderId));
    }
    return QJsonDocument(out).toJson(QJsonDocument::Compact);
}

bool parseOneFrame(QByteArray& buffer, ParsedFrame& frame) {
    if (buffer.size() < 2) return false;
    const auto* bytes = reinterpret_cast<const unsigned char*>(buffer.constData());
    const bool fin = (bytes[0] & 0x80u) != 0u;
    const std::uint8_t opcode = static_cast<std::uint8_t>(bytes[0] & 0x0Fu);
    const bool masked = (bytes[1] & 0x80u) != 0u;
    std::uint64_t len = static_cast<std::uint64_t>(bytes[1] & 0x7Fu);
    int pos = 2;
    if (!fin) {
        buffer.clear();
        return false;
    }
    if (len == 126u) {
        if (buffer.size() < pos + 2) return false;
        len = (static_cast<std::uint64_t>(bytes[pos]) << 8u) | bytes[pos + 1];
        pos += 2;
    } else if (len == 127u) {
        if (buffer.size() < pos + 8) return false;
        len = 0;
        for (int i = 0; i < 8; ++i) len = (len << 8u) | bytes[pos + i];
        pos += 8;
    }
    unsigned char mask[4]{};
    if (masked) {
        if (buffer.size() < pos + 4) return false;
        std::memcpy(mask, bytes + pos, sizeof(mask));
        pos += 4;
    }
    if (len > 65536u) {
        buffer.clear();
        return false;
    }
    if (buffer.size() < pos + static_cast<int>(len)) return false;
    QByteArray payload = buffer.mid(pos, static_cast<int>(len));
    if (masked) {
        for (int i = 0; i < payload.size(); ++i) payload[i] = static_cast<char>(payload[i] ^ mask[i % 4]);
    }
    buffer.remove(0, pos + static_cast<int>(len));
    frame.opcode = opcode;
    frame.payload = std::move(payload);
    return true;
}

}  // namespace

struct LocalVenueWsServer::ClientState {
    QByteArray buffer{};
    bool handshaken{false};
};

LocalVenueWsServer::LocalVenueWsServer(QObject* parent) : QObject(parent) {
    connect(&server_, &QTcpServer::newConnection, this, &LocalVenueWsServer::acceptConnection_);
}

LocalVenueWsServer::~LocalVenueWsServer() {
    stop();
}

bool LocalVenueWsServer::startFromEnvironment() {
    if (wsDisabled()) return false;
    if (server_.isListening()) return true;
    return server_.listen(wsHost(), wsPort());
}

void LocalVenueWsServer::stop() {
    server_.close();
}

void LocalVenueWsServer::acceptConnection_() {
    while (server_.hasPendingConnections()) {
        QTcpSocket* socket = server_.nextPendingConnection();
        if (socket == nullptr) continue;
        socket->setParent(this);
        socket->setProperty("hftrec_ws_buffer", QByteArray{});
        handshakeState().insert(socket, false);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { handleReadyRead_(socket); });
        connect(socket, &QTcpSocket::disconnected, socket, [socket]() {
            handshakeState().remove(socket);
            socket->deleteLater();
        });
    }
}

void LocalVenueWsServer::handleReadyRead_(QTcpSocket* socket) {
    ClientState state{};
    state.buffer = socket->property("hftrec_ws_buffer").toByteArray() + socket->readAll();
    state.handshaken = handshakeState().value(socket, false);

    if (!state.handshaken) {
        if (!handleHandshake_(socket, state)) {
            socket->setProperty("hftrec_ws_buffer", state.buffer);
            return;
        }
    }
    handleFrames_(socket, state);
    socket->setProperty("hftrec_ws_buffer", state.buffer);
    handshakeState().insert(socket, state.handshaken);
}

bool LocalVenueWsServer::handleHandshake_(QTcpSocket* socket, ClientState& state) {
    const int headerEnd = state.buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) return false;
    const QByteArray header = state.buffer.left(headerEnd);
    state.buffer.remove(0, headerEnd + 4);
    const QByteArray key = headerValue(header, QByteArrayLiteral("Sec-WebSocket-Key"));
    if (key.isEmpty()) {
        socket->disconnectFromHost();
        return false;
    }
    const QByteArray accept = QCryptographicHash::hash(key + QByteArrayLiteral(kWsGuid), QCryptographicHash::Sha1).toBase64();
    QByteArray response;
    response += QByteArrayLiteral("HTTP/1.1 101 Switching Protocols\r\n");
    response += QByteArrayLiteral("Upgrade: websocket\r\n");
    response += QByteArrayLiteral("Connection: Upgrade\r\n");
    response += QByteArrayLiteral("Sec-WebSocket-Accept: ") + accept + QByteArrayLiteral("\r\n\r\n");
    socket->write(response);
    state.handshaken = true;
    return true;
}

bool LocalVenueWsServer::handleFrames_(QTcpSocket* socket, ClientState& state) {
    ParsedFrame frame{};
    while (parseOneFrame(state.buffer, frame)) {
        if (frame.opcode == 0x1u) handleTextFrame_(socket, frame.payload);
        else if (frame.opcode == 0x8u) {
            sendClose_(socket);
            socket->disconnectFromHost();
            return false;
        } else if (frame.opcode == 0x9u) sendPong_(socket, frame.payload);
    }
    return true;
}

void LocalVenueWsServer::handleTextFrame_(QTcpSocket* socket, const QByteArray& payload) {
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        sendText_(socket, ackJson({}, false, QStringLiteral("invalid_json"), nullptr));
        return;
    }
    const QJsonObject root = doc.object();
    cxet::network::local::hftrecorder::OrderRequestFrame request{};
    QString error;
    if (!buildOrderRequest(root, request, error)) {
        sendText_(socket, ackJson(root, false, error, nullptr));
        return;
    }
    cxet::network::local::hftrecorder::OrderAckFrame ack{};
    const bool ok = hftrec::local_exchange::globalLocalOrderEngine().submitOrder(request, ack);
    sendText_(socket, ackJson(root, ok, ok ? QString{} : QStringLiteral("order_rejected"), &ack));
}

void LocalVenueWsServer::sendText_(QTcpSocket* socket, const QByteArray& payload) {
    QByteArray frame;
    frame.append(static_cast<char>(0x81));
    const qsizetype len = payload.size();
    if (len < 126) {
        frame.append(static_cast<char>(len));
    } else {
        frame.append(static_cast<char>(126));
        frame.append(static_cast<char>((len >> 8) & 0xFF));
        frame.append(static_cast<char>(len & 0xFF));
    }
    frame.append(payload);
    socket->write(frame);
}

void LocalVenueWsServer::sendPong_(QTcpSocket* socket, const QByteArray& payload) {
    const QByteArray bounded = payload.left(125);
    QByteArray frame;
    frame.append(static_cast<char>(0x8A));
    frame.append(static_cast<char>(bounded.size()));
    frame.append(bounded);
    socket->write(frame);
}

void LocalVenueWsServer::sendClose_(QTcpSocket* socket) {
    static constexpr char closeFrame[] = {static_cast<char>(0x88), static_cast<char>(0x00)};
    socket->write(closeFrame, sizeof(closeFrame));
}

}  // namespace hftrec::gui::api
