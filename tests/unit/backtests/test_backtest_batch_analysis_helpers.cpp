#include <gtest/gtest.h>

#include <QVariantList>
#include <QVariantMap>

#include <utility>

#include "gui/backtests/BacktestBatchAnalysisHelpers.hpp"

namespace {

QVariantMap batchRow(QString symbol,
                     QString pair,
                     QString firstExchange,
                     QString secondExchange,
                     qint64 pnl,
                     qint64 drawdown,
                     qint64 fills,
                     QVariantMap params,
                     bool riskStopped = false,
                     bool liquidated = false) {
    QVariantMap out;
    out.insert(QStringLiteral("symbol"), std::move(symbol));
    out.insert(QStringLiteral("exchangePair"), std::move(pair));
    out.insert(QStringLiteral("firstExchange"), std::move(firstExchange));
    out.insert(QStringLiteral("secondExchange"), std::move(secondExchange));
    out.insert(QStringLiteral("params"), std::move(params));
    out.insert(QStringLiteral("status"), QStringLiteral("Ok"));
    out.insert(QStringLiteral("totalPnlE8"), pnl);
    out.insert(QStringLiteral("maxDrawdownE8"), drawdown);
    out.insert(QStringLiteral("fills"), fills);
    out.insert(QStringLiteral("riskStopped"), riskStopped);
    out.insert(QStringLiteral("liquidated"), liquidated);
    return out;
}

QVariantMap params(qint64 edge, qint64 delay) {
    QVariantMap out;
    out.insert(QStringLiteral("edge_bps"), edge);
    out.insert(QStringLiteral("delay_us"), delay);
    return out;
}

QVariantMap curveRow(QString symbol, QString pair, QVariantList curve, QVariantMap pointParams) {
    QVariantMap out;
    out.insert(QStringLiteral("symbol"), std::move(symbol));
    out.insert(QStringLiteral("exchangePair"), std::move(pair));
    out.insert(QStringLiteral("params"), std::move(pointParams));
    out.insert(QStringLiteral("paramsLabel"), hftrec::gui::batchAnalysisParamsLabel(out.value(QStringLiteral("params")).toMap()));
    out.insert(QStringLiteral("totalPnlE8"), curve.empty() ? 0 : curve.back().toLongLong());
    out.insert(QStringLiteral("curve_e8"), std::move(curve));
    return out;
}

}  // namespace

TEST(BacktestBatchAnalysisHelpers, BuildsSummaryCards) {
    QVariantList rows;
    rows.push_back(batchRow(QStringLiteral("BTCUSDT"), QStringLiteral("binance/bybit"), QStringLiteral("binance"), QStringLiteral("bybit"), 200000000, 20000000, 4, params(10, 100)));
    rows.push_back(batchRow(QStringLiteral("ETHUSDT"), QStringLiteral("aster/bybit"), QStringLiteral("aster"), QStringLiteral("bybit"), -50000000, 60000000, 0, params(20, 100), true));
    QVariantList skipped;
    skipped.push_back(QVariantMap{{QStringLiteral("reason"), QStringLiteral("not futures-like")}});

    const QVariantList cards = hftrec::gui::batchSummaryCardsFromRows(rows, skipped, 3, 8);

    ASSERT_GE(cards.size(), 5);
    EXPECT_EQ(cards.front().toMap().value(QStringLiteral("id")).toString(), QStringLiteral("pairs"));
    EXPECT_EQ(cards.front().toMap().value(QStringLiteral("value")).toString(), QStringLiteral("3"));
}

TEST(BacktestBatchAnalysisHelpers, BuildsSymmetricPairMatrix) {
    QVariantList rows;
    rows.push_back(batchRow(QStringLiteral("BTCUSDT"), QStringLiteral("binance/bybit"), QStringLiteral("binance"), QStringLiteral("bybit"), 200000000, 10000000, 4, params(10, 100)));
    rows.push_back(batchRow(QStringLiteral("ETHUSDT"), QStringLiteral("aster/bybit"), QStringLiteral("aster"), QStringLiteral("bybit"), 50000000, 5000000, 2, params(10, 100)));

    const QVariantList columns = hftrec::gui::batchPairMatrixColumnsFromRows(rows);
    const QVariantList cells = hftrec::gui::batchPairMatrixCellsFromRows(rows);

    EXPECT_EQ(columns.size(), 3);
    bool foundReverse = false;
    for (const QVariant& value : cells) {
        const QVariantMap cell = value.toMap();
        if (cell.value(QStringLiteral("rowExchange")).toString() == QStringLiteral("bybit") &&
            cell.value(QStringLiteral("columnExchange")).toString() == QStringLiteral("binance")) {
            foundReverse = true;
            EXPECT_EQ(cell.value(QStringLiteral("rows")).toInt(), 1);
        }
    }
    EXPECT_TRUE(foundReverse);
}

TEST(BacktestBatchAnalysisHelpers, SplitsCurvesIntoProgressChunks) {
    QVariantList curves;
    curves.push_back(curveRow(QStringLiteral("BTCUSDT"), QStringLiteral("binance/bybit"),
                              QVariantList{0, 100000000, 150000000, 120000000, 220000000},
                              params(10, 100)));

    const QVariantList timeRows = hftrec::gui::batchTimeRowsFromCurves(curves);

    ASSERT_EQ(timeRows.size(), 1);
    const QVariantMap row = timeRows.front().toMap();
    EXPECT_EQ(row.value(QStringLiteral("positiveChunks")).toInt(), 3);
    EXPECT_LT(row.value(QStringLiteral("worstChunkE8")).toLongLong(), 0);
}

TEST(BacktestBatchAnalysisHelpers, ScoresParameterPlateausAboveIsolatedSpikes) {
    QVariantList rows;
    rows.push_back(batchRow(QStringLiteral("BTCUSDT"), QStringLiteral("binance/bybit"), QStringLiteral("binance"), QStringLiteral("bybit"), 100000000, 10000000, 4, params(10, 100)));
    rows.push_back(batchRow(QStringLiteral("BTCUSDT"), QStringLiteral("binance/bybit"), QStringLiteral("binance"), QStringLiteral("bybit"), 90000000, 10000000, 4, params(20, 100)));
    rows.push_back(batchRow(QStringLiteral("BTCUSDT"), QStringLiteral("binance/bybit"), QStringLiteral("binance"), QStringLiteral("bybit"), 80000000, 10000000, 4, params(10, 200)));
    rows.push_back(batchRow(QStringLiteral("BTCUSDT"), QStringLiteral("binance/bybit"), QStringLiteral("binance"), QStringLiteral("bybit"), 500000000, 400000000, 1, params(100, 1000)));

    const QVariantList plateauRows = hftrec::gui::batchPlateauRowsFromRows(rows);

    ASSERT_FALSE(plateauRows.empty());
    EXPECT_EQ(plateauRows.front().toMap().value(QStringLiteral("paramsLabel")).toString(), QStringLiteral("delay_us=100, edge_bps=10"));
}
