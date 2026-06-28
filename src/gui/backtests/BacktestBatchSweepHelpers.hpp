#pragma once

#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

namespace hftrec::gui {

struct BatchSweepSessionInfo {
    QString path{};
    QString sessionId{};
    QString exchange{};
    QString market{};
    QString symbol{};
    QString canonicalSymbol{};
    QString venue{};
};

struct BatchSweepPair {
    int pairId{0};
    BatchSweepSessionInfo first{};
    BatchSweepSessionInfo second{};
};

bool isBatchFuturesMarket(const QString& market);
QString batchCanonicalSymbol(const QString& symbol);
QString batchExchangePairLabel(const BatchSweepPair& pair);
QString batchParamsLabel(const QVariantMap& params);
QVector<BatchSweepPair> buildBatchSweepPairs(const QVector<BatchSweepSessionInfo>& sessions,
                                             int maxPairs,
                                             bool onlyFutures,
                                             QVariantList* skippedRows = nullptr);
QVariantList batchStableRowsFromRows(const QVariantList& rows);
QVariantList batchProfitRowsFromRows(const QVariantList& rows);
QVariantList batchSymbolRowsFromRows(const QVariantList& rows);
QVariantList batchPairRowsFromRows(const QVariantList& rows);
QVariantList batchParamRowsFromRows(const QVariantList& rows);

}  // namespace hftrec::gui
