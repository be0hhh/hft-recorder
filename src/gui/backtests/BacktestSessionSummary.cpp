#include "gui/backtests/BacktestSessionSummary.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

#include "core/recordings/RecordingDiscovery.hpp"

namespace hftrec::gui {
namespace {

QString normalizedSessionId(QString value) {
    value = value.trimmed();
    if (value.isEmpty()) return {};
    return QFileInfo(value).fileName();
}

QString resultLegSessionId(const QJsonObject& manifest, int legIndex) {
    const QJsonArray legs = manifest.value(QStringLiteral("legs")).toArray();
    for (int i = 0; i < legs.size(); ++i) {
        const QJsonObject leg = legs.at(i).toObject();
        const int index = leg.value(QStringLiteral("leg_index")).toInt(i);
        if (index == legIndex) return normalizedSessionId(leg.value(QStringLiteral("session_path")).toString());
    }
    if (legIndex == 0) return normalizedSessionId(manifest.value(QStringLiteral("session_path")).toString());
    return {};
}

QString resultArtifactPath(const QString& manifestPath, const QJsonObject& manifest, const QString& key, const QString& fallback) {
    QString path = manifest.value(key).toObject().value(QStringLiteral("path")).toString(fallback);
    if (path.trimmed().isEmpty()) return {};
    const QFileInfo info(path);
    if (info.isRelative()) path = QFileInfo(manifestPath).dir().absoluteFilePath(path);
    return path;
}

QJsonObject readFirstJsonlObject(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    while (!file.atEnd()) {
        const QByteArray line = file.readLine().trimmed();
        if (line.isEmpty()) continue;
        const QJsonDocument doc = QJsonDocument::fromJson(line);
        return doc.isObject() ? doc.object() : QJsonObject{};
    }
    return {};
}

QString sweepRowLegSessionId(const QString& manifestPath, const QJsonObject& manifest, int legIndex) {
    const QStringList keys = {QStringLiteral("rows"), QStringLiteral("curves")};
    const QStringList fallbacks = {QStringLiteral("sweep_results.jsonl"), QStringLiteral("sweep_curves.jsonl")};
    for (int i = 0; i < keys.size(); ++i) {
        const QJsonObject row = readFirstJsonlObject(resultArtifactPath(manifestPath, manifest, keys.at(i), fallbacks.at(i)));
        const QString id = resultLegSessionId(row, legIndex);
        if (!id.isEmpty()) return id;
    }
    return {};
}

QString resultLegSessionId(const QString& manifestPath, const QJsonObject& manifest, int legIndex) {
    QString id = resultLegSessionId(manifest, legIndex);
    if (!id.isEmpty() || manifest.value(QStringLiteral("type")).toString() != QStringLiteral("sweep.result.v1")) return id;
    return sweepRowLegSessionId(manifestPath, manifest, legIndex);
}

QJsonObject readManifestObject(const QString& manifestPath) {
    QFile file(manifestPath);
    if (!file.open(QIODevice::ReadOnly)) return {};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    return doc.isObject() ? doc.object() : QJsonObject{};
}

bool isBacktestResultManifest(const QJsonObject& manifest) {
    const QString type = manifest.value(QStringLiteral("type")).toString();
    return type == QStringLiteral("run.result.v2") || type == QStringLiteral("sweep.result.v1");
}

void addLegCount(QHash<QString, BacktestLegCounts>& counts, const QString& sessionId, int legIndex) {
    const QString id = normalizedSessionId(sessionId);
    if (id.isEmpty()) return;
    auto& row = counts[id];
    if (legIndex == 0) ++row.firstLeg;
    else if (legIndex == 1) ++row.secondLeg;
}

void scanResultDir(const QDir& dir, QHash<QString, BacktestLegCounts>& counts) {
    if (!dir.exists()) return;
    const QFileInfoList resultDirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& resultDir : resultDirs) {
        if (resultDir.fileName() == QStringLiteral("sweeps")) continue;
        const QString manifestPath = QDir(resultDir.absoluteFilePath()).absoluteFilePath(QStringLiteral("manifest.json"));
        const QJsonObject manifest = readManifestObject(manifestPath);
        if (!isBacktestResultManifest(manifest)) continue;
        addLegCount(counts, resultLegSessionId(manifestPath, manifest, 0), 0);
        addLegCount(counts, resultLegSessionId(manifestPath, manifest, 1), 1);
    }
}

void scanSessionBacktests(const QString& sessionPath, QHash<QString, BacktestLegCounts>& counts) {
    const QDir backtestsDir(QDir(sessionPath).absoluteFilePath(QStringLiteral("backtests")));
    scanResultDir(backtestsDir, counts);
    scanResultDir(QDir(backtestsDir.absoluteFilePath(QStringLiteral("sweeps"))), counts);
}

QString formatStartedAt(qint64 startedAtNs) {
    if (startedAtNs < 1000000000000000ll) return {};
    return QDateTime::fromMSecsSinceEpoch(startedAtNs / 1000000).toLocalTime().toString(QStringLiteral("dd.MM.yy hh:mm"));
}

}  // namespace

QString sessionIdFromSessionPathText(const QString& sessionPath) {
    return normalizedSessionId(sessionPath);
}

QHash<QString, BacktestLegCounts> backtestLegCountsBySession(const QString& recordingsRoot) {
    QHash<QString, BacktestLegCounts> counts;
    const auto discovery = hftrec::recordings::discoverRecordings(recordingsRoot.toStdString());
    for (const auto& session : discovery.sessions) {
        scanSessionBacktests(QString::fromStdString(session.path.string()), counts);
    }
    return counts;
}

BacktestLegCounts backtestLegCountsForSession(const QString& recordingsRoot, const QString& sessionId) {
    const QString targetSessionId = normalizedSessionId(sessionId);
    if (targetSessionId.isEmpty()) return {};
    return backtestLegCountsBySession(recordingsRoot).value(targetSessionId);
}

QString sessionBacktestSummaryText(int bookTickerCount, const BacktestLegCounts& counts, qint64 startedAtNs) {
    QString summary = QStringLiteral("L1 %1 | 1BT %2 | 2BT %3")
        .arg(bookTickerCount)
        .arg(counts.firstLeg)
        .arg(counts.secondLeg);
    const QString startedAt = formatStartedAt(startedAtNs);
    if (!startedAt.isEmpty()) summary += QStringLiteral(" | %1").arg(startedAt);
    return summary;
}

bool backtestManifestMatchesLegs(const QString& manifestPath, const QString& primarySessionId, const QString& secondarySessionId) {
    const QString primaryId = normalizedSessionId(primarySessionId);
    const QString secondaryId = normalizedSessionId(secondarySessionId);
    if (primaryId.isEmpty()) return false;

    const QJsonObject manifest = readManifestObject(manifestPath);
    if (!isBacktestResultManifest(manifest)) return false;

    const QString firstLegId = resultLegSessionId(manifestPath, manifest, 0);
    const QString secondLegId = resultLegSessionId(manifestPath, manifest, 1);
    if (secondaryId.isEmpty()) {
        return firstLegId.isEmpty() || (firstLegId == primaryId && secondLegId.isEmpty());
    }
    if (firstLegId.isEmpty() || secondLegId.isEmpty()) return false;
    return (firstLegId == primaryId && secondLegId == secondaryId)
        || (firstLegId == secondaryId && secondLegId == primaryId);
}

}  // namespace hftrec::gui
