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

QStringList resultLegSessionIds(const QJsonObject& manifest) {
    QStringList ids;
    const QJsonArray legs = manifest.value(QStringLiteral("legs")).toArray();
    if (legs.empty()) {
        const QString primary = normalizedSessionId(manifest.value(QStringLiteral("session_path")).toString());
        if (!primary.isEmpty()) ids.push_back(primary);
        return ids;
    }
    ids.reserve(legs.size());
    for (int i = 0; i < legs.size(); ++i) {
        ids.push_back(QString{});
    }
    for (int i = 0; i < legs.size(); ++i) {
        const QJsonObject leg = legs.at(i).toObject();
        const int index = leg.value(QStringLiteral("leg_index")).toInt(i);
        if (index < 0 || index >= ids.size()) continue;
        ids[index] = normalizedSessionId(leg.value(QStringLiteral("session_path")).toString());
    }
    while (!ids.empty() && ids.last().isEmpty()) ids.removeLast();
    return ids;
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

QStringList sweepRowLegSessionIds(const QString& manifestPath, const QJsonObject& manifest) {
    const QStringList keys = {QStringLiteral("rows"), QStringLiteral("curves")};
    const QStringList fallbacks = {QStringLiteral("sweep_results.jsonl"), QStringLiteral("sweep_curves.jsonl")};
    for (int i = 0; i < keys.size(); ++i) {
        const QJsonObject row = readFirstJsonlObject(resultArtifactPath(manifestPath, manifest, keys.at(i), fallbacks.at(i)));
        QStringList ids = resultLegSessionIds(row);
        if (!ids.empty()) return ids;
    }
    return {};
}

QStringList resultLegSessionIds(const QString& manifestPath, const QJsonObject& manifest) {
    QStringList ids = resultLegSessionIds(manifest);
    if (!ids.empty() || manifest.value(QStringLiteral("type")).toString() != QStringLiteral("sweep.result.v1")) return ids;
    return sweepRowLegSessionIds(manifestPath, manifest);
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
    ++row.total;
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
        const QStringList legSessionIds = resultLegSessionIds(manifestPath, manifest);
        for (int legIndex = 0; legIndex < legSessionIds.size(); ++legIndex) {
            addLegCount(counts, legSessionIds.at(legIndex), legIndex);
        }
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
    const int total = counts.total > 0 ? counts.total : counts.firstLeg + counts.secondLeg;
    QString summary = QStringLiteral("L1 %1 | BT %2")
        .arg(bookTickerCount)
        .arg(total);
    const QString startedAt = formatStartedAt(startedAtNs);
    if (!startedAt.isEmpty()) summary += QStringLiteral(" | %1").arg(startedAt);
    return summary;
}

QString compactCaptureWarning(QString warning) {
    warning = warning.trimmed();
    if (warning.isEmpty()) return {};

    const QString statusNeedle = QStringLiteral("route status=");
    const QString streamNeedle = QStringLiteral("stream=");
    const qsizetype statusPos = warning.indexOf(statusNeedle);
    const qsizetype streamPos = warning.indexOf(streamNeedle);
    if (statusPos >= 0 && streamPos >= 0) {
        const qsizetype statusBegin = statusPos + statusNeedle.size();
        qsizetype statusEnd = warning.indexOf(QLatin1Char(' '), statusBegin);
        if (statusEnd < 0) statusEnd = warning.size();
        const qsizetype streamBegin = streamPos + streamNeedle.size();
        qsizetype streamEnd = warning.indexOf(QLatin1Char(' '), streamBegin);
        if (streamEnd < 0) streamEnd = warning.size();
        const QString status = warning.mid(statusBegin, statusEnd - statusBegin).trimmed();
        const QString stream = warning.mid(streamBegin, streamEnd - streamBegin).trimmed();
        if (!status.isEmpty() && !stream.isEmpty()) return QStringLiteral("%1 %2").arg(stream, status);
    }

    constexpr qsizetype kMaxWarningChars = 80;
    if (warning.size() <= kMaxWarningChars) return warning;
    return warning.left(kMaxWarningChars - 3) + QStringLiteral("...");
}

QString sessionHealthSummaryLabel(const QString& sessionHealth, const QString& warningSummary) {
    const QString health = sessionHealth.trimmed().toLower();
    const QString compactWarning = compactCaptureWarning(warningSummary);
    if ((health.isEmpty() || health == QStringLiteral("clean")) && compactWarning.isEmpty()) return {};

    QString label = health.isEmpty() || health == QStringLiteral("clean") ? QStringLiteral("degraded") : health;
    if (!compactWarning.isEmpty()) label += QStringLiteral(": %1").arg(compactWarning);
    return label;
}

QString appendSessionHealthSummary(QString summary, const QString& sessionHealth, const QString& warningSummary) {
    const QString label = sessionHealthSummaryLabel(sessionHealth, warningSummary);
    return label.isEmpty() ? summary : summary + QStringLiteral(" | %1").arg(label);
}

bool backtestManifestMatchesLegs(const QString& manifestPath, const QString& primarySessionId, const QString& secondarySessionId) {
    QStringList expected{primarySessionId};
    if (!secondarySessionId.trimmed().isEmpty()) expected.push_back(secondarySessionId);
    return backtestManifestMatchesLegs(manifestPath, expected);
}

bool backtestManifestMatchesLegs(const QString& manifestPath, const QStringList& sessionIds) {
    QStringList expected;
    for (const QString& sessionId : sessionIds) {
        const QString normalized = normalizedSessionId(sessionId);
        if (!normalized.isEmpty() && !expected.contains(normalized)) expected.push_back(normalized);
    }
    if (expected.empty()) return false;

    const QJsonObject manifest = readManifestObject(manifestPath);
    if (!isBacktestResultManifest(manifest)) return false;

    const QStringList actual = resultLegSessionIds(manifestPath, manifest);
    if (expected.size() == 1) {
        return actual.empty() || (actual.size() == 1 && actual.front() == expected.front());
    }
    if (expected.size() == 2) {
        return actual.size() == 2 && actual.contains(expected.at(0)) && actual.contains(expected.at(1));
    }
    if (actual.size() != expected.size()) return false;
    for (int i = 0; i < expected.size(); ++i) {
        if (actual.at(i) != expected.at(i)) return false;
    }
    return true;
}

}  // namespace hftrec::gui
