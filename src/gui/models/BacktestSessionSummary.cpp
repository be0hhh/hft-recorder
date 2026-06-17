#include "gui/models/BacktestSessionSummary.hpp"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStringList>

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

void scanResultDir(const QDir& dir, const QString& targetSessionId, BacktestLegCounts& counts) {
    if (!dir.exists()) return;
    const QFileInfoList resultDirs = dir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& resultDir : resultDirs) {
        if (resultDir.fileName() == QStringLiteral("sweeps")) continue;
        const QString manifestPath = QDir(resultDir.absoluteFilePath()).absoluteFilePath(QStringLiteral("manifest.json"));
        const QJsonObject manifest = readManifestObject(manifestPath);
        if (!isBacktestResultManifest(manifest)) continue;
        if (resultLegSessionId(manifest, 0) == targetSessionId) ++counts.firstLeg;
        if (resultLegSessionId(manifest, 1) == targetSessionId) ++counts.secondLeg;
    }
}

void scanSessionBacktests(const QString& sessionPath, const QString& targetSessionId, BacktestLegCounts& counts) {
    const QDir backtestsDir(QDir(sessionPath).absoluteFilePath(QStringLiteral("backtests")));
    scanResultDir(backtestsDir, targetSessionId, counts);
    scanResultDir(QDir(backtestsDir.absoluteFilePath(QStringLiteral("sweeps"))), targetSessionId, counts);
}

QString formatStartedAt(qint64 startedAtNs) {
    if (startedAtNs < 1000000000000000ll) return {};
    return QDateTime::fromMSecsSinceEpoch(startedAtNs / 1000000).toLocalTime().toString(QStringLiteral("dd.MM.yy hh:mm"));
}

}  // namespace

QString sessionIdFromSessionPathText(const QString& sessionPath) {
    return normalizedSessionId(sessionPath);
}

BacktestLegCounts backtestLegCountsForSession(const QString& recordingsRoot, const QString& sessionId) {
    const QString targetSessionId = normalizedSessionId(sessionId);
    BacktestLegCounts counts{};
    if (targetSessionId.isEmpty()) return counts;

    const QDir recordingsDir(recordingsRoot);
    if (!recordingsDir.exists()) return counts;
    const QStringList entries = recordingsDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& entry : entries) {
        if (entry == QStringLiteral("backtest_profiles")) continue;
        scanSessionBacktests(recordingsDir.absoluteFilePath(entry), targetSessionId, counts);
    }
    return counts;
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

    const QString firstLegId = resultLegSessionId(manifest, 0);
    const QString secondLegId = resultLegSessionId(manifest, 1);
    if (secondaryId.isEmpty()) {
        return firstLegId.isEmpty() || firstLegId == primaryId;
    }
    if (firstLegId.isEmpty() || secondLegId.isEmpty()) return false;
    return (firstLegId == primaryId && secondLegId == secondaryId)
        || (firstLegId == secondaryId && secondLegId == primaryId);
}

}  // namespace hftrec::gui
