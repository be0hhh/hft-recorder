#include <gtest/gtest.h>

#include <QVariantMap>
#include <QVector>

#include <utility>

#include "gui/backtests/BacktestBatchSweepHelpers.hpp"

namespace {

hftrec::gui::BatchSweepSessionInfo session(QString id, QString exchange, QString market, QString symbol) {
    hftrec::gui::BatchSweepSessionInfo out;
    out.path = QStringLiteral("/tmp/%1").arg(id);
    out.sessionId = std::move(id);
    out.exchange = std::move(exchange);
    out.market = std::move(market);
    out.symbol = std::move(symbol);
    out.canonicalSymbol = hftrec::gui::batchCanonicalSymbol(out.symbol);
    out.venue = out.exchange + QLatin1Char('_') + out.market;
    return out;
}

QVariantMap row(QString symbol,
                QString pair,
                qint64 pnl,
                qint64 drawdown,
                qint64 fills,
                bool riskStopped = false,
                bool liquidated = false) {
    QVariantMap out;
    out.insert(QStringLiteral("symbol"), std::move(symbol));
    out.insert(QStringLiteral("exchangePair"), std::move(pair));
    QVariantMap params;
    params.insert(QStringLiteral("entry_edge_bps"), pnl > 0 ? 10 : 20);
    out.insert(QStringLiteral("params"), params);
    out.insert(QStringLiteral("status"), QStringLiteral("Ok"));
    out.insert(QStringLiteral("totalPnlE8"), pnl);
    out.insert(QStringLiteral("maxDrawdownE8"), drawdown);
    out.insert(QStringLiteral("fills"), fills);
    out.insert(QStringLiteral("riskStopped"), riskStopped);
    out.insert(QStringLiteral("liquidated"), liquidated);
    return out;
}

}  // namespace

TEST(BacktestBatchSweepHelpers, BuildsSameSymbolFuturesPairsOnly) {
    const QVector<hftrec::gui::BatchSweepSessionInfo> sessions{
        session(QStringLiteral("binance_btc_spot"), QStringLiteral("binance"), QStringLiteral("spot"), QStringLiteral("BTCUSDT")),
        session(QStringLiteral("binance_btc_fut"), QStringLiteral("binance"), QStringLiteral("futures"), QStringLiteral("BTCUSDT")),
        session(QStringLiteral("bybit_btc_swap"), QStringLiteral("bybit"), QStringLiteral("swap"), QStringLiteral("BTC-USDT-SWAP")),
        session(QStringLiteral("okx_eth_swap"), QStringLiteral("okx"), QStringLiteral("swap"), QStringLiteral("ETHUSDT")),
    };
    QVariantList skipped;

    const QVector<hftrec::gui::BatchSweepPair> pairs = hftrec::gui::buildBatchSweepPairs(sessions, 64, true, &skipped);

    ASSERT_EQ(pairs.size(), 1);
    EXPECT_EQ(pairs.front().first.exchange, QStringLiteral("binance"));
    EXPECT_EQ(pairs.front().second.exchange, QStringLiteral("bybit"));
    EXPECT_EQ(pairs.front().first.symbol, QStringLiteral("BTCUSDT"));
    EXPECT_EQ(pairs.front().second.symbol, QStringLiteral("BTC-USDT-SWAP"));
    ASSERT_EQ(skipped.size(), 1);
    EXPECT_EQ(skipped.front().toMap().value(QStringLiteral("reason")).toString(), QStringLiteral("not futures"));
}

TEST(BacktestBatchSweepHelpers, CanonicalizesCommonPerpSymbolFormats) {
    EXPECT_EQ(hftrec::gui::batchCanonicalSymbol(QStringLiteral("BTC-USDT-SWAP")), QStringLiteral("BTCUSDT"));
    EXPECT_EQ(hftrec::gui::batchCanonicalSymbol(QStringLiteral("btc_usdt_perp")), QStringLiteral("BTCUSDT"));
    EXPECT_EQ(hftrec::gui::batchCanonicalSymbol(QStringLiteral("1000PEPEUSDT")), QStringLiteral("1000PEPEUSDT"));
}

TEST(BacktestBatchSweepHelpers, HonorsPairBudget) {
    const QVector<hftrec::gui::BatchSweepSessionInfo> sessions{
        session(QStringLiteral("a"), QStringLiteral("aster"), QStringLiteral("futures"), QStringLiteral("BTCUSDT")),
        session(QStringLiteral("b"), QStringLiteral("binance"), QStringLiteral("futures"), QStringLiteral("BTCUSDT")),
        session(QStringLiteral("c"), QStringLiteral("bybit"), QStringLiteral("linear"), QStringLiteral("BTCUSDT")),
    };

    const QVector<hftrec::gui::BatchSweepPair> pairs = hftrec::gui::buildBatchSweepPairs(sessions, 2, true, nullptr);

    EXPECT_EQ(pairs.size(), 2);
}

TEST(BacktestBatchSweepHelpers, StableAndProfitLeaderboardsUseDifferentPriorities) {
    QVariantList rows;
    rows.push_back(row(QStringLiteral("BTCUSDT"), QStringLiteral("binance/bybit"), 100000000, 10000000, 4));
    rows.push_back(row(QStringLiteral("ETHUSDT"), QStringLiteral("aster/bybit"), 400000000, 5000000, 3, true));
    rows.push_back(row(QStringLiteral("SOLUSDT"), QStringLiteral("aster/binance"), 200000000, 5000000, 5));

    const QVariantList stable = hftrec::gui::batchStableRowsFromRows(rows);
    const QVariantList profit = hftrec::gui::batchProfitRowsFromRows(rows);

    ASSERT_EQ(stable.size(), 3);
    ASSERT_EQ(profit.size(), 3);
    EXPECT_EQ(stable.front().toMap().value(QStringLiteral("symbol")).toString(), QStringLiteral("SOLUSDT"));
    EXPECT_EQ(profit.front().toMap().value(QStringLiteral("symbol")).toString(), QStringLiteral("ETHUSDT"));
}

TEST(BacktestBatchSweepHelpers, AggregatesBySymbolPairAndParams) {
    QVariantList rows;
    rows.push_back(row(QStringLiteral("BTCUSDT"), QStringLiteral("binance/bybit"), 100000000, 10000000, 4));
    rows.push_back(row(QStringLiteral("BTCUSDT"), QStringLiteral("binance/bybit"), 200000000, 20000000, 6));
    rows.push_back(row(QStringLiteral("ETHUSDT"), QStringLiteral("aster/bybit"), -10000000, 5000000, 2));

    const QVariantList symbols = hftrec::gui::batchSymbolRowsFromRows(rows);
    const QVariantList pairs = hftrec::gui::batchPairRowsFromRows(rows);
    const QVariantList params = hftrec::gui::batchParamRowsFromRows(rows);

    ASSERT_FALSE(symbols.empty());
    ASSERT_FALSE(pairs.empty());
    ASSERT_FALSE(params.empty());
    EXPECT_EQ(symbols.front().toMap().value(QStringLiteral("label")).toString(), QStringLiteral("BTCUSDT"));
    EXPECT_EQ(pairs.front().toMap().value(QStringLiteral("label")).toString(), QStringLiteral("binance/bybit"));
}
