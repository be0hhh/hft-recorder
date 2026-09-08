#include <gtest/gtest.h>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>
#include <QStringList>
#include <QVariantMap>

#include "../../../src/Gui/src/Backtests/BacktestExecutionConfigHelpers.hpp"
#include "../../../src/Gui/src/Backtests/BacktestResultHelpers.hpp"
#include "../../../src/Gui/src/Backtests/BacktestSessionHelpers.hpp"
#include "../../../src/Gui/src/Backtests/BacktestSessionSummary.hpp"
#include "../../../src/Gui/src/Backtests/BacktestStrategyConfigHelpers.hpp"
#include "../../../src/Gui/src/Backtests/BacktestSweepHelpers.hpp"

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

TEST(BacktestSessionHelpers, LoadsOneImmutableManifestSnapshotWithExplicitStatus) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());

    const hftrec::gui::SessionManifestSnapshot missing =
        hftrec::gui::loadSessionManifestSnapshot(QDir(dir.path()).absoluteFilePath(QStringLiteral("missing")));
    EXPECT_EQ(missing.status(), hftrec::gui::SessionManifestStatus::Missing);
    EXPECT_FALSE(missing.ready());
    EXPECT_FALSE(missing.error().isEmpty());

    const QString malformedPath = QDir(dir.path()).absoluteFilePath(QStringLiteral("malformed"));
    ASSERT_TRUE(QDir().mkpath(malformedPath));
    writeTextFile(QDir(malformedPath).absoluteFilePath(QStringLiteral("manifest.json")),
                  QByteArrayLiteral("{"));
    const hftrec::gui::SessionManifestSnapshot malformed =
        hftrec::gui::loadSessionManifestSnapshot(malformedPath);
    EXPECT_EQ(malformed.status(), hftrec::gui::SessionManifestStatus::Malformed);
    EXPECT_FALSE(malformed.ready());
    EXPECT_FALSE(malformed.error().isEmpty());

    const QString readyPath = QDir(dir.path()).absoluteFilePath(QStringLiteral("ready_BTCUSDT"));
    ASSERT_TRUE(QDir().mkpath(readyPath));
    writeTextFile(QDir(readyPath).absoluteFilePath(QStringLiteral("manifest.json")),
                  QByteArrayLiteral(R"json({
                    "exchange":"binance",
                    "market":"linear",
                    "symbols":["BTCUSDT"],
                    "channels":{
                      "bookticker":{"declared_event_count":42},
                      "trades":{"declared_event_count":7}
                    }
                  })json"));
    const hftrec::gui::SessionManifestSnapshot ready =
        hftrec::gui::loadSessionManifestSnapshot(readyPath);
    ASSERT_TRUE(ready.ready());
    EXPECT_EQ(ready.status(), hftrec::gui::SessionManifestStatus::Ready);
    EXPECT_GT(ready.size(), 0);
    EXPECT_GT(ready.lastModifiedMs(), 0);
    EXPECT_EQ(hftrec::gui::venueSectionForSession(ready), QStringLiteral("binance_futures"));
    EXPECT_EQ(hftrec::gui::symbolForSessionPath(ready), QStringLiteral("BTC_USDT"));
    EXPECT_EQ(hftrec::gui::manifestChannelDeclaredCount(ready, QStringLiteral("bookticker")), 42u);
    EXPECT_EQ(hftrec::gui::manifestChannelDeclaredCount(ready, QStringLiteral("trades")), 7u);
    QString compatibilityError;
    EXPECT_FALSE(hftrec::gui::sessionSupportsCurrentBacktestContract(ready, &compatibilityError));
    EXPECT_TRUE(compatibilityError.contains(QStringLiteral("view-only")));

    const QString currentPath = QDir(dir.path()).absoluteFilePath(QStringLiteral("current_BTC_USDT"));
    ASSERT_TRUE(QDir().mkpath(currentPath));
    writeTextFile(QDir(currentPath).absoluteFilePath(QStringLiteral("manifest.json")),
                  QByteArrayLiteral(R"json({
                    "manifest_schema_version":3,
                    "corpus_schema_version":3,
                    "capture_contract_version":"hftrec.captured_arrival_rows_json.v4",
                    "session_status":"complete",
                    "replay":{"structurally_loadable":true},
                    "integrity":{"session_health":"clean","exact_replay_eligible":true},
                    "channels":{
                      "trades":{"declared_event_count":1},
                      "liquidations":{"declared_event_count":0},
                      "bookticker":{"declared_event_count":0},
                      "depth":{"declared_event_count":0},
                      "candles":{"declared_event_count":0},
                      "candles2":{"declared_event_count":0},
                      "mark_price":{"declared_event_count":0},
                      "index_price":{"declared_event_count":0},
                      "funding":{"declared_event_count":0},
                      "price_limit":{"declared_event_count":0}
                    },
                    "arrival_clock":{
                      "boundary":"hft-parser.application-frame-ready",
                      "realtime_clock":"CLOCK_REALTIME",
                      "monotonic_clock":"CLOCK_MONOTONIC",
                      "captured_rows":1,
                      "historical_rows":0,
                      "unavailable_rows":0,
                      "realtime_regressions":0,
                      "monotonic_non_increasing":0,
                      "exchange_ahead_of_receive":0,
                      "exchange_timestamp_missing":0,
                      "first_receive_realtime_ns":1900000000000000000,
                      "last_receive_realtime_ns":1900000000000000000,
                      "first_receive_monotonic_ns":900000000000000000,
                      "last_receive_monotonic_ns":900000000000000000
                    }
                  })json"));
    const hftrec::gui::SessionManifestSnapshot current =
        hftrec::gui::loadSessionManifestSnapshot(currentPath);
    ASSERT_TRUE(current.ready());
    EXPECT_TRUE(hftrec::gui::sessionSupportsCurrentBacktestContract(current, &compatibilityError));
    EXPECT_TRUE(compatibilityError.isEmpty());
}

TEST(BacktestResultHelpers, UsesAuthoritativeTotalAndFailVisibleMissingTotal) {
    const QJsonObject authoritative{
        {QStringLiteral("type"), QStringLiteral("run.result.v3")},
        {QStringLiteral("schema_version"), 2},
        {QStringLiteral("summary"), QJsonObject{
            {QStringLiteral("initial_balance_e8"), 1000},
            {QStringLiteral("total_pnl_e8"), 150},
            {QStringLiteral("net_realized_pnl_e8"), 100},
            {QStringLiteral("realized_pnl_e8"), 90},
        }},
    };
    const hftrec::gui::BacktestRunSummary decoded =
        hftrec::gui::decodeBacktestRunSummary(authoritative);
    ASSERT_TRUE(decoded.ready());
    EXPECT_TRUE(decoded.canonicalRunResult);
    EXPECT_EQ(decoded.initialBalanceE8, 1000);
    EXPECT_EQ(decoded.totalPnlE8, 150);

    const QJsonObject missingTotal{
        {QStringLiteral("type"), QStringLiteral("run.result.v3")},
        {QStringLiteral("summary"), QJsonObject{
            {QStringLiteral("net_realized_pnl_e8"), 100},
            {QStringLiteral("realized_pnl_e8"), 90},
        }},
    };
    const hftrec::gui::BacktestRunSummary invalid =
        hftrec::gui::decodeBacktestRunSummary(missingTotal);
    EXPECT_FALSE(invalid.ready());
    EXPECT_EQ(invalid.status, hftrec::gui::BacktestRunSummaryStatus::MissingTotalPnl);
    EXPECT_FALSE(invalid.error.isEmpty());

    const QJsonObject fractionalTotal{
        {QStringLiteral("type"), QStringLiteral("run.result.v3")},
        {QStringLiteral("summary"), QJsonObject{
            {QStringLiteral("total_pnl_e8"), 1.5},
        }},
    };
    const hftrec::gui::BacktestRunSummary fractional =
        hftrec::gui::decodeBacktestRunSummary(fractionalTotal);
    EXPECT_FALSE(fractional.ready());
    EXPECT_EQ(fractional.status, hftrec::gui::BacktestRunSummaryStatus::InvalidTotalPnl);
}

TEST(BacktestResultHelpers, LegacyTotalFallbackOrderIsStable) {
    const auto decodeLegacy = [](const QJsonObject& summary) {
        return hftrec::gui::decodeBacktestRunSummary(QJsonObject{
            {QStringLiteral("type"), QStringLiteral("run.result")},
            {QStringLiteral("summary"), summary},
        });
    };

    const hftrec::gui::BacktestRunSummary total = decodeLegacy(QJsonObject{
        {QStringLiteral("total_pnl_e8"), 300},
        {QStringLiteral("net_realized_pnl_e8"), 200},
        {QStringLiteral("realized_pnl_e8"), 100},
    });
    ASSERT_TRUE(total.ready());
    EXPECT_EQ(total.totalPnlE8, 300);

    const hftrec::gui::BacktestRunSummary net = decodeLegacy(QJsonObject{
        {QStringLiteral("net_realized_pnl_e8"), 200},
        {QStringLiteral("realized_pnl_e8"), 100},
    });
    ASSERT_TRUE(net.ready());
    EXPECT_EQ(net.totalPnlE8, 200);

    const hftrec::gui::BacktestRunSummary realized = decodeLegacy(QJsonObject{
        {QStringLiteral("realized_pnl_e8"), 100},
    });
    ASSERT_TRUE(realized.ready());
    EXPECT_EQ(realized.totalPnlE8, 100);
}

TEST(BacktestResultHelpers, PortfolioSynthesisUsesBaselineBeforeLegFirstTimestamp) {
    const QVariantList firstLeg{
        QVariantMap{{QStringLiteral("tsNs"), 100ll},
                    {QStringLiteral("totalPnlE8"), 0ll},
                    {QStringLiteral("walletBalanceE8"), 1000ll}},
        QVariantMap{{QStringLiteral("tsNs"), 200ll},
                    {QStringLiteral("totalPnlE8"), 10ll},
                    {QStringLiteral("walletBalanceE8"), 1010ll}},
    };
    const QVariantList delayedLeg{
        QVariantMap{{QStringLiteral("tsNs"), 200ll},
                    {QStringLiteral("totalPnlE8"), 20ll},
                    {QStringLiteral("walletBalanceE8"), 2020ll}},
    };
    qint64 minPnl = 0;
    qint64 maxPnl = 0;
    const QVariantList portfolio = hftrec::gui::synthesizePortfolioEquityPoints(
        std::vector<QVariantList>{firstLeg, delayedLeg},
        std::vector<qint64>{1000, 2000},
        minPnl,
        maxPnl);

    ASSERT_EQ(portfolio.size(), 2);
    const QVariantMap beforeDelayedLeg = portfolio.at(0).toMap();
    EXPECT_EQ(beforeDelayedLeg.value(QStringLiteral("tsNs")).toLongLong(), 100ll);
    EXPECT_EQ(beforeDelayedLeg.value(QStringLiteral("totalPnlE8")).toLongLong(), 0ll);
    EXPECT_EQ(beforeDelayedLeg.value(QStringLiteral("walletBalanceE8")).toLongLong(), 3000ll);
    const QVariantMap bothLegs = portfolio.at(1).toMap();
    EXPECT_EQ(bothLegs.value(QStringLiteral("totalPnlE8")).toLongLong(), 30ll);
    EXPECT_EQ(bothLegs.value(QStringLiteral("walletBalanceE8")).toLongLong(), 3030ll);
}

TEST(BacktestSessionSummary, AppendsCompactCaptureHealthWarning) {
    const QString clean = hftrec::gui::appendSessionHealthSummary(
        QStringLiteral("L1 10 | BT 0"),
        QStringLiteral("clean"),
        QString{});
    EXPECT_EQ(clean, QStringLiteral("L1 10 | BT 0"));

    const QString degraded = hftrec::gui::appendSessionHealthSummary(
        QStringLiteral("L1 10 | BT 0"),
        QStringLiteral("clean"),
        QStringLiteral("reference: route status=disconnected stream=mark_price symbol=AGLD_USDT"));
    EXPECT_EQ(degraded, QStringLiteral("L1 10 | BT 0 | degraded: mark_price disconnected"));

    EXPECT_EQ(hftrec::gui::sessionHealthSummaryLabel(QStringLiteral("corrupt"), QString{}),
              QStringLiteral("corrupt"));
}

TEST(BacktestSessionSummary, MatchesThreeLegResultsOnlyInSelectedOrder) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString manifestPath = QDir(dir.path()).absoluteFilePath(QStringLiteral("manifest.json"));
    writeTextFile(manifestPath, QByteArrayLiteral(R"json({
      "type":"run.result.v3",
      "run_id":"multi-leg",
      "legs":[
        {"leg_index":0,"session_path":"/recordings/binance_btc"},
        {"leg_index":1,"session_path":"/recordings/bybit_btc"},
        {"leg_index":2,"session_path":"/recordings/okx_btc"}
      ]
    })json"));

    EXPECT_TRUE(hftrec::gui::backtestManifestMatchesLegs(
        manifestPath,
        QStringList{QStringLiteral("binance_btc"), QStringLiteral("bybit_btc"), QStringLiteral("okx_btc")}));
    EXPECT_FALSE(hftrec::gui::backtestManifestMatchesLegs(
        manifestPath,
        QStringList{QStringLiteral("bybit_btc"), QStringLiteral("binance_btc"), QStringLiteral("okx_btc")}));
    EXPECT_FALSE(hftrec::gui::backtestManifestMatchesLegs(
        manifestPath,
        QStringList{QStringLiteral("binance_btc"), QStringLiteral("bybit_btc")}));
}

TEST(BacktestSessionSummary, KeepsTwoLegResultsOrderIndependent) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString manifestPath = QDir(dir.path()).absoluteFilePath(QStringLiteral("manifest.json"));
    writeTextFile(manifestPath, QByteArrayLiteral(R"json({
      "type":"run.result.v3",
      "run_id":"pair",
      "legs":[
        {"leg_index":0,"session_path":"/recordings/binance_eth"},
        {"leg_index":1,"session_path":"/recordings/bybit_eth"}
      ]
    })json"));

    EXPECT_TRUE(hftrec::gui::backtestManifestMatchesLegs(
        manifestPath,
        QStringList{QStringLiteral("bybit_eth"), QStringLiteral("binance_eth")}));
}

TEST(BacktestSessionSummary, FormatsTotalBacktestCountForManyLegs) {
    hftrec::gui::BacktestLegCounts counts;
    counts.firstLeg = 1;
    counts.secondLeg = 1;
    counts.total = 3;

    const QString summary = hftrec::gui::sessionBacktestSummaryText(42, counts, 0);

    EXPECT_EQ(summary, QStringLiteral("L1 42 | BT 3"));
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
    EXPECT_EQ(schedule.buckets[0].kind, trading_core::RateLimitBucketKind::Orders);
    EXPECT_EQ(schedule.buckets[0].limit, 1200);
    EXPECT_EQ(schedule.buckets[0].intervalNs, 60'000'000'000ull);
    EXPECT_EQ(schedule.buckets[1].kind, trading_core::RateLimitBucketKind::CancelOrders);

    ASSERT_EQ(schedule.actions.size(), 3u);
    EXPECT_EQ(schedule.actions[0].action, trading_core::RateLimitActionKind::LimitOrder);
    EXPECT_EQ(schedule.actions[0].costs[0].bucket, trading_core::RateLimitBucketKind::Orders);
    EXPECT_EQ(schedule.actions[0].costs[0].cost, 1);
    EXPECT_EQ(schedule.actions[1].action, trading_core::RateLimitActionKind::MarketOrder);
    EXPECT_EQ(schedule.actions[1].costs[0].cost, 2);
    EXPECT_EQ(schedule.actions[2].action, trading_core::RateLimitActionKind::CancelOrder);
    EXPECT_EQ(schedule.actions[2].costs[0].bucket, trading_core::RateLimitBucketKind::CancelOrders);
}

TEST(BacktestExecutionConfigHelpers, AppliesTypedExecutionPolicyAndGatesStrictRejects) {
    hftrec::gui::BacktestExecutionPolicy policy;
    policy.latencySeed = 17;
    policy.marketOrderLatency = {21, 22};
    policy.limitOrderLatency = {31, 32};
    policy.cancelOrderLatency = {41, 42};
    policy.userDataLatency = {51, 52};
    policy.orderLatencyUs = 61;
    policy.cancelLatencyUs = 62;
    policy.initialBalanceE8 = 70;
    policy.rateLimitsEnabled = false;
    policy.strictRateLimitsEnabled = true;
    policy.legInitialBalancesE8 = {71, 72};
    policy.feeSchedules.push_back(hft_backtest::BacktestFeeSchedule{});
    policy.latencySchedules.push_back(hft_backtest::BacktestLatencySchedule{});
    policy.rateLimitSchedules.push_back(hft_backtest::BacktestRateLimitSchedule{});

    hft_backtest::BacktestRunRequest request;
    hftrec::gui::applyBacktestExecutionPolicy(request, policy);

    EXPECT_EQ(request.latencySeed, 17u);
    EXPECT_EQ(request.marketOrderLatency.baseUs, 21u);
    EXPECT_EQ(request.limitOrderLatency.baseUs, 31u);
    EXPECT_EQ(request.cancelOrderLatency.baseUs, 41u);
    EXPECT_EQ(request.userDataLatency.baseUs, 51u);
    EXPECT_EQ(request.orderLatencyUs, 61u);
    EXPECT_EQ(request.cancelLatencyUs, 62u);
    EXPECT_EQ(request.initialBalanceE8, 70);
    EXPECT_FALSE(request.rateLimitsEnabled);
    EXPECT_FALSE(request.strictRateLimitRejects);
    EXPECT_EQ(request.legInitialBalancesE8, policy.legInitialBalancesE8);
    EXPECT_EQ(request.feeSchedules.size(), 1u);
    EXPECT_EQ(request.latencySchedules.size(), 1u);
    EXPECT_EQ(request.rateLimitSchedules.size(), 1u);

    policy.rateLimitsEnabled = true;
    hftrec::gui::applyBacktestExecutionPolicy(request, policy);
    EXPECT_TRUE(request.rateLimitsEnabled);
    EXPECT_TRUE(request.strictRateLimitRejects);
}

TEST(BacktestExecutionConfigHelpers, ExecutionLatencySweepOmitsOverridingVenueLatencySchedules) {
    EXPECT_TRUE(hftrec::gui::usePerVenueLatencySchedules(false));
    EXPECT_FALSE(hftrec::gui::usePerVenueLatencySchedules(true));
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

TEST(BacktestStrategyConfigHelpers, FiltersTemplateSectionsFromBaseConfig) {
    const QString base = QStringLiteral(
        "symbol=BTC_USDT\n"
        "[sweep]\n"
        "edge_bps.min=1\n"
        "[indicators]\n"
        "profile=default\n");

    const QString filtered = hftrec::gui::filteredBaseConfig(base);

    EXPECT_TRUE(filtered.contains(QStringLiteral("symbol=BTC_USDT")));
    EXPECT_FALSE(filtered.contains(QStringLiteral("edge_bps")));
    EXPECT_FALSE(filtered.contains(QStringLiteral("profile=default")));
}

TEST(BacktestSweepHelpers, ParsesRowsAndLegMetricCurves) {
    QTemporaryDir dir;
    ASSERT_TRUE(dir.isValid());
    const QString rowsPath = QDir(dir.path()).absoluteFilePath(QStringLiteral("rows.jsonl"));
    writeTextFile(rowsPath,
                  QByteArrayLiteral(R"json({"point_id":1,"params":{"edge_bps":2},"initial_balance_e8":1000000000,"total_pnl_e8":300000000,"curve_e8":[100000000,300000000],"legs":[{"leg_index":0,"exchange":"binance","symbol":"BTC_USDT","initial_balance_e8":500000000,"total_pnl_e8":100000000,"curve_e8":[0,100000000]}],"status":"ok"})json")
                      + QByteArrayLiteral("\n"));

    const QVariantList rows = hftrec::gui::sweepRowsFromJsonl(rowsPath, QStringLiteral("leg_0_total_pnl_e8"));
    ASSERT_EQ(rows.size(), 1);
    const QVariantMap row = rows.front().toMap();
    EXPECT_EQ(row.value(QStringLiteral("metricKey")).toString(), QStringLiteral("leg_0_total_pnl_e8"));
    EXPECT_EQ(row.value(QStringLiteral("metricRaw")).toLongLong(), 100000000);
    EXPECT_EQ(row.value(QStringLiteral("metricLabel")).toString(), QStringLiteral("Leg 1 binance BTC_USDT"));

    QStringList paramKeys;
    hftrec::gui::appendSweepParamKeysFromRows(rows, paramKeys);
    EXPECT_EQ(paramKeys, QStringList({QStringLiteral("edge_bps")}));

    const QVariantList curves = hftrec::gui::sweepCurvesFromJsonl(rowsPath);
    ASSERT_EQ(curves.size(), 1);
    EXPECT_EQ(curves.front().toMap().value(QStringLiteral("curve")).toList().size(), 2);
}
