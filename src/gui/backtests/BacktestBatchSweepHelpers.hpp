#pragma once

#include <QString>
#include <QtGlobal>
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
    qint64 candleRows{0};
    qint64 expiryUtcNs{0};
    qint64 priceBasisQtyE8{0};
    bool sessionDirExists{false};
    bool manifestPresent{false};
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
QString basisChainSpotSkipReason(const BatchSweepSessionInfo& session);
QString basisChainFutureSkipReason(const BatchSweepSessionInfo& session);
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
