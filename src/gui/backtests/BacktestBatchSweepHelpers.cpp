#include "gui/backtests/BacktestBatchSweepHelpers.hpp"

#include <QHash>
#include <QStringList>

#include <algorithm>
#include <limits>

namespace hftrec::gui {
namespace {

qint64 absI64(qint64 value) noexcept {
    return value < 0 ? -value : value;
}

QString normalizedMarket(QString market) {
    return market.trimmed().toLower();
}

bool rowOk(const QVariantMap& row) {
    const QString status = row.value(QStringLiteral("status")).toString();
    return (status.isEmpty() || status == QStringLiteral("Ok") || status == QStringLiteral("complete")) &&
           !row.value(QStringLiteral("riskStopped")).toBool() &&
           !row.value(QStringLiteral("risk_stopped")).toBool() &&
           !row.value(QStringLiteral("liquidated")).toBool() &&
           row.value(QStringLiteral("fills")).toLongLong() > 0;
}

qint64 rowPnl(const QVariantMap& row) {
    if (row.contains(QStringLiteral("totalPnlE8"))) return row.value(QStringLiteral("totalPnlE8")).toLongLong();
    return row.value(QStringLiteral("total_pnl_e8")).toLongLong();
}

qint64 rowDrawdown(const QVariantMap& row) {
    if (row.contains(QStringLiteral("maxDrawdownE8"))) return row.value(QStringLiteral("maxDrawdownE8")).toLongLong();
    return row.value(QStringLiteral("max_drawdown_e8")).toLongLong();
}

qint64 rowStableScore(const QVariantMap& row) {
    if (!rowOk(row)) return std::numeric_limits<qint64>::min() / 4;
    const qint64 pnl = rowPnl(row);
    const qint64 positiveBonus = pnl > 0 ? 1000000000000ll : 0ll;
    return positiveBonus + pnl - absI64(rowDrawdown(row));
}

QString rowText(const QVariantMap& row, const QString& camelKey, const QString& snakeKey) {
    const QString camel = row.value(camelKey).toString().trimmed();
    if (!camel.isEmpty()) return camel;
    return row.value(snakeKey).toString().trimmed();
}

QString basisChainSessionSkipReason(const BatchSweepSessionInfo& session, const QString& role) {
    if (!session.sessionDirExists) return QStringLiteral("%1 session directory missing").arg(role);
    if (!session.manifestPresent) return QStringLiteral("%1 manifest missing").arg(role);
    if (session.candleRows <= 0) return QStringLiteral("%1 has no candle rows").arg(role);
    return {};
}

struct Aggregate {
    QString key{};
    QString label{};
    int rows{0};
    int okRows{0};
    int positiveRows{0};
    qint64 pnlSum{0};
    qint64 bestPnl{std::numeric_limits<qint64>::min()};
    qint64 worstDrawdown{0};
    qint64 fills{0};
};

void addAggregate(QHash<QString, Aggregate>& map, const QString& key, const QString& label, const QVariantMap& row) {
    if (key.trimmed().isEmpty()) return;
    Aggregate& agg = map[key];
    if (agg.key.isEmpty()) {
        agg.key = key;
        agg.label = label.trimmed().isEmpty() ? key : label;
    }
    const qint64 pnl = rowPnl(row);
    ++agg.rows;
    if (rowOk(row)) ++agg.okRows;
    if (pnl > 0) ++agg.positiveRows;
    agg.pnlSum += pnl;
    agg.bestPnl = std::max(agg.bestPnl, pnl);
    agg.worstDrawdown = std::max(agg.worstDrawdown, absI64(rowDrawdown(row)));
    agg.fills += row.value(QStringLiteral("fills")).toLongLong();
}

QVariantMap aggregateRow(const Aggregate& agg) {
    QVariantMap out;
    const qint64 avgPnl = agg.rows <= 0 ? 0 : agg.pnlSum / agg.rows;
    const qint64 okBps = agg.rows <= 0 ? 0 : (static_cast<qint64>(agg.okRows) * 10000ll) / agg.rows;
    const qint64 positiveBps = agg.rows <= 0 ? 0 : (static_cast<qint64>(agg.positiveRows) * 10000ll) / agg.rows;
    const qint64 stableScore = positiveBps * 100000000ll + okBps * 1000000ll + avgPnl - agg.worstDrawdown;
    out.insert(QStringLiteral("key"), agg.key);
    out.insert(QStringLiteral("label"), agg.label);
    out.insert(QStringLiteral("rows"), agg.rows);
    out.insert(QStringLiteral("okRows"), agg.okRows);
    out.insert(QStringLiteral("positiveRows"), agg.positiveRows);
    out.insert(QStringLiteral("positivePct"), agg.rows <= 0 ? 0 : (agg.positiveRows * 100) / agg.rows);
    out.insert(QStringLiteral("avgPnlE8"), avgPnl);
    out.insert(QStringLiteral("bestPnlE8"), agg.bestPnl == std::numeric_limits<qint64>::min() ? 0 : agg.bestPnl);
    out.insert(QStringLiteral("worstDrawdownE8"), agg.worstDrawdown);
    out.insert(QStringLiteral("fills"), agg.fills);
    out.insert(QStringLiteral("stableScore"), stableScore);
    return out;
}

QVariantList sortedAggregates(QHash<QString, Aggregate> map) {
    QVariantList out;
    for (const Aggregate& agg : map) out.push_back(aggregateRow(agg));
    std::sort(out.begin(), out.end(), [](const QVariant& lhs, const QVariant& rhs) {
        const QVariantMap a = lhs.toMap();
        const QVariantMap b = rhs.toMap();
        const qint64 scoreA = a.value(QStringLiteral("stableScore")).toLongLong();
        const qint64 scoreB = b.value(QStringLiteral("stableScore")).toLongLong();
        if (scoreA != scoreB) return scoreA > scoreB;
        return a.value(QStringLiteral("bestPnlE8")).toLongLong() > b.value(QStringLiteral("bestPnlE8")).toLongLong();
    });
    return out;
}

}  // namespace

bool isBatchFuturesMarket(const QString& market) {
    const QString value = normalizedMarket(market);
    return value == QStringLiteral("futures") ||
           value == QStringLiteral("future") ||
           value == QStringLiteral("swap") ||
           value == QStringLiteral("linear") ||
           value == QStringLiteral("usdt") ||
           value == QStringLiteral("usdc") ||
           value == QStringLiteral("inverse");
}

QString batchCanonicalSymbol(const QString& symbol) {
    QString out;
    const QString upper = symbol.trimmed().toUpper();
    out.reserve(upper.size());
    for (const QChar ch : upper) {
        if (ch.isLetterOrNumber()) out.push_back(ch);
    }
    const QStringList suffixes{QStringLiteral("SWAP"), QStringLiteral("PERP")};
    for (const QString& suffix : suffixes) {
        if (out.size() > suffix.size() && out.endsWith(suffix)) {
            out.chop(suffix.size());
            break;
        }
    }
    return out;
}

QString batchExchangePairLabel(const BatchSweepPair& pair) {
    return QStringLiteral("%1/%2").arg(pair.first.exchange, pair.second.exchange);
}

QString batchParamsLabel(const QVariantMap& params) {
    QStringList keys = params.keys();
    std::sort(keys.begin(), keys.end());
    QStringList parts;
    for (const QString& key : keys) parts.push_back(QStringLiteral("%1=%2").arg(key, params.value(key).toString()));
    return parts.join(QStringLiteral(", "));
}

QString basisChainSpotSkipReason(const BatchSweepSessionInfo& session) {
    return basisChainSessionSkipReason(session, QStringLiteral("spot"));
}

QString basisChainFutureSkipReason(const BatchSweepSessionInfo& session) {
    const QString availability = basisChainSessionSkipReason(session, QStringLiteral("future"));
    if (!availability.isEmpty()) return availability;
    if (session.expiryUtcNs <= 0) return QStringLiteral("future missing expiry_utc_ns");
    if (session.priceBasisQtyE8 <= 0) return QStringLiteral("future missing price_basis_qty_e8");
    return {};
}

QVector<BatchSweepPair> buildBatchSweepPairs(const QVector<BatchSweepSessionInfo>& sessions,
                                             int maxPairs,
                                             bool onlyFutures,
                                             QVariantList* skippedRows) {
    QHash<QString, QVector<BatchSweepSessionInfo>> bySymbol;
    for (const BatchSweepSessionInfo& session : sessions) {
        const QString symbol = batchCanonicalSymbol(session.canonicalSymbol.isEmpty() ? session.symbol : session.canonicalSymbol);
        QVariantMap skipped;
        skipped.insert(QStringLiteral("sessionId"), session.sessionId);
        skipped.insert(QStringLiteral("path"), session.path);
        skipped.insert(QStringLiteral("exchange"), session.exchange);
        skipped.insert(QStringLiteral("market"), session.market);
        skipped.insert(QStringLiteral("symbol"), session.symbol);
        skipped.insert(QStringLiteral("canonicalSymbol"), symbol);
        if (symbol.isEmpty()) {
            skipped.insert(QStringLiteral("reason"), QStringLiteral("missing symbol"));
            if (skippedRows != nullptr) skippedRows->push_back(skipped);
            continue;
        }
        if (onlyFutures && !isBatchFuturesMarket(session.market)) {
            skipped.insert(QStringLiteral("reason"), QStringLiteral("not futures"));
            if (skippedRows != nullptr) skippedRows->push_back(skipped);
            continue;
        }
        bySymbol[symbol].push_back(session);
    }

    QVector<BatchSweepPair> pairs;
    int pairId = 0;
    QStringList symbols = bySymbol.keys();
    std::sort(symbols.begin(), symbols.end());
    for (const QString& symbol : symbols) {
        QVector<BatchSweepSessionInfo> rows = bySymbol.value(symbol);
        std::sort(rows.begin(), rows.end(), [](const BatchSweepSessionInfo& lhs, const BatchSweepSessionInfo& rhs) {
            if (lhs.exchange != rhs.exchange) return lhs.exchange < rhs.exchange;
            if (lhs.market != rhs.market) return lhs.market < rhs.market;
            return lhs.sessionId < rhs.sessionId;
        });
        for (int i = 0; i < rows.size(); ++i) {
            for (int j = i + 1; j < rows.size(); ++j) {
                if (rows[i].exchange.trimmed().toLower() == rows[j].exchange.trimmed().toLower()) continue;
                BatchSweepPair pair;
                pair.pairId = pairId++;
                pair.first = rows[i];
                pair.second = rows[j];
                pairs.push_back(std::move(pair));
                if (maxPairs > 0 && pairs.size() >= maxPairs) return pairs;
            }
        }
    }
    return pairs;
}

QVariantList batchStableRowsFromRows(const QVariantList& rows) {
    QVariantList out = rows;
    std::sort(out.begin(), out.end(), [](const QVariant& lhs, const QVariant& rhs) {
        const QVariantMap a = lhs.toMap();
        const QVariantMap b = rhs.toMap();
        const qint64 scoreA = rowStableScore(a);
        const qint64 scoreB = rowStableScore(b);
        if (scoreA != scoreB) return scoreA > scoreB;
        return rowPnl(a) > rowPnl(b);
    });
    return out;
}

QVariantList batchProfitRowsFromRows(const QVariantList& rows) {
    QVariantList out = rows;
    std::sort(out.begin(), out.end(), [](const QVariant& lhs, const QVariant& rhs) {
        return rowPnl(lhs.toMap()) > rowPnl(rhs.toMap());
    });
    return out;
}

QVariantList batchSymbolRowsFromRows(const QVariantList& rows) {
    QHash<QString, Aggregate> map;
    for (const QVariant& value : rows) {
        const QVariantMap row = value.toMap();
        const QString symbol = rowText(row, QStringLiteral("symbol"), QStringLiteral("symbol"));
        addAggregate(map, symbol, symbol, row);
    }
    return sortedAggregates(map);
}

QVariantList batchPairRowsFromRows(const QVariantList& rows) {
    QHash<QString, Aggregate> map;
    for (const QVariant& value : rows) {
        const QVariantMap row = value.toMap();
        const QString pair = rowText(row, QStringLiteral("exchangePair"), QStringLiteral("exchange_pair"));
        addAggregate(map, pair, pair, row);
    }
    return sortedAggregates(map);
}

QVariantList batchParamRowsFromRows(const QVariantList& rows) {
    QHash<QString, Aggregate> map;
    for (const QVariant& value : rows) {
        const QVariantMap row = value.toMap();
        const QVariantMap params = row.value(QStringLiteral("params")).toMap();
        const QString label = batchParamsLabel(params);
        addAggregate(map, label, label, row);
    }
    return sortedAggregates(map);
}

}  // namespace hftrec::gui
