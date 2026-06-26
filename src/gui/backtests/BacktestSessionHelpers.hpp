#pragma once

#include "gui/backtests/BacktestSessionSummary.hpp"

#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace hftrec::gui {

QString resolveRecordingsRoot();
QString sessionSourceSummary(const QString& sessionPath, const BacktestLegCounts& backtestCounts);
QString manifestValue(const QString& sessionPath, const QString& key);
QString symbolFromSessionId(const QString& sessionId);
QString venueSectionFor(const QString& exchange, const QString& market);
bool isVenueSectionKnown(const QString& exchange, const QString& market);
QString venueSectionForSession(const QString& sessionPath);
QString symbolForSessionPath(const QString& sessionPath);
QString sessionPathFromToken(const QString& recordingsRoot, const QString& token);
QVariantMap sessionRowById(const QVariantList& rows, const QString& id);
bool sessionRowSelectable(const QVariantMap& row);
QString firstSelectableSessionId(const QVariantList& rows);
QStringList sessionPathsFromRow(const QVariantMap& row);

}  // namespace hftrec::gui
