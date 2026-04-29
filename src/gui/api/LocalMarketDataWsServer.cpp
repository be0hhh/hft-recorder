#include "gui/api/LocalMarketDataWsServer.hpp"

#include <QAbstractSocket>
#include <QByteArray>
#include <QCryptographicHash>
#include <QHostAddress>
#include <QMetaObject>
#include <QUrl>
#include <QUrlQuery>
#include <QTcpSocket>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <utility>

namespace hftrec::gui::api {
namespace {

constexpr const char* kWsGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
constexpr const char* kDefaultPath = "/api/v1/local-marketdata/ws";

struct ParsedFrame {
    std::uint8_t opcode{0};
    QByteArray payload{};
};

std::uint16_t wsPort() noexcept {
    const auto raw = qEnvironmentVariable("HFTREC_MARKETDATA_WS_PORT", "18082").trimmed();
    bool ok = false;
    const int parsed = raw.toInt(&ok);
    if (!ok || parsed <= 0 || parsed > 65535) return 18082u;
    return static_cast<std::uint16_t>(parsed);
}

QHostAddress wsHost() {
    const auto raw = qEnvironmentVariable("HFTREC_MARKETDATA_WS_HOST", "127.0.0.1").trimmed();
    QHostAddress address;
    if (!address.setAddress(raw)) return QHostAddress::LocalHost;
    return address;
}

bool wsDisabled() noexcept {
    return qEnvironmentVariable("HFTREC_MARKETDATA_WS_MODE").trimmed().toLower() == QStringLiteral("off");
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

QByteArray requestTarget(const QByteArray& header) {
    const int lineEnd = header.indexOf('\n');
    const QByteArray firstLine = (lineEnd >= 0 ? header.left(lineEnd) : header).trimmed();
    const QList<QByteArray> parts = firstLine.split(' ');
    if (parts.size() < 2) return QByteArrayLiteral(kDefaultPath);
    return parts[1];
}

bool parseTarget(const QByteArray& target, QString& channel, QString& symbol) {
    QUrl url(QString::fromLatin1(target));
    if (!url.isValid()) url = QUrl(QString::fromLatin1(kDefaultPath));
    const QUrlQuery query(url);
    channel = query.queryItemValue(QStringLiteral("channel")).trimmed().toLower();
    symbol = query.queryItemValue(QStringLiteral("symbol")).trimmed().toLower();
    if (channel.isEmpty()) channel = QStringLiteral("trades");
    if (channel == QStringLiteral("orderbook")) channel = QStringLiteral("orderbook.delta");
    return true;
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

LocalMarketDataWsServer::LocalMarketDataWsServer(QObject* parent) : QObject(parent) {}

LocalMarketDataWsServer::~LocalMarketDataWsServer() {
    stop();
}

bool LocalMarketDataWsServer::startFromEnvironment() {
    if (wsDisabled()) return false;
    if (server_.isListening()) return true;
    connect(&server_, &QTcpServer::newConnection, this, &LocalMarketDataWsServer::acceptConnection_, Qt::UniqueConnection);
    const bool ok = server_.listen(wsHost(), wsPort());
    if (ok) hftrec::local_exchange::globalLocalMarketDataBus().setSink(this);
    return ok;
}

void LocalMarketDataWsServer::stop() {
    hftrec::local_exchange::globalLocalMarketDataBus().setSink(nullptr);
    server_.close();
    for (auto it = clients_.begin(); it != clients_.end(); ++it) {
        if (it.key() != nullptr) it.key()->disconnectFromHost();
    }
    clients_.clear();
}

void LocalMarketDataWsServer::onLocalMarketDataFrame(std::string_view channel,
                                                     std::string_view symbol,
                                                     std::string_view payload) noexcept {
    const QString qChannel = QString::fromLatin1(channel.data(), static_cast<qsizetype>(channel.size())).toLower();
    const QString qSymbol = QString::fromLatin1(symbol.data(), static_cast<qsizetype>(symbol.size())).toLower();
    const QByteArray bytes(payload.data(), static_cast<qsizetype>(payload.size()));
    QMetaObject::invokeMethod(this, [this, qChannel, qSymbol, bytes]() { broadcast_(qChannel, qSymbol, bytes); }, Qt::QueuedConnection);
}

void LocalMarketDataWsServer::acceptConnection_() {
    while (server_.hasPendingConnections()) {
        QTcpSocket* socket = server_.nextPendingConnection();
        if (socket == nullptr) continue;
        socket->setParent(this);
        clients_.insert(socket, ClientState{});
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { handleReadyRead_(socket); });
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            clients_.remove(socket);
            socket->deleteLater();
        });
    }
}

void LocalMarketDataWsServer::handleReadyRead_(QTcpSocket* socket) {
    auto it = clients_.find(socket);
    if (it == clients_.end()) return;
    ClientState& state = it.value();
    state.buffer += socket->readAll();
    if (!state.handshaken && !handleHandshake_(socket, state)) return;
    handleFrames_(socket, state);
}

bool LocalMarketDataWsServer::handleHandshake_(QTcpSocket* socket, ClientState& state) {
    const int headerEnd = state.buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) return false;
    const QByteArray header = state.buffer.left(headerEnd);
    state.buffer.remove(0, headerEnd + 4);
    const QByteArray key = headerValue(header, QByteArrayLiteral("Sec-WebSocket-Key"));
    if (key.isEmpty()) {
        socket->disconnectFromHost();
        return false;
    }
    parseTarget(requestTarget(header), state.channel, state.symbol);
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

bool LocalMarketDataWsServer::handleFrames_(QTcpSocket* socket, ClientState& state) {
    ParsedFrame frame{};
    while (parseOneFrame(state.buffer, frame)) {
        if (frame.opcode == 0x8u) {
            sendClose_(socket);
            socket->disconnectFromHost();
            return false;
        }
        if (frame.opcode == 0x9u) sendPong_(socket, frame.payload);
    }
    return true;
}

void LocalMarketDataWsServer::broadcast_(const QString& channel, const QString& symbol, const QByteArray& payload) {
    QList<QTcpSocket*> stale;
    for (auto it = clients_.begin(); it != clients_.end(); ++it) {
        QTcpSocket* socket = it.key();
        const ClientState& state = it.value();
        if (socket == nullptr || socket->state() != QAbstractSocket::ConnectedState) {
            stale.push_back(socket);
            continue;
        }
        if (!state.handshaken) continue;
        if (state.channel != channel) continue;
        if (!state.symbol.isEmpty() && state.symbol != symbol) continue;
        sendText_(socket, payload);
    }
    for (QTcpSocket* socket : stale) clients_.remove(socket);
}

void LocalMarketDataWsServer::sendText_(QTcpSocket* socket, const QByteArray& payload) {
    QByteArray frame;
    frame.append(static_cast<char>(0x81));
    const std::uint64_t len = static_cast<std::uint64_t>(payload.size());
    if (len < 126u) {
        frame.append(static_cast<char>(len));
    } else if (len <= 65535u) {
        frame.append(static_cast<char>(126));
        frame.append(static_cast<char>((len >> 8u) & 0xFFu));
        frame.append(static_cast<char>(len & 0xFFu));
    } else {
        frame.append(static_cast<char>(127));
        for (int shift = 56; shift >= 0; shift -= 8) {
            frame.append(static_cast<char>((len >> static_cast<unsigned>(shift)) & 0xFFu));
        }
    }
    frame.append(payload);
    socket->write(frame);
}

void LocalMarketDataWsServer::sendPong_(QTcpSocket* socket, const QByteArray& payload) {
    const QByteArray bounded = payload.left(125);
    QByteArray frame;
    frame.append(static_cast<char>(0x8A));
    frame.append(static_cast<char>(bounded.size()));
    frame.append(bounded);
    socket->write(frame);
}

void LocalMarketDataWsServer::sendClose_(QTcpSocket* socket) {
    static constexpr char closeFrame[] = {static_cast<char>(0x88), static_cast<char>(0x00)};
    socket->write(closeFrame, sizeof(closeFrame));
}

}  // namespace hftrec::gui::api
