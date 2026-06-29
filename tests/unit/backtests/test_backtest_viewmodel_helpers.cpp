#include <gtest/gtest.h>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QStringList>
#include <QVariantMap>

#include "gui/backtests/BacktestExecutionConfigHelpers.hpp"
#include "gui/backtests/BacktestSessionHelpers.hpp"
#include "gui/backtests/BacktestStrategyConfigHelpers.hpp"
#include "gui/backtests/BacktestSweepHelpers.hpp"

namespace {

void writeTextFile(const QString& path, const QByteArray& data) {
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(file.write(data), data.size());
}

}  // namespace

TEST(BacktestSessionHelpers, MapsVenueSectionsFromExchangeAndMarket) {
    using hftrec::gui::venueSectionFor;
    using hftrec::gui::isVenueSectionKnown;

    EXPECT_EQ(venueSectionFor(QStringLiteral("binance"), QStringLiteral("spot")), QStringLiteral("binance_spot"));
    EXPECT_EQ(venueSectionFor(QStringLiteral("binance"), QStringLiteral("linear")), QStringLiteral("binance_futures"));
    EXPECT_EQ(venueSectionFor(QStringLiteral("bitget"), QStringLiteral("inverse")), QStringLiteral("bitget_inverse"));
    EXPECT_EQ(venueSectionFor(QStringLiteral("mexc"), QStringLiteral("futures")), QStringLiteral("mexc_futures"));
    EXPECT_EQ(venueSectionFor(QStringLiteral("finam"), QStringLiteral("forts")), QStringLiteral("finam_futures"));
    EXPECT_TRUE(isVenueSectionKnown(QStringLiteral("binance"), QStringLiteral("spot")));
    EXPECT_TRUE(isVenueSectionKnown(QStringLiteral("binance"), QStringLiteral("linear")));
    EXPECT_FALSE(isVenueSectionKnown(QStringLiteral("unknown"), QStringLiteral("unknown")));
    EXPECT_TRUE(venueSectionFor(QStringLiteral("unknown"), QStringLiteral("unknown")).isEmpty());
}

TEST(BacktestSessionSummary, AppendsCompactCaptureHealthWarning) {
    const QString clean = hftrec::gui::appendSessionHealthSummary(
        QStringLiteral("L1 10 | 1BT 0 | 2BT 0"),
        QStringLiteral("clean"),
        QString{});
    EXPECT_EQ(clean, QStringLiteral("L1 10 | 1BT 0 | 2BT 0"));

    const QString degraded = hftrec::gui::appendSessionHealthSummary(
        QStringLiteral("L1 10 | 1BT 0 | 2BT 0"),
        QStringLiteral("clean"),
        QStringLiteral("reference: route status=disconnected stream=mark_price symbol=AGLDUSDT"));
    EXPECT_EQ(degraded, QStringLiteral("L1 10 | 1BT 0 | 2BT 0 | degraded: mark_price disconnected"));

    EXPECT_EQ(hftrec::gui::sessionHealthSummaryLabel(QStringLiteral("corrupt"), QString{}),
              QStringLiteral("corrupt"));
}

TEST(BacktestExecutionConfigHelpers, BuildsRateLimitScheduleFromVenueRow) {
    QVariantMap row;
    row.insert(QStringLiteral("exchange"), QStringLiteral("binance"));
    row.insert(QStringLiteral("market"), QStringLiteral("futures_usdt"));
    row.insert(QStringLiteral("rateLimitOrdersLimit"), QStringLiteral("1200"));
    row.insert(QStringLiteral("rateLimitOrdersIntervalMs"), QStringLiteral("60000"));
    row.insert(QStringLiteral("rateLimitCancelOrdersLimit"), QStringLiteral("300"));
    row.insert(QStringLiteral("rateLimitCancelOrdersIntervalMs"), QStringLiteral("10000"));
    row.insert(QStringLiteral("rateLimitLimitOrderCost"), QStringLiteral("1"));
    row.insert(QStringLiteral("rateLimitMarketOrderCost"), QStringLiteral("2"));
    row.insert(QStringLiteral("rateLimitCancelOrderCost"), QStringLiteral("1"));

    const hft_backtest::BacktestRateLimitSchedule schedule = hftrec::gui::rateLimitScheduleFromVenueRow(row);

    ASSERT_EQ(schedule.exchange, "binance");
    ASSERT_EQ(schedule.market, "futures_usdt");
    ASSERT_EQ(schedule.buckets.size(), 2u);
    EXPECT_EQ(schedule.buckets[0].kind, hft_trader::core::RateLimitBucketKind::Orders);
    EXPECT_EQ(schedule.buckets[0].limit, 1200);
    EXPECT_EQ(schedule.buckets[0].intervalNs, 60'000'000'000ull);
    EXPECT_EQ(schedule.buckets[1].kind, hft_trader::core::RateLimitBucketKind::CancelOrders);

    ASSERT_EQ(schedule.actions.size(), 3u);
    EXPECT_EQ(schedule.actions[0].action, hft_trader::core::RateLimitActionKind::LimitOrder);
    EXPECT_EQ(schedule.actions[0].costs[0].bucket, hft_trader::core::RateLimitBucketKind::Orders);
    EXPECT_EQ(schedule.actions[0].costs[0].cost, 1);
    EXPECT_EQ(schedule.actions[1].action, hft_trader::core::RateLimitActionKind::MarketOrder);
    EXPECT_EQ(schedule.actions[1].costs[0].cost, 2);
    EXPECT_EQ(schedule.actions[2].action, hft_trader::core::RateLimitActionKind::CancelOrder);
    EXPECT_EQ(schedule.actions[2].costs[0].bucket, hft_trader::core::RateLimitBucketKind::CancelOrders);
}

TEST(BacktestExecutionConfigHelpers, BuildsFeeScheduleFromVenueRowWithPresetFallback) {
    QVariantMap row;
    row.insert(QStringLiteral("exchange"), QStringLiteral("binance"));
    row.insert(QStringLiteral("market"), QStringLiteral("futures_usdt"));
    const hft_backtest::BacktestFeeSchedule presetSchedule = hftrec::gui::feeScheduleFromVenueRow(row);

    EXPECT_EQ(presetSchedule.exchange, "binance");
    EXPECT_EQ(presetSchedule.market, "futures_usdt");
    EXPECT_EQ(presetSchedule.makerFeeBpsE8, 200000000);
    EXPECT_EQ(presetSchedule.takerFeeBpsE8, 500000000);

    row.insert(QStringLiteral("makerFeeBps"), QStringLiteral("0"));
    row.insert(QStringLiteral("takerFeeBps"), QStringLiteral("1.23"));

    const hft_backtest::BacktestFeeSchedule schedule = hftrec::gui::feeScheduleFromVenueRow(row);

    EXPECT_EQ(schedule.exchange, "binance");
    EXPECT_EQ(schedule.market, "futures_usdt");
    EXPECT_EQ(schedule.makerFeeBpsE8, 0);
    EXPECT_EQ(schedule.takerFeeBpsE8, 123000000);
}

TEST(BacktestExecutionConfigHelpers, WritesRuntimeRateLimitConfigOnlyForEnabledBuckets) {
    QVariantMap execution;
    execution.insert(QStringLiteral("rateLimitOrdersLimit"), QStringLiteral("100"));
    execution.insert(QStringLiteral("rateLimitOrdersIntervalMs"), QStringLiteral("1000"));
    execution.insert(QStringLiteral("rateLimitLimitOrderCost"), QStringLiteral("3"));
    execution.insert(QStringLiteral("rateLimitMarketOrderCost"), QStringLiteral("4"));
    execution.insert(QStringLiteral("rateLimitCancelOrderCost"), QStringLiteral("5"));

    QString text;
    QTextStream stream(&text);
    hftrec::gui::writeRuntimeRateLimitConfig(stream, execution);

    EXPECT_TRUE(text.contains(QStringLiteral("rate_limit_bucket_orders_limit=100")));
    EXPECT_TRUE(text.contains(QStringLiteral("rate_limit_action_limit_order_orders=3")));
    EXPECT_TRUE(text.contains(QStringLiteral("rate_limit_action_cancel_order_orders=5")));
    EXPECT_FALSE(text.contains(QStringLiteral("rate_limit_bucket_cancel_orders_limit")));
}

TEST(BacktestExecutionConfigHelpers, WritesBacktestRateLimitEnabledFlag) {
    QString enabledText;
    QTextStream enabledStream(&enabledText);
    hftrec::gui::writeBacktestRateLimitConfig(enabledStream, true);
    EXPECT_TRUE(enabledText.contains(QStringLiteral("rate_limits_enabled=true")));
    EXPECT_TRUE(enabledText.contains(QStringLiteral("strict_rate_limits=false")));

    QString strictText;
    QTextStream strictStream(&strictText);
    hftrec::gui::writeBacktestRateLimitConfig(strictStream, true, true);
    EXPECT_TRUE(strictText.contains(QStringLiteral("rate_limits_enabled=true")));
    EXPECT_TRUE(strictText.contains(QStringLiteral("strict_rate_limits=true")));

    QString disabledText;
    QTextStream disabledStream(&disabledText);
    hftrec::gui::writeBacktestRateLimitConfig(disabledStream, false, true);
    EXPECT_TRUE(disabledText.contains(QStringLiteral("rate_limits_enabled=false")));
    EXPECT_TRUE(disabledText.contains(QStringLiteral("strict_rate_limits=false")));
}

TEST(BacktestExecutionConfigHelpers, ExecutionPresetSummaryShowsDisabledRateLimits) {
    const QString summary = hftrec::gui::exchangeExecutionPresetSummary(
        QStringLiteral("binance"), QStringLiteral("futures_usdt"), false);
    EXPECT_TRUE(summary.contains(QStringLiteral("RL off")));
    EXPECT_FALSE(summary.contains(QStringLiteral("Costs ")));
}

TEST(BacktestExecutionConfigHelpers, GuiBacktestsUseInlineExecutionPipeline) {
    EXPECT_EQ(hftrec::gui::guiBacktestExecutionPipeline(),
              hft_backtest::BacktestExecutionPipeline::Inline);
}

TEST(BacktestStrategyConfigHelpers, FiltersTemplateSectionsFromBaseConfig) {
    const QString base = QStringLiteral(
        "symbol=BTCUSDT\n"
        "[sweep]\n"
        "edge_bps.min=1\n"
        "[indicators]\n"
        "profile=default\n");

    const QString filtered = hftrec::gui::filteredBaseConfig(base);

    EXPECT_TRUE(filtered.contains(QStringLiteral("symbol=BTCUSDT")));
    EXPECT_FALSE(filtered.contains(QStringLiteral("edge_bps")));
    EXPECT_FALSE(filtered.contains(QStringLiteral("profile=default")));
}

TEST(BacktestSweepHelpers, ParsesRowsAndLegMetricCurves) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString rowsPath = QDir(dir.path()).absoluteFilePath(QStringLiteral("rows.jsonl"));
    writeTextFile(rowsPath,
                  QByteArrayLiteral(R"json({"point_id":1,"params":{"edge_bps":2},"initial_balance_e8":1000000000,"total_pnl_e8":300000000,"curve_e8":[100000000,300000000],"legs":[{"leg_index":0,"exchange":"binance","symbol":"BTCUSDT","initial_balance_e8":500000000,"total_pnl_e8":100000000,"curve_e8":[0,100000000]}],"status":"ok"})json")
                      + QByteArrayLiteral("\n"));

    const QVariantList rows = hftrec::gui::sweepRowsFromJsonl(rowsPath, QStringLiteral("leg_0_total_pnl_e8"));
    ASSERT_EQ(rows.size(), 1);
    const QVariantMap row = rows.front().toMap();
    EXPECT_EQ(row.value(QStringLiteral("metricKey")).toString(), QStringLiteral("leg_0_total_pnl_e8"));
    EXPECT_EQ(row.value(QStringLiteral("metricRaw")).toLongLong(), 100000000);
    EXPECT_EQ(row.value(QStringLiteral("metricLabel")).toString(), QStringLiteral("Leg 1 binance BTCUSDT"));

    QStringList paramKeys;
    hftrec::gui::appendSweepParamKeysFromRows(rows, paramKeys);
    EXPECT_EQ(paramKeys, QStringList({QStringLiteral("edge_bps")}));

    const QVariantList curves = hftrec::gui::sweepCurvesFromJsonl(rowsPath);
    ASSERT_EQ(curves.size(), 1);
    EXPECT_EQ(curves.front().toMap().value(QStringLiteral("curve")).toList().size(), 2);
}
