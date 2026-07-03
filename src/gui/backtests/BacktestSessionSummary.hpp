#pragma once

#include <QHash>
#include <QString>
#include <QStringList>

#include <vector>

#include "core/recordings/RecordingDiscovery.hpp"

namespace hftrec::gui {

struct BacktestLegCounts {
    int firstLeg{0};
    int secondLeg{0};
    int total{0};
};

QHash<QString, BacktestLegCounts> backtestLegCountsBySession(const QString& recordingsRoot);
QHash<QString, BacktestLegCounts> backtestLegCountsBySession(const std::vector<hftrec::recordings::RecordedSessionInfo>& sessions);
BacktestLegCounts backtestLegCountsForSession(const QString& recordingsRoot, const QString& sessionId);
QString sessionBacktestSummaryText(int bookTickerCount, const BacktestLegCounts& counts, qint64 startedAtNs);
QString sessionHealthSummaryLabel(const QString& sessionHealth, const QString& warningSummary);
QString appendSessionHealthSummary(QString summary, const QString& sessionHealth, const QString& warningSummary);
bool backtestManifestMatchesLegs(const QString& manifestPath, const QString& primarySessionId, const QString& secondarySessionId);
bool backtestManifestMatchesLegs(const QString& manifestPath, const QStringList& sessionIds);
QString sessionIdFromSessionPathText(const QString& sessionPath);

}  // namespace hftrec::gui
