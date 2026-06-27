#include "gui/backtests/BacktestBatchAnalysisHelpers.hpp"

#include <QHash>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <limits>
#include <utility>

namespace hftrec::gui {
namespace {

qint64 absI64(qint64 value) noexcept {
    return value < 0 ? -value : value;
}

qint64 pnlValue(const QVariantMap& row) {
    if (row.contains(QStringLiteral("totalPnlE8"))) return row.value(QStringLiteral("totalPnlE8")).toLongLong();
    return row.value(QStringLiteral("total_pnl_e8")).toLongLong();
}

qint64 drawdownValue(const QVariantMap& row) {
    if (row.contains(QStringLiteral("maxDrawdownE8"))) return row.value(QStringLiteral("maxDrawdownE8")).toLongLong();
    return row.value(QStringLiteral("max_drawdown_e8")).toLongLong();
}

bool rowOk(const QVariantMap& row) {
    const QString status = row.value(QStringLiteral("status")).toString();
    return (status.isEmpty() || status == QStringLiteral("Ok") || status == QStringLiteral("complete")) &&
           !row.value(QStringLiteral("riskStopped")).toBool() &&
           !row.value(QStringLiteral("risk_stopped")).toBool() &&
           !row.value(QStringLiteral("liquidated")).toBool() &&
           row.value(QStringLiteral("fills")).toLongLong() > 0;
}

bool rowPositive(const QVariantMap& row) {
    return rowOk(row) && pnlValue(row) > 0;
}

QString textValue(const QVariantMap& row, const QString& camelKey, const QString& snakeKey = {}) {
    const QString camel = row.value(camelKey).toString().trimmed();
    if (!camel.isEmpty() || snakeKey.isEmpty()) return camel;
    return row.value(snakeKey).toString().trimmed();
}

QString e8Text(qint64 value) {
    const bool negative = value < 0;
    qint64 absValue = absI64(value);
    const qint64 whole = absValue / 100000000ll;
    qint64 frac = absValue % 100000000ll;
    QString fracText = QStringLiteral("%1").arg(frac, 8, 10, QLatin1Char('0'));
    while (fracText.endsWith(QLatin1Char('0'))) fracText.chop(1);
    const QString body = fracText.isEmpty()
        ? QString::number(whole)
        : QStringLiteral("%1.%2").arg(QString::number(whole), fracText.left(4));
    return negative ? QStringLiteral("-%1").arg(body) : body;
}

QVariantMap card(QString id, QString label, QString value, QString detail, QString tone = {}) {
    QVariantMap out;
    out.insert(QStringLiteral("id"), std::move(id));
    out.insert(QStringLiteral("label"), std::move(label));
    out.insert(QStringLiteral("value"), std::move(value));
    out.insert(QStringLiteral("detail"), std::move(detail));
    out.insert(QStringLiteral("tone"), std::move(tone));
    return out;
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

void addAggregate(Aggregate& agg, const QVariantMap& row) {
    const qint64 pnl = pnlValue(row);
    ++agg.rows;
    if (rowOk(row)) ++agg.okRows;
    if (pnl > 0) ++agg.positiveRows;
    agg.pnlSum += pnl;
    agg.bestPnl = std::max(agg.bestPnl, pnl);
    agg.worstDrawdown = std::max(agg.worstDrawdown, absI64(drawdownValue(row)));
    agg.fills += row.value(QStringLiteral("fills")).toLongLong();
}

QVariantMap aggregateMap(const Aggregate& agg) {
    QVariantMap out;
    const qint64 avgPnl = agg.rows <= 0 ? 0 : agg.pnlSum / agg.rows;
    const int positivePct = agg.rows <= 0 ? 0 : (agg.positiveRows * 100) / agg.rows;
    const int okPct = agg.rows <= 0 ? 0 : (agg.okRows * 100) / agg.rows;
    const qint64 stableScore = static_cast<qint64>(positivePct) * 10000000000ll +
                               static_cast<qint64>(okPct) * 100000000ll +
                               avgPnl - agg.worstDrawdown;
    out.insert(QStringLiteral("key"), agg.key);
    out.insert(QStringLiteral("label"), agg.label);
    out.insert(QStringLiteral("rows"), agg.rows);
    out.insert(QStringLiteral("okRows"), agg.okRows);
    out.insert(QStringLiteral("positiveRows"), agg.positiveRows);
    out.insert(QStringLiteral("positivePct"), positivePct);
    out.insert(QStringLiteral("avgPnlE8"), avgPnl);
    out.insert(QStringLiteral("bestPnlE8"), agg.bestPnl == std::numeric_limits<qint64>::min() ? 0 : agg.bestPnl);
    out.insert(QStringLiteral("worstDrawdownE8"), agg.worstDrawdown);
    out.insert(QStringLiteral("fills"), agg.fills);
    out.insert(QStringLiteral("stableScore"), stableScore);
    return out;
}

QVariantList curveValues(const QVariantMap& row) {
    QVariantList curve = row.value(QStringLiteral("curve_e8")).toList();
    if (curve.empty()) curve = row.value(QStringLiteral("curve")).toList();
    return curve;
}

QString groupKey(const QVariantMap& row) {
    return textValue(row, QStringLiteral("symbol")) + QLatin1Char('|') +
           textValue(row, QStringLiteral("exchangePair"), QStringLiteral("exchange_pair"));
}

QString paramKey(const QVariantMap& params) {
    return batchAnalysisParamsLabel(params);
}

bool adjacentParamRows(const QVariantMap& lhs,
                       const QVariantMap& rhs,
                       const QHash<QString, QList<qint64>>& valueRanks) {
    const QVariantMap a = lhs.value(QStringLiteral("params")).toMap();
    const QVariantMap b = rhs.value(QStringLiteral("params")).toMap();
    QStringList keys = a.keys();
    for (const QString& key : b.keys()) {
        if (!keys.contains(key)) keys.push_back(key);
    }
    if (keys.empty()) return false;
    int diffCount = 0;
    for (const QString& key : keys) {
        if (!a.contains(key) || !b.contains(key)) return false;
        const qint64 av = a.value(key).toLongLong();
        const qint64 bv = b.value(key).toLongLong();
        if (av == bv) continue;
        const QList<qint64> values = valueRanks.value(key);
        const int ai = values.indexOf(av);
        const int bi = values.indexOf(bv);
        if (ai < 0 || bi < 0 || absI64(ai - bi) != 1) return false;
        ++diffCount;
    }
    return diffCount > 0;
}

}  // namespace

QString batchAnalysisParamsLabel(const QVariantMap& params) {
    QStringList keys = params.keys();
    std::sort(keys.begin(), keys.end());
    QStringList parts;
    for (const QString& key : keys) parts.push_back(QStringLiteral("%1=%2").arg(key, params.value(key).toString()));
    return parts.join(QStringLiteral(", "));
}

QVariantList batchSummaryCardsFromRows(const QVariantList& rows,
                                       const QVariantList& skippedRows,
                                       int pairCount,
                                       quint64 pointsEvaluated) {
    int positiveRows = 0;
    int okRows = 0;
    int riskRows = 0;
    qint64 pnlSum = 0;
    qint64 worstDrawdown = 0;
    for (const QVariant& value : rows) {
        const QVariantMap row = value.toMap();
        const qint64 pnl = pnlValue(row);
        if (rowOk(row)) ++okRows;
        if (pnl > 0) ++positiveRows;
        if (row.value(QStringLiteral("riskStopped")).toBool() ||
            row.value(QStringLiteral("risk_stopped")).toBool() ||
            row.value(QStringLiteral("liquidated")).toBool()) {
            ++riskRows;
        }
        pnlSum += pnl;
        worstDrawdown = std::max(worstDrawdown, absI64(drawdownValue(row)));
    }
    const int rowCount = rows.size();
    const int positivePct = rowCount <= 0 ? 0 : (positiveRows * 100) / rowCount;
    const int okPct = rowCount <= 0 ? 0 : (okRows * 100) / rowCount;
    QVariantList out;
    out.push_back(card(QStringLiteral("pairs"), QStringLiteral("Pairs"), QString::number(pairCount),
                       QStringLiteral("futures/futures same-symbol pairs")));
    out.push_back(card(QStringLiteral("points"), QStringLiteral("Points"), QString::number(static_cast<qulonglong>(pointsEvaluated)),
                       QStringLiteral("sweep points evaluated")));
    out.push_back(card(QStringLiteral("positive"), QStringLiteral("Positive"), QStringLiteral("%1%").arg(positivePct),
                       QStringLiteral("%1/%2 profitable rows").arg(positiveRows).arg(rowCount),
                       positivePct >= 50 ? QStringLiteral("good") : QStringLiteral("bad")));
    out.push_back(card(QStringLiteral("ok"), QStringLiteral("Clean"), QStringLiteral("%1%").arg(okPct),
                       QStringLiteral("fills > 0, no risk stop/liquidation"),
                       okPct >= 50 ? QStringLiteral("good") : QStringLiteral("bad")));
    out.push_back(card(QStringLiteral("pnl"), QStringLiteral("Avg PnL"), e8Text(rowCount <= 0 ? 0 : pnlSum / rowCount),
                       QStringLiteral("worst dd %1").arg(e8Text(worstDrawdown)),
                       pnlSum >= 0 ? QStringLiteral("good") : QStringLiteral("bad")));
    out.push_back(card(QStringLiteral("risk"), QStringLiteral("Risk"), QString::number(riskRows),
                       QStringLiteral("%1 skipped").arg(skippedRows.size()),
                       riskRows == 0 ? QStringLiteral("good") : QStringLiteral("bad")));
    return out;
}

QVariantList batchPairMatrixColumnsFromRows(const QVariantList& rows) {
    QSet<QString> seen;
    QStringList exchanges;
    for (const QVariant& value : rows) {
        const QVariantMap row = value.toMap();
        for (const QString& key : {QStringLiteral("firstExchange"), QStringLiteral("secondExchange")}) {
            const QString exchange = row.value(key).toString().trimmed().toLower();
            if (exchange.isEmpty() || seen.contains(exchange)) continue;
            seen.insert(exchange);
            exchanges.push_back(exchange);
        }
    }
    std::sort(exchanges.begin(), exchanges.end());
    QVariantList out;
    for (const QString& exchange : exchanges) {
        QVariantMap row;
        row.insert(QStringLiteral("id"), exchange);
        row.insert(QStringLiteral("label"), exchange);
        out.push_back(row);
    }
    return out;
}

QVariantList batchPairMatrixCellsFromRows(const QVariantList& rows) {
    QHash<QString, Aggregate> aggregates;
    for (const QVariant& value : rows) {
        const QVariantMap row = value.toMap();
        QString a = row.value(QStringLiteral("firstExchange")).toString().trimmed().toLower();
        QString b = row.value(QStringLiteral("secondExchange")).toString().trimmed().toLower();
        if (a.isEmpty() || b.isEmpty() || a == b) continue;
        if (b < a) std::swap(a, b);
        const QString key = a + QLatin1Char('|') + b;
        Aggregate& agg = aggregates[key];
        if (agg.key.isEmpty()) {
            agg.key = key;
            agg.label = QStringLiteral("%1/%2").arg(a, b);
        }
        addAggregate(agg, row);
    }

    QVariantList out;
    for (auto it = aggregates.constBegin(); it != aggregates.constEnd(); ++it) {
        const QVariantMap agg = aggregateMap(it.value());
        const QStringList parts = it.key().split(QLatin1Char('|'));
        if (parts.size() != 2) continue;
        for (int direction = 0; direction < 2; ++direction) {
            QVariantMap cell = agg;
            const QString rowExchange = direction == 0 ? parts[0] : parts[1];
            const QString columnExchange = direction == 0 ? parts[1] : parts[0];
            cell.insert(QStringLiteral("rowExchange"), rowExchange);
            cell.insert(QStringLiteral("columnExchange"), columnExchange);
            cell.insert(QStringLiteral("cellKey"), rowExchange + QLatin1Char('|') + columnExchange);
            out.push_back(cell);
        }
    }
    std::sort(out.begin(), out.end(), [](const QVariant& lhs, const QVariant& rhs) {
        const QVariantMap a = lhs.toMap();
        const QVariantMap b = rhs.toMap();
        if (a.value(QStringLiteral("rowExchange")).toString() != b.value(QStringLiteral("rowExchange")).toString()) {
            return a.value(QStringLiteral("rowExchange")).toString() < b.value(QStringLiteral("rowExchange")).toString();
        }
        return a.value(QStringLiteral("columnExchange")).toString() < b.value(QStringLiteral("columnExchange")).toString();
    });
    return out;
}

QVariantList batchTimeRowsFromCurves(const QVariantList& curves) {
    QVariantList out;
    for (const QVariant& value : curves) {
        const QVariantMap row = value.toMap();
        const QVariantList curve = curveValues(row);
        if (curve.empty()) continue;
        qint64 chunks[4]{0, 0, 0, 0};
        const qsizetype curveSize = curve.size();
        if (curveSize == 1) {
            chunks[0] = curve.front().toLongLong();
        } else {
            for (qsizetype i = 1; i < curveSize; ++i) {
                const qsizetype chunk = std::min<qsizetype>(3, ((i - 1) * 4) / std::max<qsizetype>(1, curveSize - 1));
                chunks[chunk] += curve.at(i).toLongLong() - curve.at(i - 1).toLongLong();
            }
        }
        int positiveChunks = 0;
        qint64 worstChunk = chunks[0];
        qint64 bestChunk = chunks[0];
        for (qint64 chunk : chunks) {
            if (chunk > 0) ++positiveChunks;
            worstChunk = std::min(worstChunk, chunk);
            bestChunk = std::max(bestChunk, chunk);
        }
        QVariantMap outRow = row;
        outRow.insert(QStringLiteral("chunk0E8"), chunks[0]);
        outRow.insert(QStringLiteral("chunk1E8"), chunks[1]);
        outRow.insert(QStringLiteral("chunk2E8"), chunks[2]);
        outRow.insert(QStringLiteral("chunk3E8"), chunks[3]);
        outRow.insert(QStringLiteral("positiveChunks"), positiveChunks);
        outRow.insert(QStringLiteral("worstChunkE8"), worstChunk);
        outRow.insert(QStringLiteral("bestChunkE8"), bestChunk);
        outRow.insert(QStringLiteral("stabilityText"), QStringLiteral("%1/4 chunks").arg(positiveChunks));
        outRow.insert(QStringLiteral("timeScore"), static_cast<qint64>(positiveChunks) * 1000000000000ll + pnlValue(row) - absI64(worstChunk));
        if (!outRow.contains(QStringLiteral("paramsLabel"))) outRow.insert(QStringLiteral("paramsLabel"), batchAnalysisParamsLabel(row.value(QStringLiteral("params")).toMap()));
        out.push_back(outRow);
    }
    std::sort(out.begin(), out.end(), [](const QVariant& lhs, const QVariant& rhs) {
        return lhs.toMap().value(QStringLiteral("timeScore")).toLongLong() > rhs.toMap().value(QStringLiteral("timeScore")).toLongLong();
    });
    return out;
}

QVariantList batchPlateauRowsFromRows(const QVariantList& rows) {
    QHash<QString, QVector<QVariantMap>> groups;
    for (const QVariant& value : rows) {
        const QVariantMap row = value.toMap();
        groups[groupKey(row)].push_back(row);
    }

    QVariantList out;
    for (const QVector<QVariantMap>& groupRows : groups) {
        QHash<QString, QList<qint64>> valueRanks;
        for (const QVariantMap& row : groupRows) {
            const QVariantMap params = row.value(QStringLiteral("params")).toMap();
            for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
                QList<qint64>& values = valueRanks[it.key()];
                const qint64 raw = it.value().toLongLong();
                if (!values.contains(raw)) values.push_back(raw);
            }
        }
        for (auto it = valueRanks.begin(); it != valueRanks.end(); ++it) {
            QList<qint64>& values = it.value();
            std::sort(values.begin(), values.end());
        }

        for (int i = 0; i < groupRows.size(); ++i) {
            const QVariantMap row = groupRows.at(i);
            int neighbors = 0;
            int positiveNeighbors = 0;
            for (int j = 0; j < groupRows.size(); ++j) {
                if (i == j) continue;
                const QVariantMap other = groupRows.at(j);
                if (!adjacentParamRows(row, other, valueRanks)) continue;
                ++neighbors;
                if (rowPositive(other)) ++positiveNeighbors;
            }
            const int neighborPositivePct = neighbors <= 0 ? 0 : (positiveNeighbors * 100) / neighbors;
            const int confidence = std::min(100, neighbors * 25);
            const qint64 plateauScore = static_cast<qint64>(positiveNeighbors) * 1000000000000ll +
                                        static_cast<qint64>(neighborPositivePct) * 10000000000ll +
                                        pnlValue(row) - absI64(drawdownValue(row));
            QVariantMap outRow = row;
            outRow.insert(QStringLiteral("paramsLabel"), batchAnalysisParamsLabel(row.value(QStringLiteral("params")).toMap()));
            outRow.insert(QStringLiteral("neighbors"), neighbors);
            outRow.insert(QStringLiteral("positiveNeighbors"), positiveNeighbors);
            outRow.insert(QStringLiteral("neighborPositivePct"), neighborPositivePct);
            outRow.insert(QStringLiteral("confidence"), confidence);
            outRow.insert(QStringLiteral("plateauScore"), plateauScore);
            out.push_back(outRow);
        }
    }
    std::sort(out.begin(), out.end(), [](const QVariant& lhs, const QVariant& rhs) {
        return lhs.toMap().value(QStringLiteral("plateauScore")).toLongLong() > rhs.toMap().value(QStringLiteral("plateauScore")).toLongLong();
    });
    return out;
}

}  // namespace hftrec::gui
