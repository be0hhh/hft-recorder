#include "gui/api/LocalVenueWsServer.hpp"
#include "gui/api/LocalVenueOrderParser.hpp"

#include <QByteArray>
#include <QCryptographicHash>
#include <QDateTime>
#include <QHash>
#include <QHostAddress>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMessageAuthenticationCode>
#include <QMetaObject>
#include <QAbstractSocket>
#include <QDir>
#include <QFile>
#include <QTcpSocket>

#include <algorithm>
#include <cstring>
#include <limits>
#include <utility>
#include <string_view>

#include "canon/PositionAndExchange.hpp"
#include "composite/level_0/SendWsObject.hpp"
#include "core/local_exchange/LocalOrderEngine.hpp"
#include "network/local/hftrecorder/Protocol.hpp"
#include "parse/decimal_to_scaled.hpp"
#include "primitives/buf/CanonConstants.hpp"
#include "primitives/raw/bool/Side.hpp"
#include "primitives/composite/UserDataEvent.hpp"

namespace hftrec::gui::api {
namespace {

constexpr const char* kProtocol = "hftrecorder.local.v1";
constexpr const char* kWsGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

struct ParsedFrame {
    std::uint8_t opcode{0};
    QByteArray payload{};
};

std::uint64_t nowNs() noexcept;
QJsonValue requestIdValue(const QJsonObject& request);
bool parseUInt64Value(const QJsonValue& value, std::uint64_t& out) noexcept;

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

QString auditDirPath() {
    return qEnvironmentVariable("HFTREC_LOCAL_AUDIT_DIR", "recordings/local_exchange").trimmed();
}

qint64 authWindowNs() noexcept {
    const auto raw = qEnvironmentVariable("HFTREC_LOCAL_AUTH_WINDOW_NS", "5000000000").trimmed();
    bool ok = false;
    const qint64 parsed = raw.toLongLong(&ok, 10);
    if (!ok || parsed <= 0) return 5000000000ll;
    return parsed;
}

QString envValue(const char* name) {
    return qEnvironmentVariable(name).trimmed();
}

bool resolveSecretForApiKey(const QString& apiKey, QString& secret) {
    const QString key = apiKey.trimmed();
    for (int slot = 1; slot <= 8; ++slot) {
        const QString suffix = QString::number(slot);
        const QByteArray futuresKeyName = (QStringLiteral("HFTRECORDER_LOCAL_FUTURES_API_") + suffix + QStringLiteral("_KEY")).toLatin1();
        const QByteArray futuresSecretName = (QStringLiteral("HFTRECORDER_LOCAL_FUTURES_API_") + suffix + QStringLiteral("_SECRET")).toLatin1();
        const QString futuresKey = envValue(futuresKeyName.constData());
        if (!futuresKey.isEmpty() && futuresKey == key) {
            secret = envValue(futuresSecretName.constData());
            return !secret.isEmpty();
        }
        const QByteArray binanceKeyName = (QStringLiteral("BINANCE_FUTURES_API_") + suffix + QStringLiteral("_KEY")).toLatin1();
        const QByteArray binanceSecretName = (QStringLiteral("BINANCE_FUTURES_API_") + suffix + QStringLiteral("_SECRET")).toLatin1();
        const QString binanceKey = envValue(binanceKeyName.constData());
        if (!binanceKey.isEmpty() && binanceKey == key) {
            secret = envValue(binanceSecretName.constData());
            return !secret.isEmpty();
        }
        const QByteArray asterKeyName = (QStringLiteral("ASTER_FUTURES_API_") + suffix + QStringLiteral("_KEY")).toLatin1();
        const QByteArray asterSecretName = (QStringLiteral("ASTER_FUTURES_API_") + suffix + QStringLiteral("_SECRET")).toLatin1();
        const QString asterKey = envValue(asterKeyName.constData());
        if (!asterKey.isEmpty() && asterKey == key) {
            secret = envValue(asterSecretName.constData());
            return !secret.isEmpty();
        }
        const QByteArray genericKeyName = (QStringLiteral("HFTRECORDER_LOCAL_API_") + suffix + QStringLiteral("_KEY")).toLatin1();
        const QByteArray genericSecretName = (QStringLiteral("HFTRECORDER_LOCAL_API_") + suffix + QStringLiteral("_SECRET")).toLatin1();
        const QString genericKey = envValue(genericKeyName.constData());
        if (!genericKey.isEmpty() && genericKey == key) {
            secret = envValue(genericSecretName.constData());
            return !secret.isEmpty();
        }
    }
    const QString legacyKey = envValue("HFTRECORDER_LOCAL_API_KEY");
    if (!legacyKey.isEmpty() && legacyKey == key) {
        secret = envValue("HFTRECORDER_LOCAL_SECRET_KEY");
        if (secret.isEmpty()) secret = envValue("HFTRECORDER_LOCAL_API_SECRET");
        if (secret.isEmpty()) secret = envValue("HFTRECORDER_LOCAL_SECRET");
        return !secret.isEmpty();
    }
    return false;
}

QByteArray authReply(const QJsonValue& requestId, bool ok, const QString& error) {
    QJsonObject out;
    out.insert(QStringLiteral("protocol"), QString::fromLatin1(kProtocol));
    out.insert(QStringLiteral("op"), ok ? QStringLiteral("auth.ok") : QStringLiteral("auth.error"));
    out.insert(QStringLiteral("request_id"), requestId);
    out.insert(QStringLiteral("ok"), ok);
    out.insert(QStringLiteral("server_time_ns"), QString::number(nowNs()));
    if (!ok) out.insert(QStringLiteral("error"), error);
    return QJsonDocument(out).toJson(QJsonDocument::Compact);
}

QByteArray errorJson(const QJsonObject& request, const QString& error) {
    QJsonObject out;
    out.insert(QStringLiteral("protocol"), QString::fromLatin1(kProtocol));
    out.insert(QStringLiteral("op"), QStringLiteral("error"));
    out.insert(QStringLiteral("request_id"), requestIdValue(request));
    out.insert(QStringLiteral("ok"), false);
    out.insert(QStringLiteral("error"), error);
    return QJsonDocument(out).toJson(QJsonDocument::Compact);
}

bool verifyAuthFrame(const QJsonObject& root,
                     QHash<QString, QSet<QString>>& nonceCache,
                     QString& apiKeyOut,
                     QString& error) {
    if (root.value(QStringLiteral("protocol")).toString() != QString::fromLatin1(kProtocol)) {
        error = QStringLiteral("bad_protocol");
        return false;
    }
    if (root.value(QStringLiteral("op")).toString() != QStringLiteral("auth.login")) {
        error = QStringLiteral("bad_op");
        return false;
    }
    const QString apiKey = root.value(QStringLiteral("api_key")).toString().trimmed();
    const QString nonce = root.value(QStringLiteral("nonce")).toString().trimmed();
    const QString signature = root.value(QStringLiteral("signature")).toString().trimmed().toLower();
    const QString targetOp = root.value(QStringLiteral("target_op")).toString(QStringLiteral("session")).trimmed();
    std::uint64_t timestampNs = 0;
    if (apiKey.isEmpty() || nonce.isEmpty() || signature.isEmpty() || !parseUInt64Value(root.value(QStringLiteral("timestamp_ns")), timestampNs)) {
        error = QStringLiteral("missing_auth_fields");
        return false;
    }
    const std::uint64_t serverNowNs = nowNs();
    const qint64 delta = static_cast<qint64>(serverNowNs > timestampNs ? serverNowNs - timestampNs : timestampNs - serverNowNs);
    if (delta > authWindowNs()) {
        error = QStringLiteral("timestamp_out_of_window");
        return false;
    }
    QString secret;
    if (!resolveSecretForApiKey(apiKey, secret)) {
        error = QStringLiteral("unknown_api_key");
        return false;
    }
    QSet<QString>& seen = nonceCache[apiKey];
    if (seen.contains(nonce)) {
        error = QStringLiteral("duplicate_nonce");
        return false;
    }
    const QByteArray payload = apiKey.toLatin1() + QByteArrayLiteral("|") + QByteArray::number(timestampNs) +
                               QByteArrayLiteral("|") + nonce.toLatin1() + QByteArrayLiteral("|") + targetOp.toLatin1();
    const QByteArray expected = QMessageAuthenticationCode::hash(payload, secret.toLatin1(), QCryptographicHash::Sha256).toHex();
    if (expected != signature.toLatin1()) {
        error = QStringLiteral("bad_signature");
        return false;
    }
    seen.insert(nonce);
    apiKeyOut = apiKey;
    return true;
}

QString auditFileName(hftrec::execution::ExecutionEventKind kind) {
    using Kind = hftrec::execution::ExecutionEventKind;
    if (kind == Kind::Fill || kind == Kind::Fee) return QStringLiteral("fills.jsonl");
    if (kind == Kind::PositionChange) return QStringLiteral("positions.jsonl");
    if (kind == Kind::BalanceChange || kind == Kind::Funding) return QStringLiteral("balances.jsonl");
    return QStringLiteral("orders.jsonl");
}

std::uint64_t nowNs() noexcept {
    return static_cast<std::uint64_t>(QDateTime::currentMSecsSinceEpoch()) * 1000000ull;
}

bool parseUInt64String(const QString& text, std::uint64_t& out) noexcept {
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) return false;
    std::uint64_t value = 0;
    for (QChar ch : trimmed) {
        if (!ch.isDigit()) return false;
        const std::uint64_t digit = static_cast<std::uint64_t>(ch.unicode() - u'0');
        value = value * 10u + digit;
    }
    out = value;
    return true;
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
    if (value.isString()) return parseUInt64String(value.toString(), out);
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

void copySmallText(const QString& text, char* out, std::size_t outSize) noexcept {
    if (out == nullptr || outSize == 0u) return;
    out[0] = '\0';
    const QByteArray bytes = text.trimmed().toLatin1();
    const qsizetype n = std::min<qsizetype>(bytes.size(), static_cast<qsizetype>(outSize - 1u));
    if (n > 0) std::memcpy(out, bytes.constData(), static_cast<std::size_t>(n));
    out[n] = '\0';
}

QJsonValue requestIdValue(const QJsonObject& request) {
    return request.contains(QStringLiteral("request_id")) ? request.value(QStringLiteral("request_id")) : request.value(QStringLiteral("id"));
}

QString jsonTextValue(const QJsonObject& object, const char* key) {
    const QJsonValue value = object.value(QString::fromLatin1(key));
    if (value.isString()) return value.toString().trimmed();
    if (value.isDouble()) return QString::number(value.toInteger());
    return {};
}

bool parseScaledText(const QString& text, std::size_t scaleDigits, std::int64_t& out) noexcept {
    const QByteArray bytes = text.trimmed().toLatin1();
    if (bytes.isEmpty()) return false;
    out = cxet::decimalStringToScaled(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())), scaleDigits);
    return out != 0;
}

bool parseBinanceTimeInForce(const QString& text, canon::TimeInForce& out) noexcept {
    const QString upper = text.trimmed().toUpper();
    if (upper == QStringLiteral("GTC")) { out = canon::TimeInForce::GTC; return true; }
    if (upper == QStringLiteral("IOC")) { out = canon::TimeInForce::IOC; return true; }
    if (upper == QStringLiteral("FOK")) { out = canon::TimeInForce::FOK; return true; }
    if (upper == QStringLiteral("GTX")) { out = canon::TimeInForce::GTX; return true; }
    if (upper == QStringLiteral("GTD")) { out = canon::TimeInForce::GTD; return true; }
    out = canon::TimeInForce::Unknown;
    return false;
}

bool parseBinanceReduceOnly(const QJsonObject& params, std::uint8_t& setOut, std::uint8_t& valueOut) noexcept {
    if (!params.contains(QStringLiteral("reduceOnly"))) {
        setOut = 0u;
        valueOut = 0u;
        return true;
    }
    const QJsonValue value = params.value(QStringLiteral("reduceOnly"));
    setOut = 1u;
    if (value.isBool()) {
        valueOut = value.toBool() ? 1u : 0u;
        return true;
    }
    const QString text = value.toString().trimmed().toLower();
    if (text == QStringLiteral("true") || text == QStringLiteral("1")) { valueOut = 1u; return true; }
    if (text == QStringLiteral("false") || text == QStringLiteral("0")) { valueOut = 0u; return true; }
    return false;
}

bool parseOptionalBoolText(const QJsonObject& params, const char* key, bool& presentOut, bool& valueOut) noexcept {
    const QString qKey = QString::fromLatin1(key);
    if (!params.contains(qKey)) {
        presentOut = false;
        valueOut = false;
        return true;
    }
    presentOut = true;
    const QJsonValue value = params.value(qKey);
    if (value.isBool()) {
        valueOut = value.toBool();
        return true;
    }
    const QString text = value.toString().trimmed().toLower();
    if (text == QStringLiteral("true") || text == QStringLiteral("1")) { valueOut = true; return true; }
    if (text == QStringLiteral("false") || text == QStringLiteral("0")) { valueOut = false; return true; }
    return false;
}

bool rejectUnsupportedBinanceFlags(const QJsonObject& params, QString& error) noexcept {
    const QString positionSide = jsonTextValue(params, "positionSide").toUpper();
    if (!positionSide.isEmpty() && positionSide != QStringLiteral("BOTH")) {
        error = QStringLiteral("unsupported_position_side");
        return false;
    }

    bool present = false;
    bool boolValue = false;
    if (!parseOptionalBoolText(params, "closePosition", present, boolValue)) {
        error = QStringLiteral("bad_close_position");
        return false;
    }
    if (present && boolValue) {
        error = QStringLiteral("unsupported_close_position");
        return false;
    }

    if (!parseOptionalBoolText(params, "priceProtect", present, boolValue)) {
        error = QStringLiteral("bad_price_protect");
        return false;
    }
    if (present && boolValue) {
        error = QStringLiteral("unsupported_price_protect");
        return false;
    }

    const QString responseType = jsonTextValue(params, "newOrderRespType").toUpper();
    if (!responseType.isEmpty() && responseType != QStringLiteral("ACK")) {
        error = QStringLiteral("unsupported_order_resp_type");
        return false;
    }
    const QString priceMatch = jsonTextValue(params, "priceMatch").toUpper();
    if (!priceMatch.isEmpty() && priceMatch != QStringLiteral("NONE")) {
        error = QStringLiteral("unsupported_price_match");
        return false;
    }
    const QString stp = jsonTextValue(params, "selfTradePreventionMode").toUpper();
    if (!stp.isEmpty() && stp != QStringLiteral("NONE")) {
        error = QStringLiteral("unsupported_self_trade_prevention");
        return false;
    }
    if (params.contains(QStringLiteral("goodTillDate"))) {
        error = QStringLiteral("unsupported_good_till_date");
        return false;
    }
    if (params.contains(QStringLiteral("activationPrice")) || params.contains(QStringLiteral("callbackRate")) ||
        params.contains(QStringLiteral("workingType"))) {
        error = QStringLiteral("unsupported_trigger_option");
        return false;
    }
    return true;
}

bool buildBinanceOrderPlaceRequest(const QJsonObject& root,
                                   cxet::network::local::hftrecorder::OrderRequestFrame& frame,
                                   QString& error) noexcept {
    if (root.value(QStringLiteral("method")).toString() != QStringLiteral("order.place")) {
        error = QStringLiteral("bad_method");
        return false;
    }
    const QJsonObject params = root.value(QStringLiteral("params")).toObject();
    if (params.isEmpty()) {
        error = QStringLiteral("missing_params");
        return false;
    }
    if (!rejectUnsupportedBinanceFlags(params, error)) return false;

    frame = cxet::network::local::hftrecorder::OrderRequestFrame{};
    frame.operationRaw = static_cast<std::uint8_t>(cxet::UnifiedRequestSpec::Operation::SendWs);
    frame.objectRaw = static_cast<std::uint8_t>(cxet::composite::out::WsSendObject::Order);
    frame.exchangeRaw = canon::kExchangeIdBinance.raw;
    frame.marketRaw = canon::kMarketTypeFutures.raw;
    frame.apiSlotRaw = 1u;
    frame.sendTsNs = nowNs();
    parseUInt64Value(root.value(QStringLiteral("id")), frame.clientSeq);

    const QString side = jsonTextValue(params, "side").toUpper();
    if (side == QStringLiteral("BUY")) {
        frame.sideSet = 1u;
        frame.sideRaw = Side::Buy().raw;
    } else if (side == QStringLiteral("SELL")) {
        frame.sideSet = 1u;
        frame.sideRaw = Side::Sell().raw;
    } else {
        error = QStringLiteral("bad_side");
        return false;
    }

    const QString type = jsonTextValue(params, "type").toUpper();
    if (type == QStringLiteral("MARKET")) frame.typeRaw = static_cast<std::uint8_t>(canon::OrderType::Market);
    else if (type == QStringLiteral("LIMIT")) frame.typeRaw = static_cast<std::uint8_t>(canon::OrderType::Limit);
    else if (type == QStringLiteral("STOP_MARKET")) frame.typeRaw = static_cast<std::uint8_t>(canon::OrderType::Stop);
    else {
        error = QStringLiteral("unsupported_order_type");
        return false;
    }

    if (!parseScaledText(jsonTextValue(params, "quantity"), numeric::kAmountScaleDigits, frame.quantityRaw)) {
        error = QStringLiteral("missing_quantity");
        return false;
    }
    if (frame.typeRaw == static_cast<std::uint8_t>(canon::OrderType::Limit)) {
        if (!parseScaledText(jsonTextValue(params, "price"), numeric::kPriceScaleDigits, frame.priceRaw)) {
            error = QStringLiteral("missing_price");
            return false;
        }
        canon::TimeInForce tif = canon::TimeInForce::Unknown;
        if (!parseBinanceTimeInForce(jsonTextValue(params, "timeInForce"), tif)) tif = canon::TimeInForce::GTC;
        if (tif != canon::TimeInForce::GTC) {
            error = QStringLiteral("unsupported_time_in_force");
            return false;
        }
        frame.timeInForceRaw = static_cast<std::uint8_t>(tif);
        frame.subtypeRaw = static_cast<std::uint8_t>(tif);
    } else if (frame.typeRaw == static_cast<std::uint8_t>(canon::OrderType::Stop)) {
        if (!parseScaledText(jsonTextValue(params, "stopPrice"), numeric::kPriceScaleDigits, frame.priceRaw)) {
            error = QStringLiteral("missing_stop_price");
            return false;
        }
    }
    if (!parseBinanceReduceOnly(params, frame.reduceOnlySet, frame.reduceOnly)) {
        error = QStringLiteral("bad_reduce_only");
        return false;
    }
    copySmallText(jsonTextValue(params, "newClientOrderId"), frame.clientOrderId, sizeof(frame.clientOrderId));
    if (!copySymbol(jsonTextValue(params, "symbol"), frame.symbol, sizeof(frame.symbol))) {
        error = QStringLiteral("missing_symbol");
        return false;
    }
    return true;
}
bool buildBinanceOrderCancelRequest(const QJsonObject& root,
                                    cxet::network::local::hftrecorder::OrderRequestFrame& frame,
                                    QString& error) noexcept {
    if (root.value(QStringLiteral("method")).toString() != QStringLiteral("order.cancel")) {
        error = QStringLiteral("bad_method");
        return false;
    }
    const QJsonObject params = root.value(QStringLiteral("params")).toObject();
    if (params.isEmpty()) {
        error = QStringLiteral("missing_params");
        return false;
    }
    frame = cxet::network::local::hftrecorder::OrderRequestFrame{};
    frame.operationRaw = static_cast<std::uint8_t>(cxet::UnifiedRequestSpec::Operation::SendWs);
    frame.objectRaw = static_cast<std::uint8_t>(cxet::composite::out::WsSendObject::Order);
    frame.exchangeRaw = canon::kExchangeIdBinance.raw;
    frame.marketRaw = canon::kMarketTypeFutures.raw;
    frame.apiSlotRaw = 1u;
    frame.typeRaw = static_cast<std::uint8_t>(canon::OrderType::Cancel);
    frame.sendTsNs = nowNs();
    parseUInt64Value(root.value(QStringLiteral("id")), frame.clientSeq);
    copySmallText(jsonTextValue(params, "orderId"), frame.orderId, sizeof(frame.orderId));
    copySmallText(jsonTextValue(params, "origClientOrderId"), frame.origClientOrderId, sizeof(frame.origClientOrderId));
    copySmallText(jsonTextValue(params, "newClientOrderId"), frame.clientOrderId, sizeof(frame.clientOrderId));
    if (!copySymbol(jsonTextValue(params, "symbol"), frame.symbol, sizeof(frame.symbol))) {
        error = QStringLiteral("missing_symbol");
        return false;
    }
    if (frame.orderId[0] == '\0' && frame.origClientOrderId[0] == '\0') {
        error = QStringLiteral("missing_order_ref");
        return false;
    }
    return true;
}
bool buildOrderRequestImpl(const QJsonObject& root,
                           cxet::network::local::hftrecorder::OrderRequestFrame& frame,
                           QString& error) noexcept {
    if (root.value(QStringLiteral("method")).toString() == QStringLiteral("order.place")) {
        return buildBinanceOrderPlaceRequest(root, frame, error);
    }
    if (root.value(QStringLiteral("method")).toString() == QStringLiteral("order.cancel")) {
        return buildBinanceOrderCancelRequest(root, frame, error);
    }
    if (root.value(QStringLiteral("protocol")).toString() != QString::fromLatin1(kProtocol)) {
        error = QStringLiteral("bad_protocol");
        return false;
    }
    const QString op = root.value(QStringLiteral("op")).toString();
    if (op != QStringLiteral("order.submit") && op != QStringLiteral("order.cancel")) {
        error = QStringLiteral("bad_op");
        return false;
    }
    const QJsonObject order = root.value(op == QStringLiteral("order.cancel") ? QStringLiteral("cancel") : QStringLiteral("order")).toObject();
    if (order.isEmpty()) {
        error = QStringLiteral("missing_order");
        return false;
    }

    frame = cxet::network::local::hftrecorder::OrderRequestFrame{};
    frame.operationRaw = static_cast<std::uint8_t>(cxet::UnifiedRequestSpec::Operation::SendWs);
    frame.objectRaw = static_cast<std::uint8_t>(cxet::composite::out::WsSendObject::Order);
    frame.exchangeRaw = static_cast<std::uint8_t>(order.value(QStringLiteral("exchange_raw")).toInt());
    frame.marketRaw = static_cast<std::uint8_t>(order.value(QStringLiteral("market_raw")).toInt());
    frame.typeRaw = op == QStringLiteral("order.cancel") ? static_cast<std::uint8_t>(canon::OrderType::Cancel) : static_cast<std::uint8_t>(order.value(QStringLiteral("type_raw")).toInt(cxet::UnifiedRequestSpec::kUnset));
    frame.subtypeRaw = static_cast<std::uint8_t>(order.value(QStringLiteral("subtype_raw")).toInt(cxet::UnifiedRequestSpec::kUnset));
    frame.apiSlotRaw = static_cast<std::uint8_t>(order.value(QStringLiteral("api_slot_raw")).toInt(1));
    if (frame.apiSlotRaw == 0u) frame.apiSlotRaw = 1u;
    frame.sideSet = static_cast<std::uint8_t>(order.value(QStringLiteral("side_set")).toInt());
    frame.sideRaw = static_cast<std::uint8_t>(order.value(QStringLiteral("side_raw")).toInt());
    frame.timeInForceRaw = static_cast<std::uint8_t>(order.value(QStringLiteral("time_in_force_raw")).toInt(static_cast<int>(canon::TimeInForce::Unknown)));
    frame.reduceOnlySet = static_cast<std::uint8_t>(order.value(QStringLiteral("reduce_only_set")).toInt());
    frame.reduceOnly = static_cast<std::uint8_t>(order.value(QStringLiteral("reduce_only")).toInt());
    frame.positionModeRaw = static_cast<std::uint8_t>(order.value(QStringLiteral("position_mode_raw")).toInt(static_cast<int>(canon::PositionMode::Unknown)));
    if (!parseInt64Value(order.value(QStringLiteral("price_raw")), frame.priceRaw)) frame.priceRaw = 0;
    if (!parseInt64Value(order.value(QStringLiteral("quantity_raw")), frame.quantityRaw)) frame.quantityRaw = 0;
    parseUInt64Value(root.value(QStringLiteral("request_id")), frame.clientSeq);
    parseUInt64Value(root.value(QStringLiteral("client_send_ts_ns")), frame.sendTsNs);
    copySmallText(order.value(QStringLiteral("client_order_id")).toString(), frame.clientOrderId, sizeof(frame.clientOrderId));
    copySmallText(order.value(QStringLiteral("order_id")).toString(), frame.orderId, sizeof(frame.orderId));
    copySmallText(order.value(QStringLiteral("orig_client_order_id")).toString(), frame.origClientOrderId, sizeof(frame.origClientOrderId));
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
    out.insert(QStringLiteral("request_id"), requestIdValue(request));
    out.insert(QStringLiteral("ok"), ok);
    out.insert(QStringLiteral("recorder_ack_ts_ns"), QString::number(nowNs()));
    if (!ok) out.insert(QStringLiteral("error"), error);
    if (ack != nullptr) {
        out.insert(QStringLiteral("exchange_raw"), static_cast<int>(ack->exchangeRaw));
        out.insert(QStringLiteral("market_raw"), static_cast<int>(ack->marketRaw));
        out.insert(QStringLiteral("status_raw"), static_cast<int>(ack->statusRaw));
        out.insert(QStringLiteral("error_code"), static_cast<int>(ack->errorCode));
        out.insert(QStringLiteral("ts_ns"), QString::number(ack->tsNs));
        out.insert(QStringLiteral("symbol"), QString::fromLatin1(ack->symbol));
        out.insert(QStringLiteral("order_id"), QString::fromLatin1(ack->orderId));
        out.insert(QStringLiteral("client_order_id"), QString::fromLatin1(ack->clientOrderId));
    }
    return QJsonDocument(out).toJson(QJsonDocument::Compact);
}

std::uint8_t userDataKindRaw(hftrec::execution::ExecutionEventKind kind) noexcept {
    using Kind = hftrec::execution::ExecutionEventKind;
    if (kind == Kind::Fill || kind == Kind::Fee || kind == Kind::Funding) return static_cast<std::uint8_t>(cxet::composite::UserDataEventKind::Trade);
    if (kind == Kind::PositionChange) return static_cast<std::uint8_t>(cxet::composite::UserDataEventKind::Position);
    if (kind == Kind::BalanceChange) return static_cast<std::uint8_t>(cxet::composite::UserDataEventKind::Balance);
    return static_cast<std::uint8_t>(cxet::composite::UserDataEventKind::Order);
}

QByteArray userEventJson(const hftrec::execution::ExecutionEvent& event) {
    QJsonObject out;
    out.insert(QStringLiteral("protocol"), QString::fromLatin1(kProtocol));
    out.insert(QStringLiteral("op"), QStringLiteral("user.event"));
    out.insert(QStringLiteral("kind_raw"), static_cast<int>(userDataKindRaw(event.kind)));
    out.insert(QStringLiteral("exchange_raw"), static_cast<int>(event.exchangeRaw));
    out.insert(QStringLiteral("market_raw"), static_cast<int>(event.marketRaw));
    out.insert(QStringLiteral("side_raw"), static_cast<int>(event.sideRaw));
    out.insert(QStringLiteral("type_raw"), static_cast<int>(event.typeRaw));
    out.insert(QStringLiteral("time_in_force_raw"), static_cast<int>(event.timeInForceRaw));
    out.insert(QStringLiteral("position_mode_raw"), static_cast<int>(canon::PositionMode::Unknown));
    out.insert(QStringLiteral("status_raw"), static_cast<int>(event.statusRaw));
    out.insert(QStringLiteral("error_code"), static_cast<int>(event.errorCode));
    out.insert(QStringLiteral("symbol"), QString::fromStdString(event.symbol));
    out.insert(QStringLiteral("currency"), QStringLiteral("USDT"));
    out.insert(QStringLiteral("order_id"), QString::fromStdString(event.orderId));
    out.insert(QStringLiteral("client_order_id"), QString::fromStdString(event.clientOrderId));
    out.insert(QStringLiteral("exec_id"), QString::fromStdString(event.execId));
    out.insert(QStringLiteral("price_raw"), QString::number(event.priceRaw));
    out.insert(QStringLiteral("avg_price_raw"), QString::number(event.avgEntryPriceE8));
    out.insert(QStringLiteral("entry_price_raw"), QString::number(event.avgEntryPriceE8));
    out.insert(QStringLiteral("qty_raw"), QString::number(event.quantityRaw));
    out.insert(QStringLiteral("filled_qty_raw"), QString::number(event.filledQtyRaw));
    out.insert(QStringLiteral("fee_raw"), QString::number(event.feeRaw));
    out.insert(QStringLiteral("realized_pnl_raw"), QString::number(event.realizedPnlRaw));
    out.insert(QStringLiteral("wallet_balance_raw"), QString::number(event.walletBalanceRaw));
    out.insert(QStringLiteral("available_balance_raw"), QString::number(event.availableBalanceRaw));
    out.insert(QStringLiteral("equity_raw"), QString::number(event.equityRaw));
    out.insert(QStringLiteral("event_time_ns"), QString::number(event.tsNs));
    out.insert(QStringLiteral("update_time_ns"), QString::number(event.tsNs));
    out.insert(QStringLiteral("success"), event.success);
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

namespace detail {

bool buildOrderRequest(const QJsonObject& root,
                       cxet::network::local::hftrecorder::OrderRequestFrame& frame,
                       QString& error) noexcept {
    return buildOrderRequestImpl(root, frame, error);
}

}  // namespace detail

struct LocalVenueWsServer::ClientState {
    QByteArray buffer{};
    bool handshaken{false};
};

LocalVenueWsServer::LocalVenueWsServer(QObject* parent) : QObject(parent) {
    connect(&server_, &QTcpServer::newConnection, this, &LocalVenueWsServer::acceptConnection_);
    hftrec::local_exchange::globalLocalOrderEngine().setEventSink(this);
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
    hftrec::local_exchange::globalLocalOrderEngine().setEventSink(nullptr);
    server_.close();
    authenticatedClients_.clear();
    clientApiKeys_.clear();
    nonceCache_.clear();
    userStreamClients_.clear();
}

void LocalVenueWsServer::onExecutionEvent(const hftrec::execution::ExecutionEvent& event) noexcept {
    if (downstreamSink_ != nullptr) downstreamSink_->onExecutionEvent(event);
    QMetaObject::invokeMethod(this, [this, event]() { broadcastUserEvent_(event); }, Qt::QueuedConnection);
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
        connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
            userStreamClients_.remove(socket);
            authenticatedClients_.remove(socket);
            clientApiKeys_.remove(socket);
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
    if (root.value(QStringLiteral("protocol")).toString() == QString::fromLatin1(kProtocol) &&
        root.value(QStringLiteral("op")).toString() == QStringLiteral("auth.login")) {
        QString apiKey;
        QString error;
        const bool ok = verifyAuthFrame(root, nonceCache_, apiKey, error);
        if (ok) {
            authenticatedClients_.insert(socket, true);
            clientApiKeys_.insert(socket, apiKey);
        }
        sendText_(socket, authReply(root.value(QStringLiteral("request_id")), ok, error));
        return;
    }
    if (!authenticatedClients_.value(socket, false)) {
        sendText_(socket, errorJson(root, QStringLiteral("auth_required")));
        return;
    }
    if (root.value(QStringLiteral("protocol")).toString() == QString::fromLatin1(kProtocol) &&
        root.value(QStringLiteral("op")).toString() == QStringLiteral("user.subscribe")) {
        userStreamClients_.insert(socket, true);
        QJsonObject ok;
        ok.insert(QStringLiteral("protocol"), QString::fromLatin1(kProtocol));
        ok.insert(QStringLiteral("op"), QStringLiteral("user.subscribed"));
        ok.insert(QStringLiteral("request_id"), root.value(QStringLiteral("request_id")));
        ok.insert(QStringLiteral("ok"), true);
        sendText_(socket, QJsonDocument(ok).toJson(QJsonDocument::Compact));
        return;
    }
    cxet::network::local::hftrecorder::OrderRequestFrame request{};
    QString error;
    if (!detail::buildOrderRequest(root, request, error)) {
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

void LocalVenueWsServer::broadcastUserEvent_(const hftrec::execution::ExecutionEvent& event) {
    const QByteArray payload = userEventJson(event);
    appendAuditEvent_(event, payload);
    QList<QTcpSocket*> stale;
    for (auto it = userStreamClients_.begin(); it != userStreamClients_.end(); ++it) {
        QTcpSocket* socket = it.key();
        if (socket == nullptr || socket->state() != QAbstractSocket::ConnectedState) {
            stale.push_back(socket);
            continue;
        }
        sendText_(socket, payload);
    }
    for (QTcpSocket* socket : stale) userStreamClients_.remove(socket);
}

void LocalVenueWsServer::appendAuditEvent_(const hftrec::execution::ExecutionEvent& event, const QByteArray& payload) {
    const QString dirPath = auditDirPath();
    if (dirPath.isEmpty()) return;
    QDir dir;
    if (!dir.mkpath(dirPath)) return;
    QFile file(dirPath + QLatin1Char('/') + auditFileName(event.kind));
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) return;
    file.write(payload);
    file.write("\n", 1);
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
