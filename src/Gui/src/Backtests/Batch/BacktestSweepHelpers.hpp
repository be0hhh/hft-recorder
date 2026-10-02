#pragma once

#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace hftrec::gui {

QVariantList sweepParamModeChoices();
QVariantList sweepCurveLimitChoiceRows();
QVariantList sweepViewChoiceRows();
QVariantList sweepMetricChoiceRows();
QString sweepLegMetricKey(int legIndex);
QVariantMap sweepMapForMetric(QVariantMap out, const QString& metricKey);
QStringList sweepParamKeysFromManifest(const QJsonObject& object);
void appendSweepParamKeysFromRows(const QVariantList& rows, QStringList& keys);
QVariantList sweepCurvesFromJsonl(const QString& path);
QVariantList sweepRowsFromJsonl(const QString& path, const QString& metricKey);

}  // namespace hftrec::gui
