#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include <vector>

#include "Corpus/Recordings/RecordingGroups.hpp"

namespace hftrec::gui {

struct BacktestLegCounts {
    int firstLeg{0};
    int secondLeg{0};
    int total{0};
};

QHash<QString, BacktestLegCounts> backtestLegCountsBySession(const QString& recordingsRoot,QString* error = nullptr);
QHash<QString, BacktestLegCounts> backtestLegCountsBySession(const std::vector<hftrec::recordings::RecordedSessionInfo>& sessions,
                                                         QString* error = nullptr,
                                                         const QString& recordingsRoot = QString{});
BacktestLegCounts backtestLegCountsForSession(const QString& recordingsRoot, const QString& sessionId);
QString sessionBacktestSummaryText(std::uint64_t bookTickerCount, const BacktestLegCounts& counts, qint64 startedAtNs);
QString recordedSessionLabel(const recordings::RecordedSessionInfo& session);
QString recordedSessionDataSummary(const recordings::RecordedSessionInfo& session);
QString recordedSessionSummary(const recordings::RecordedSessionInfo& session,const BacktestLegCounts& counts);
QString sessionHealthSummaryLabel(const QString& sessionHealth, const QString& warningSummary);
QString appendSessionHealthSummary(QString summary, const QString& sessionHealth, const QString& warningSummary);
bool backtestManifestMatchesLegs(const QString& manifestPath, const QString& primarySessionId, const QString& secondarySessionId);
bool backtestManifestMatchesLegs(const QString& manifestPath, const QStringList& sessionIds);
QString sessionIdFromSessionPathText(const QString& sessionPath);

}  // namespace hftrec::gui
