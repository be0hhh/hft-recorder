#include "gui/backtests/BacktestSessionHelpers.hpp"

#include "core/recordings/RecordingDiscovery.hpp"
#include "core/recordings/RecordingRoot.hpp"
#include "gui/backtests/BacktestResultHelpers.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>

namespace hftrec::gui {

QString resolveRecordingsRoot() {
    return QDir::cleanPath(QString::fromStdString(recordings::defaultRecordingsRoot().string()));
}

QString sessionSourceSummary(const QString& sessionPath, const BacktestLegCounts& backtestCounts) {
    QFile file(QDir(sessionPath).absoluteFilePath(QStringLiteral("manifest.json")));
    if (!file.open(QIODevice::ReadOnly)) return sessionBacktestSummaryText(0, backtestCounts, 0);
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    const QJsonObject manifest = doc.object();
    const QJsonObject bookTicker = manifest.value(QStringLiteral("channels")).toObject().value(QStringLiteral("bookticker")).toObject();
    const QString summary = sessionBacktestSummaryText(
        bookTicker.value(QStringLiteral("declared_event_count")).toInt(),
        backtestCounts,
        manifest.value(QStringLiteral("capture")).toObject().value(QStringLiteral("started_at_ns")).toInteger());
    return appendSessionHealthSummary(
        summary,
        manifest.value(QStringLiteral("integrity")).toObject().value(QStringLiteral("session_health")).toString(),
        manifest.value(QStringLiteral("summary")).toObject().value(QStringLiteral("warning_summary")).toString());
}

QString manifestValue(const QString& sessionPath, const QString& key) {
    QFile file(QDir(sessionPath).absoluteFilePath(QStringLiteral("manifest.json")));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject()) return {};
    return manifestObjectValue(doc.object(), key);
}

QString symbolFromSessionId(const QString& sessionId) {
    const QStringList parts = sessionId.split(QLatin1Char('_'), Qt::SkipEmptyParts);
    return parts.isEmpty() ? QString{} : parts.constLast().trimmed();
}

QString venueSectionFor(const QString& exchange, const QString& market) {
    const QString normalizedExchange = exchange.trimmed().toLower();
    const QString normalizedMarket = market.trimmed().toLower();
    if (normalizedExchange == QStringLiteral("binance") && normalizedMarket == QStringLiteral("spot")) return QStringLiteral("binance_spot");
    if (normalizedExchange == QStringLiteral("binance")) return QStringLiteral("binance_futures");
    if (normalizedExchange == QStringLiteral("bybit") && normalizedMarket == QStringLiteral("spot")) return QStringLiteral("bybit_spot");
    if (normalizedExchange == QStringLiteral("bybit")) return QStringLiteral("bybit_futures");
    if (normalizedExchange == QStringLiteral("kucoin") && normalizedMarket == QStringLiteral("spot")) return QStringLiteral("kucoin_spot");
    if (normalizedExchange == QStringLiteral("kucoin")) return QStringLiteral("kucoin_futures");
    if (normalizedExchange == QStringLiteral("gate") && normalizedMarket == QStringLiteral("spot")) return QStringLiteral("gate_spot");
    if (normalizedExchange == QStringLiteral("gate")) return QStringLiteral("gate_futures");
    if (normalizedExchange == QStringLiteral("bitget") && normalizedMarket == QStringLiteral("spot")) return QStringLiteral("bitget_spot");
    if (normalizedExchange == QStringLiteral("bitget") && normalizedMarket == QStringLiteral("inverse")) return QStringLiteral("bitget_inverse");
    if (normalizedExchange == QStringLiteral("bitget") && normalizedMarket == QStringLiteral("swap")) return QStringLiteral("bitget_swap");
    if (normalizedExchange == QStringLiteral("bitget")) return QStringLiteral("bitget_futures");
    if (normalizedExchange == QStringLiteral("aster") && normalizedMarket == QStringLiteral("spot")) return QStringLiteral("aster_spot");
    if (normalizedExchange == QStringLiteral("aster")) return QStringLiteral("aster_futures");
    if (normalizedExchange == QStringLiteral("okx") && normalizedMarket == QStringLiteral("spot")) return QStringLiteral("okx_spot");
    if (normalizedExchange == QStringLiteral("okx")) return QStringLiteral("okx_futures");
    if (normalizedExchange == QStringLiteral("mexc") && normalizedMarket == QStringLiteral("spot")) return QStringLiteral("mexc_spot");
    if (normalizedExchange == QStringLiteral("mexc")) return QStringLiteral("mexc_futures");
    if (normalizedExchange == QStringLiteral("finam") && normalizedMarket == QStringLiteral("spot")) return QStringLiteral("finam_spot");
    if (normalizedExchange == QStringLiteral("finam")) return QStringLiteral("finam_futures");
    return {};
}

bool isVenueSectionKnown(const QString& exchange, const QString& market) {
    return !venueSectionFor(exchange, market).isEmpty();
}

QString venueSectionForSession(const QString& sessionPath) {
    return venueSectionFor(manifestValue(sessionPath, QStringLiteral("exchange")), manifestValue(sessionPath, QStringLiteral("market")));
}

QString symbolForSessionPath(const QString& sessionPath) {
    const QString fromManifest = manifestValue(sessionPath, QStringLiteral("symbols")).trimmed().toUpper();
    if (!fromManifest.isEmpty()) return fromManifest;
    return symbolFromSessionId(QFileInfo(sessionPath).fileName()).toUpper();
}

QString sessionPathFromToken(const QString& recordingsRoot, const QString& token) {
    const QString trimmed = token.trimmed();
    if (trimmed.isEmpty()) return {};
    const QFileInfo info(trimmed);
    if (info.isAbsolute()) return QDir::cleanPath(info.absoluteFilePath());
    const auto discovery = hftrec::recordings::discoverRecordings(recordingsRoot.toStdString());
    const QString groupPrefix = QStringLiteral("group:");
    if (trimmed.startsWith(groupPrefix)) {
        const QString groupId = trimmed.mid(groupPrefix.size());
        for (const auto& group : discovery.groups) {
            if (QString::fromStdString(group.id) != groupId || group.sessions.empty()) continue;
            return QString::fromStdString(group.sessions.front().path.string());
        }
    }
    for (const auto& session : discovery.sessions) {
        if (QString::fromStdString(session.sessionId) == trimmed) return QString::fromStdString(session.path.string());
    }
    return QDir(recordingsRoot).absoluteFilePath(trimmed);
}

QVariantMap sessionRowById(const QVariantList& rows, const QString& id) {
    for (const QVariant& value : rows) {
        const QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("id")).toString() == id) return row;
    }
    return {};
}

bool sessionRowSelectable(const QVariantMap& row) {
    return row.value(QStringLiteral("selectable"), true).toBool();
}

QString firstSelectableSessionId(const QVariantList& rows) {
    for (const QVariant& value : rows) {
        const QVariantMap row = value.toMap();
        if (sessionRowSelectable(row)) return row.value(QStringLiteral("id")).toString();
    }
    return {};
}

QStringList sessionPathsFromRow(const QVariantMap& row) {
    QStringList paths;
    const QVariantList rowPaths = row.value(QStringLiteral("sessionPaths")).toList();
    for (const QVariant& value : rowPaths) {
        const QString path = value.toString().trimmed();
        if (!path.isEmpty() && !paths.contains(path)) paths.push_back(path);
    }
    const QString path = row.value(QStringLiteral("path")).toString().trimmed();
    if (paths.empty() && !path.isEmpty()) paths.push_back(path);
    return paths;
}

}  // namespace hftrec::gui
