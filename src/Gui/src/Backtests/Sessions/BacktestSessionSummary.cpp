#include "BacktestSessionSummary.hpp"

#include <QDateTime>
#include <QByteArray>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QStringList>

#include <algorithm>

#include "Corpus/Recordings/RecordingGroups.hpp"
#include "Corpus/Recordings/RecordingRoot.hpp"

namespace hftrec::gui {
namespace {

struct BacktestScanBudget {
    std::size_t entries{0u},bytes{0u};
    QString recordingsRoot;
    QHash<QString,QString> sessionIdsByPath,jsonSessionIdsByBasename;
    bool binaryCorpus{false};
    QString error;
    bool charge(std::size_t amount) {
        if(amount>recordings::kRecordingMaxAggregateInputBytes-bytes) {
            error=QStringLiteral("backtest index input byte bound exceeded");return false;
        }
        bytes+=amount;return true;
    }
};

bool readBacktestMetadata(const QString& path,recordings::RecordingMetadataReadMode mode,
                          std::string& output,BacktestScanBudget* budget) {
    const auto root=budget?std::filesystem::path{budget->recordingsRoot.toStdString()}:recordings::defaultRecordingsRoot();
    std::size_t maximumBytes=recordings::kRecordingMaxSessionManifestBytes;
    if(budget) {
        maximumBytes=std::min(maximumBytes,recordings::kRecordingMaxAggregateInputBytes-budget->bytes);
        if(!maximumBytes) {budget->error=QStringLiteral("backtest index input byte bound exceeded");return false;}
    }
    std::string error;
    std::size_t consumed=0u;
    const auto result=recordings::readRecordingMetadata(root,path.toStdString(),maximumBytes,output,&error,mode,&consumed);
    if(budget && !budget->charge(consumed)) return false;
    if(result==recordings::RecordingMetadataReadResult::Refused && budget)
        budget->error=QStringLiteral("backtest metadata refused: ")+QString::fromStdString(error);
    return result==recordings::RecordingMetadataReadResult::Ready;
}

bool containedArtifact(const QString& root,const QString& path) {
    if(root.size()>static_cast<qsizetype>(recordings::kRecordingMaxPathBytes) ||
       path.size()>static_cast<qsizetype>(recordings::kRecordingMaxPathBytes)) return false;
    const QDir directory(QDir::cleanPath(QFileInfo(root).absoluteFilePath()));
    const QString relative=directory.relativeFilePath(QFileInfo(path).absoluteFilePath());
    if(QDir::isAbsolutePath(relative) || relative==QStringLiteral("..") || relative.startsWith(QStringLiteral("../"))) return false;
    QString current=directory.absolutePath();
    if(QFileInfo(current).isSymLink()) return false;
    for(const auto& component:relative.split(QLatin1Char('/'),Qt::SkipEmptyParts)) {
        current=QDir(current).absoluteFilePath(component);
        if(QFileInfo(current).isSymLink()) return false;
    }
    return true;
}

QString normalizedSessionId(QString value) {
    value = value.trimmed();
    if (value.isEmpty()) return {};
    return QFileInfo(value).fileName();
}

QString capturedSessionId(const QString& value,const BacktestScanBudget* budget) {
    if(!budget) return normalizedSessionId(value);
    const QString trimmed=value.trimmed();
    if(trimmed.isEmpty()) return {};
    const QFileInfo info(trimmed);
    const QString path=QDir::cleanPath(info.isAbsolute()?info.absoluteFilePath():
        QDir(budget->recordingsRoot).absoluteFilePath(trimmed));
    const auto found=budget->sessionIdsByPath.constFind(path);
    if(found!=budget->sessionIdsByPath.constEnd()) return found.value();
    if(!info.isAbsolute() && !budget->binaryCorpus) return budget->jsonSessionIdsByBasename.value(info.fileName());
    return {};
}

QStringList resultLegSessionIds(const QJsonObject& manifest,const BacktestScanBudget* budget=nullptr) {
    QStringList ids;
    const QJsonArray legs = manifest.value(QStringLiteral("legs")).toArray();
    if(legs.size()>static_cast<qsizetype>(recordings::kRecordingMaxSymbols)) return {};
    if (legs.empty()) {
        const QString primary = capturedSessionId(manifest.value(QStringLiteral("session_path")).toString(),budget);
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
        ids[index] = capturedSessionId(leg.value(QStringLiteral("session_path")).toString(),budget);
    }
    while (!ids.empty() && ids.last().isEmpty()) ids.removeLast();
    return ids;
}

QString resultArtifactPath(const QString& manifestPath, const QJsonObject& manifest, const QString& key, const QString& fallback) {
    QString path = manifest.value(key).toObject().value(QStringLiteral("path")).toString(fallback);
    if (path.trimmed().isEmpty()) return {};
    const QFileInfo info(path);
    if (info.isRelative()) path = QFileInfo(manifestPath).dir().absoluteFilePath(path);
    if(!containedArtifact(QFileInfo(manifestPath).absolutePath(),path)) return {};
    return path;
}

QJsonObject readFirstJsonlObject(const QString& path,BacktestScanBudget* budget=nullptr) {
    std::string text;
    if(!readBacktestMetadata(path,recordings::RecordingMetadataReadMode::FirstNonemptyLine,text,budget)) return {};
    if(text.empty()) return {};
    QJsonParseError parseError;
    const QJsonDocument doc=QJsonDocument::fromJson(QByteArray::fromStdString(text),&parseError);
    if(budget && (parseError.error!=QJsonParseError::NoError || !doc.isObject()))
        budget->error=QStringLiteral("backtest row artifact is malformed");
    return doc.isObject()?doc.object():QJsonObject{};
}

QStringList sweepRowLegSessionIds(const QString& manifestPath, const QJsonObject& manifest,BacktestScanBudget* budget=nullptr) {
    const QStringList keys = {QStringLiteral("rows"), QStringLiteral("curves")};
    const QStringList fallbacks = {QStringLiteral("sweep_results.jsonl"), QStringLiteral("sweep_curves.jsonl")};
    for (int i = 0; i < keys.size(); ++i) {
        const QString path=resultArtifactPath(manifestPath,manifest,keys.at(i),fallbacks.at(i));
        if(path.isEmpty()) {
            if(budget) budget->error=QStringLiteral("backtest row artifact escapes its result directory");
            return {};
        }
        const QJsonObject row = readFirstJsonlObject(path,budget);
        if(row.value(QStringLiteral("legs")).toArray().size()>static_cast<qsizetype>(recordings::kRecordingMaxSymbols)) {
            if(budget) budget->error=QStringLiteral("backtest row leg count exceeds the symbol bound");
            return {};
        }
        QStringList ids = resultLegSessionIds(row,budget);
        if (!ids.empty()) return ids;
    }
    return {};
}

QStringList resultLegSessionIds(const QString& manifestPath, const QJsonObject& manifest,BacktestScanBudget* budget=nullptr) {
    if(manifest.value(QStringLiteral("legs")).toArray().size()>static_cast<qsizetype>(recordings::kRecordingMaxSymbols)) {
        if(budget) budget->error=QStringLiteral("backtest leg count exceeds the symbol bound");
        return {};
    }
    QStringList ids = resultLegSessionIds(manifest,budget);
    if (!ids.empty() || manifest.value(QStringLiteral("type")).toString() != QStringLiteral("sweep.result")) return ids;
    return sweepRowLegSessionIds(manifestPath, manifest,budget);
}

QJsonObject readManifestObject(const QString& manifestPath,BacktestScanBudget* budget=nullptr) {
    std::string text;
    if(!readBacktestMetadata(manifestPath,recordings::RecordingMetadataReadMode::Document,text,budget)) return {};
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(text),&parseError);
    if(budget && (parseError.error!=QJsonParseError::NoError || !doc.isObject()))
        budget->error=QStringLiteral("backtest manifest is malformed");
    return doc.isObject() ? doc.object() : QJsonObject{};
}

bool isBacktestResultManifest(const QJsonObject& manifest) {
    const QString type = manifest.value(QStringLiteral("type")).toString();
    return type == QStringLiteral("run.result") ||
           type == QStringLiteral("sweep.result");
}

void addLegCount(QHash<QString, BacktestLegCounts>& counts, const QString& sessionId, int legIndex) {
    const QString id = sessionId.trimmed();
    if (id.isEmpty()) return;
    auto& row = counts[id];
    ++row.total;
    if (legIndex == 0) ++row.firstLeg;
    else if (legIndex == 1) ++row.secondLeg;
}

void scanResultDir(const QDir& dir, QHash<QString, BacktestLegCounts>& counts,BacktestScanBudget& budget) {
    if (!dir.exists()) return;
    if(QFileInfo(dir.absolutePath()).isSymLink()) {budget.error=QStringLiteral("backtest result directory is a symlink");return;}
    QDirIterator iterator(dir.absolutePath(),QDir::AllEntries|QDir::NoDotAndDotDot|QDir::Hidden|QDir::System);
    while(iterator.hasNext() && budget.error.isEmpty()) {
        iterator.next();const QFileInfo resultDir=iterator.fileInfo();
        if(++budget.entries>recordings::kRecordingMaxEntries) {budget.error=QStringLiteral("backtest index entry bound exceeded");return;}
        if(resultDir.isSymLink()) {budget.error=QStringLiteral("backtest result entry is a symlink");return;}
        if(!resultDir.isDir()) continue;
        if (resultDir.fileName() == QStringLiteral("sweeps")) continue;
        const QString manifestPath = QDir(resultDir.absoluteFilePath()).absoluteFilePath(QStringLiteral("manifest.json"));
        const QJsonObject manifest = readManifestObject(manifestPath,&budget);
        if (!isBacktestResultManifest(manifest)) continue;
        const QStringList legSessionIds = resultLegSessionIds(manifestPath, manifest,&budget);
        for (int legIndex = 0; legIndex < legSessionIds.size(); ++legIndex) {
            if(counts.contains(legSessionIds.at(legIndex))) addLegCount(counts, legSessionIds.at(legIndex), legIndex);
        }
    }
}

void scanSessionBacktests(const QString& sessionPath, QHash<QString, BacktestLegCounts>& counts,BacktestScanBudget& budget) {
    const QDir backtestsDir(QDir(sessionPath).absoluteFilePath(QStringLiteral("backtests")));
    if(!containedArtifact(sessionPath,backtestsDir.absolutePath())) {budget.error=QStringLiteral("backtest directory escapes its session");return;}
    scanResultDir(backtestsDir, counts,budget);
    if(budget.error.isEmpty()) scanResultDir(QDir(backtestsDir.absoluteFilePath(QStringLiteral("sweeps"))), counts,budget);
}

QString formatStartedAt(qint64 startedAtNs) {
    if (startedAtNs < 1000000000000000ll) return {};
    return QDateTime::fromMSecsSinceEpoch(startedAtNs / 1000000).toLocalTime().toString(QStringLiteral("dd.MM.yy hh:mm"));
}

}  // namespace

QString sessionIdFromSessionPathText(const QString& sessionPath) {
    return normalizedSessionId(sessionPath);
}

QHash<QString, BacktestLegCounts> backtestLegCountsBySession(const QString& recordingsRoot,QString* error) {
    if(error) error->clear();
    const auto discovery = hftrec::recordings::discoverRecordings(recordingsRoot.toStdString());
    if(!discovery.complete || !discovery.errors.empty()) {
        if(error) *error=discovery.errors.empty()?QStringLiteral("recording discovery is incomplete"):QString::fromStdString(discovery.errors.front());
        return {};
    }
    return backtestLegCountsBySession(discovery.sessions,error,recordingsRoot);
}

QHash<QString, BacktestLegCounts> backtestLegCountsBySession(const std::vector<hftrec::recordings::RecordedSessionInfo>& sessions,
                                                         QString* error,const QString& recordingsRoot) {
    if(error) error->clear();
    QHash<QString, BacktestLegCounts> counts;
    if(sessions.size()>recordings::kRecordingMaxSessions) {if(error) *error=QStringLiteral("backtest session count exceeds the catalog bound");return {};}
    BacktestScanBudget budget;
    budget.recordingsRoot=recordingsRoot.isEmpty()?QString::fromStdString(recordings::defaultRecordingsRoot().string()):recordingsRoot;
    for(const auto& session:sessions) {
        const QString id=QString::fromStdString(session.sessionId);
        counts.insert(id,{});
        budget.sessionIdsByPath.insert(QDir::cleanPath(QString::fromStdString(session.path.string())),id);
        if(!session.binaryCorpus) budget.jsonSessionIdsByBasename.insert(QFileInfo(QString::fromStdString(session.path.string())).fileName(),id);
    }
    for (const auto& session : sessions) {
        budget.binaryCorpus=session.binaryCorpus;
        scanSessionBacktests(QString::fromStdString(session.path.string()), counts,budget);
        if(!budget.error.isEmpty()) {if(error) *error=budget.error;return {};}
    }
    return counts;
}

BacktestLegCounts backtestLegCountsForSession(const QString& recordingsRoot, const QString& sessionId) {
    const auto counts=backtestLegCountsBySession(recordingsRoot);
    if(counts.contains(sessionId)) return counts.value(sessionId);
    const QString targetSessionId = normalizedSessionId(sessionId);
    if (targetSessionId.isEmpty()) return {};
    return counts.value(targetSessionId);
}

QString sessionBacktestSummaryText(std::uint64_t bookTickerCount, const BacktestLegCounts& counts, qint64 startedAtNs) {
    const int total = counts.total > 0 ? counts.total : counts.firstLeg + counts.secondLeg;
    QString summary = QStringLiteral("L1 %1 | BT %2")
        .arg(bookTickerCount)
        .arg(total);
    const QString startedAt = formatStartedAt(startedAtNs);
    if (!startedAt.isEmpty()) summary += QStringLiteral(" | %1").arg(startedAt);
    return summary;
}

QString recordedSessionLabel(const recordings::RecordedSessionInfo& session) {
    if(session.binaryCorpus) return QStringLiteral("Binary corpus | %1 sources").arg(session.sourceCount);
    return QStringLiteral("%1/%2 %3").arg(QString::fromStdString(session.exchange),
        QString::fromStdString(session.market),
        QString::fromStdString(session.symbols.empty()?session.normalizedSymbol:session.symbols.front()));
}

QString recordedSessionDataSummary(const recordings::RecordedSessionInfo& session) {
    if(!session.channelCountsAvailable) return QStringLiteral("TOTAL RECORDS %1 | sources %2 | per-channel unknown")
        .arg(static_cast<qulonglong>(session.totalRows)).arg(session.sourceCount);
    return QStringLiteral("BTK %1 | TRD %2").arg(static_cast<qulonglong>(session.bookTickerCount))
        .arg(static_cast<qulonglong>(session.tradesCount));
}

QString recordedSessionSummary(const recordings::RecordedSessionInfo& session,const BacktestLegCounts& counts) {
    QString summary;
    if(session.channelCountsAvailable) {
        summary=sessionBacktestSummaryText(session.bookTickerCount,counts,session.startedAtNs);
        if(session.candleCount>0) summary+=QStringLiteral(" | C %1").arg(session.candleCount);
    } else {
        const int total=counts.total>0?counts.total:counts.firstLeg+counts.secondLeg;
        summary=recordedSessionDataSummary(session)+QStringLiteral(" | BT %1").arg(total);
        const auto startedAt=formatStartedAt(session.startedAtNs);
        if(!startedAt.isEmpty()) summary+=QStringLiteral(" | %1").arg(startedAt);
    }
    return appendSessionHealthSummary(summary,QString::fromStdString(session.sessionHealth),
        QString::fromStdString(session.warningSummary));
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
