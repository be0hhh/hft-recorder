#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QVariantList>

#include <vector>

namespace hftrec::gui {

QString jsonValueString(const QJsonObject& object, const QString& key);
QString manifestObjectValue(const QJsonObject& object, const QString& key);
bool isE8Key(const QString& key);
QString e8DisplayString(qint64 value);
QString humanSummaryJson(const QJsonValue& value);
int errorCount(const QJsonValue& value);
QVariantList resultMetrics(const QJsonObject& root, const QJsonObject& summary);
QString pnlPercentText(qint64 pnlE8, qint64 initialBalanceE8);
QVariantList equityPointsFromJsonl(const QString& path,
                                   const QJsonObject& summary,
                                   qint64 totalRows,
                                   qint64& minPnl,
                                   qint64& maxPnl);
QVariantList synthesizePortfolioEquityPoints(const std::vector<QVariantList>& legSeries,
                                             qint64& minPnl,
                                             qint64& maxPnl);

}  // namespace hftrec::gui
