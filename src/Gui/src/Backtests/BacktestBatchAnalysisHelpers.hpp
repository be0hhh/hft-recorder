#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace hftrec::gui {

QString batchAnalysisParamsLabel(const QVariantMap& params);
QVariantList batchSummaryCardsFromRows(const QVariantList& rows,
                                       const QVariantList& skippedRows,
                                       int pairCount,
                                       quint64 pointsEvaluated);
QVariantList batchPairMatrixColumnsFromRows(const QVariantList& rows);
QVariantList batchPairMatrixCellsFromRows(const QVariantList& rows);
QVariantList batchTimeRowsFromCurves(const QVariantList& curves);
QVariantList batchPlateauRowsFromRows(const QVariantList& rows);

}  // namespace hftrec::gui
