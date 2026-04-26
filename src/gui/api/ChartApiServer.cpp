#include "gui/api/ChartApiServer.hpp"

#include <QByteArray>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QProcessEnvironment>
#include <QTcpSocket>

#include <limits>

#include "gui/viewer/ChartController.hpp"

namespace hftrec::gui::api {
namespace {

QByteArray jsonBool(bool value) {
    return value ? QByteArrayLiteral("true") : QByteArrayLiteral("false");
}

std::uint16_t apiPort() noexcept {
    const auto raw = qEnvironmentVariable("HFTREC_API_PORT", "18080").trimmed();
    bool ok = false;
    const int parsed = raw.toInt(&ok);
    if (!ok || parsed <= 0 || parsed > 65535) return 18080u;
    return static_cast<std::uint16_t>(parsed);
}

QHostAddress apiHost() {
    const auto raw = qEnvironmentVariable("HFTREC_API_HOST", "127.0.0.1").trimmed();
    QHostAddress address;
    if (!address.setAddress(raw)) return QHostAddress::LocalHost;
    return address;
}

bool apiDisabled() noexcept {
    return qEnvironmentVariable("HFTREC_API_MODE").trimmed().toLower() == QStringLiteral("off");
}

QByteArray reasonPhrase(int statusCode) {
    switch (statusCode) {
        case 200: return QByteArrayLiteral("OK");
        case 400: return QByteArrayLiteral("Bad Request");
        case 404: return QByteArrayLiteral("Not Found");
        case 405: return QByteArrayLiteral("Method Not Allowed");
        case 409: return QByteArrayLiteral("Conflict");
        default: return QByteArrayLiteral("Internal Server Error");
    }
}

bool parseTimestampNs(const QJsonValue& value, qint64& tsNs) noexcept {
    if (value.isString()) {
        bool ok = false;
        const qint64 parsed = value.toString().trimmed().toLongLong(&ok, 10);
        if (!ok) return false;
        tsNs = parsed;
        return true;
    }
    if (!value.isDouble()) return false;
    const qint64 invalid = std::numeric_limits<qint64>::min();
    const qint64 parsed = value.toInteger(invalid);
    if (parsed == invalid) return false;
    tsNs = parsed;
    return true;
}

}  // namespace

ChartApiServer::ChartApiServer(QObject* parent) : QObject(parent) {
    connect(&server_, &QTcpServer::newConnection, this, &ChartApiServer::acceptConnection_);
}

ChartApiServer::~ChartApiServer() {
    stop();
}

void ChartApiServer::setChartController(hftrec::gui::viewer::ChartController* controller) noexcept {
    controller_ = controller;
}

hftrec::gui::viewer::ChartController* ChartApiServer::controller() const noexcept {
    // TODO: when Viewer cloning is introduced, replace this singleton target
    // with an explicit workspace chart-target policy.
    if (auto* active = hftrec::gui::viewer::ChartController::activeInstance()) return active;
    return controller_;
}

bool ChartApiServer::startFromEnvironment() {
    if (apiDisabled()) return false;
    if (server_.isListening()) return true;
    return server_.listen(apiHost(), apiPort());
}

void ChartApiServer::stop() {
    server_.close();
}

void ChartApiServer::acceptConnection_() {
    while (server_.hasPendingConnections()) {
        QTcpSocket* socket = server_.nextPendingConnection();
        if (socket == nullptr) continue;
        socket->setParent(this);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { handleReadyRead_(socket); });
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
    }
}

void ChartApiServer::handleReadyRead_(QTcpSocket* socket) {
    const QByteArray request = socket->readAll();
    const int headerEnd = request.indexOf("\r\n\r\n");
    if (headerEnd < 0) return;
    const QByteArray header = request.left(headerEnd);
    const QByteArray body = request.mid(headerEnd + 4);
    const int firstLineEnd = header.indexOf('\n');
    const QByteArray firstLine = (firstLineEnd >= 0 ? header.left(firstLineEnd) : header).trimmed();
    const QList<QByteArray> parts = firstLine.split(' ');
    if (parts.size() < 2) {
        respond_(socket, 400, reasonPhrase(400), QByteArrayLiteral("application/json"), QByteArrayLiteral("{\"ok\":false,\"error\":\"bad_request\"}\n"));
        return;
    }

    const QByteArray method = parts[0];
    const QByteArray path = parts[1];
    int statusCode = 200;
    QByteArray statusText = reasonPhrase(200);
    QByteArray responseBody;

    if (method == "GET" && path == "/api/v1/health") {
        responseBody = healthBody_();
    } else if (method == "POST" && path == "/api/v1/chart/markers/vertical") {
        responseBody = markerPost_(body, statusCode, statusText);
    } else if (method == "DELETE" && path == "/api/v1/chart/markers") {
        auto* chart = controller();
        if (chart == nullptr) {
            statusCode = 409;
            statusText = reasonPhrase(statusCode);
            responseBody = QByteArrayLiteral("{\"ok\":false,\"error\":\"no_active_chart\"}\n");
        } else {
            chart->clearVerticalMarkers();
            responseBody = QByteArrayLiteral("{\"ok\":true}\n");
        }
    } else {
        statusCode = 404;
        statusText = reasonPhrase(statusCode);
        responseBody = QByteArrayLiteral("{\"ok\":false,\"error\":\"not_found\"}\n");
    }

    respond_(socket, statusCode, statusText, QByteArrayLiteral("application/json"), responseBody);
}

void ChartApiServer::respond_(QTcpSocket* socket, int statusCode, const QByteArray& statusText,
                              const QByteArray& contentType, const QByteArray& body) {
    QByteArray response;
    response += "HTTP/1.1 " + QByteArray::number(statusCode) + " " + statusText + "\r\n";
    response += "Content-Type: " + contentType + "\r\n";
    response += "Content-Length: " + QByteArray::number(body.size()) + "\r\n";
    response += "Connection: close\r\n\r\n";
    response += body;
    socket->write(response);
    socket->disconnectFromHost();
}

QByteArray ChartApiServer::healthBody_() const {
    auto* chart = controller();
    const bool active = chart != nullptr;
    const bool loaded = active && chart->loaded();
    QByteArray out = QByteArrayLiteral("{\"ok\":true,\"active_chart\":");
    out += jsonBool(active);
    out += QByteArrayLiteral(",\"loaded\":");
    out += jsonBool(loaded);
    out += QByteArrayLiteral("}\n");
    return out;
}

QByteArray ChartApiServer::markerPost_(const QByteArray& body, int& statusCode, QByteArray& statusText) {
    auto* chart = controller();
    if (chart == nullptr || !chart->loaded()) {
        statusCode = 409;
        statusText = reasonPhrase(statusCode);
        return QByteArrayLiteral("{\"ok\":false,\"error\":\"no_active_chart\"}\n");
    }

    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        statusCode = 400;
        statusText = reasonPhrase(statusCode);
        return QByteArrayLiteral("{\"ok\":false,\"error\":\"invalid_json\"}\n");
    }
    const QJsonObject obj = doc.object();
    qint64 tsNs = 0;
    if (!obj.contains(QStringLiteral("ts_ns")) || !parseTimestampNs(obj.value(QStringLiteral("ts_ns")), tsNs)) {
        statusCode = 400;
        statusText = reasonPhrase(statusCode);
        return QByteArrayLiteral("{\"ok\":false,\"error\":\"missing_ts_ns\"}\n");
    }
    const QString label = obj.value(QStringLiteral("label")).toString();
    if (!chart->addVerticalMarker(tsNs, label)) {
        statusCode = 400;
        statusText = reasonPhrase(statusCode);
        return QByteArrayLiteral("{\"ok\":false,\"error\":\"invalid_marker\"}\n");
    }
    return QByteArrayLiteral("{\"ok\":true}\n");
}

}  // namespace hftrec::gui::api
