#pragma once

#include <QHash>
#include <QString>

namespace hftrec::gui {

struct BacktestLegCounts {
    int firstLeg{0};
    int secondLeg{0};
};

QHash<QString, BacktestLegCounts> backtestLegCountsBySession(const QString& recordingsRoot);
BacktestLegCounts backtestLegCountsForSession(const QString& recordingsRoot, const QString& sessionId);
QString sessionBacktestSummaryText(int bookTickerCount, const BacktestLegCounts& counts, qint64 startedAtNs);
bool backtestManifestMatchesLegs(const QString& manifestPath, const QString& primarySessionId, const QString& secondarySessionId);
QString sessionIdFromSessionPathText(const QString& sessionPath);

}  // namespace hftrec::gui
