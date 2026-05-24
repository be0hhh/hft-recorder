#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QString>

#include <cstdlib>

#include "gui/viewmodels/BacktestViewModel.hpp"

namespace {

QString makeTempSessionDir() {
    QDir base(QDir::tempPath());
    const QString path = base.absoluteFilePath(QStringLiteral("hftrec_backtest_result_%1_%2")
                                                   .arg(QCoreApplication::applicationPid())
                                                   .arg(std::rand()));
    QDir().mkpath(path);
    QDir(path).mkpath(QStringLiteral("backtests"));
    return path;
}

void writeFile(const QString& path, const QByteArray& data) {
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(file.write(data), data.size());
}

void isolateSettings(QStringView suffix) {
    QCoreApplication::setOrganizationName(QStringLiteral("hftrec_backtest_tests"));
    QCoreApplication::setApplicationName(QStringLiteral("case_%1_%2").arg(suffix, QString::number(std::rand())));
    QSettings{}.clear();
}

bool hasParamKey(const QVariantList& rows, const QString& key) {
    for (const QVariant& row : rows) {
        if (row.toMap().value(QStringLiteral("key")).toString() == key) return true;
    }
    return false;
}

QString metricValue(const QVariantList& rows, const QString& key) {
    for (const QVariant& row : rows) {
        const QVariantMap map = row.toMap();
        if (map.value(QStringLiteral("key")).toString() == key) return map.value(QStringLiteral("value")).toString();
    }
    return {};
}

QString metricLabelValue(const QVariantList& rows, const QString& key) {
    for (const QVariant& row : rows) {
        const QVariantMap map = row.toMap();
        if (map.value(QStringLiteral("key")).toString() == key) return map.value(QStringLiteral("label")).toString();
    }
    return {};
}

}  // namespace

TEST(BacktestViewModel, LoadsValidResultAndSummary) {
    isolateSettings(QStringLiteral("valid"));
    const QString session = makeTempSessionDir();
    writeFile(QDir(session).absoluteFilePath(QStringLiteral("backtests/run-a.json")), R"json({
      "type":"run.result",
      "run_id":"run-a",
      "status":"complete",
      "strategy":"spread_maker1and2",
      "summary":{"fills":2,"pnl_raw":1200},
      "errors":[]
    })json");

    hftrec::gui::BacktestViewModel vm;
    vm.setSessionPath(session);

    ASSERT_EQ(vm.runCount(), 1);
    EXPECT_EQ(vm.selectedRunId(), QStringLiteral("run-a"));
    EXPECT_TRUE(vm.selectedJson().contains(QStringLiteral("spread_maker1and2")));
    EXPECT_TRUE(vm.selectedSummaryJson().contains(QStringLiteral("pnl_raw")));
    EXPECT_TRUE(vm.selectedErrorText().isEmpty());
}

TEST(BacktestViewModel, FormatsSummaryE8FieldsForDisplayOnly) {
    isolateSettings(QStringLiteral("human_summary"));
    const QString session = makeTempSessionDir();
    writeFile(QDir(session).absoluteFilePath(QStringLiteral("backtests/run-human.json")), R"json({
      "type":"run.result",
      "run_id":"run-human",
      "status":"complete",
      "strategy":"spread_maker1and2",
      "summary":{
        "events":3,
        "wallet_balance_e8":881513841,
        "net_qty_e8":454700000000,
        "negative_pnl_e8":-120000000,
        "zero_e8":0,
        "strategy_closed":false
      },
      "errors":[]
    })json");

    hftrec::gui::BacktestViewModel vm;
    vm.setSessionPath(session);

    const QString summary = vm.selectedSummaryJson();
    EXPECT_TRUE(summary.contains(QStringLiteral("wallet_balance")));
    EXPECT_TRUE(summary.contains(QStringLiteral("8.81513841")));
    EXPECT_TRUE(summary.contains(QStringLiteral("net_qty")));
    EXPECT_TRUE(summary.contains(QStringLiteral("4547")));
    EXPECT_TRUE(summary.contains(QStringLiteral("negative_pnl")));
    EXPECT_TRUE(summary.contains(QStringLiteral("-1.2")));
    EXPECT_TRUE(summary.contains(QStringLiteral("zero")));
    EXPECT_TRUE(summary.contains(QStringLiteral("events")));
    EXPECT_TRUE(summary.contains(QStringLiteral("3")));
    EXPECT_FALSE(summary.contains(QStringLiteral("wallet_balance_e8")));
    EXPECT_TRUE(vm.selectedJson().contains(QStringLiteral("wallet_balance_e8")));
    EXPECT_TRUE(vm.selectedJson().contains(QStringLiteral("881513841")));
}

TEST(BacktestViewModel, FallsBackToFileNameWhenRunIdMissing) {
    isolateSettings(QStringLiteral("fallback"));
    const QString session = makeTempSessionDir();
    writeFile(QDir(session).absoluteFilePath(QStringLiteral("backtests/demo-run.json")), R"json({
      "type":"run.result",
      "status":"complete",
      "summary":{},
      "errors":[]
    })json");

    hftrec::gui::BacktestViewModel vm;
    vm.setSessionPath(session);

    ASSERT_EQ(vm.runCount(), 1);
    EXPECT_EQ(vm.selectedRunId(), QStringLiteral("demo-run"));
}

TEST(BacktestViewModel, UsesConfigMetadataForRunListLabels) {
    isolateSettings(QStringLiteral("config_metadata"));
    const QString session = makeTempSessionDir();
    const QString configPath = QDir(session).absoluteFilePath(QStringLiteral("backtests/spread_maker1and2-BTCUSDT-fixed-20260524-183012.ini"));
    writeFile(configPath, R"ini(# recorder backtest metadata
# display_name=spread_maker1and2 BTCUSDT fixed
# config_summary=fixed: distance_bps=20, trigger_bps=2, refresh_ms=1000

[strategy]
type=spread_maker1and2
)ini");
    const QByteArray json = QByteArrayLiteral(R"json({
      "type":"run.result",
      "run_id":"spread_maker1and2-BTCUSDT-fixed-20260524-183012",
      "status":"complete",
      "strategy":"spread_maker1and2",
      "config_path":")json")
        + configPath.toUtf8().replace('\\', '/')
        + QByteArrayLiteral(R"json(",
      "summary":{},
      "errors":[]
    })json");
    writeFile(QDir(session).absoluteFilePath(QStringLiteral("backtests/spread_maker1and2-BTCUSDT-fixed-20260524-183012.json")), json);

    hftrec::gui::BacktestViewModel vm;
    vm.setSessionPath(session);

    ASSERT_EQ(vm.runCount(), 1);
    const QVariantMap row = vm.runs().front().toMap();
    EXPECT_EQ(row.value(QStringLiteral("label")).toString(), QStringLiteral("spread_maker1and2 BTCUSDT fixed"));
    EXPECT_EQ(row.value(QStringLiteral("configText")).toString(), QStringLiteral("fixed: distance_bps=20, trigger_bps=2, refresh_ms=1000"));
}

TEST(BacktestViewModel, KeepsInvalidJsonAsInvalidRun) {
    isolateSettings(QStringLiteral("invalid"));
    const QString session = makeTempSessionDir();
    writeFile(QDir(session).absoluteFilePath(QStringLiteral("backtests/bad.json")), QByteArrayLiteral("{"));

    hftrec::gui::BacktestViewModel vm;
    vm.setSessionPath(session);

    ASSERT_EQ(vm.runCount(), 1);
    EXPECT_EQ(vm.selectedRunId(), QStringLiteral("bad"));
    EXPECT_FALSE(vm.selectedErrorText().isEmpty());
    EXPECT_TRUE(vm.selectedJson().contains(QStringLiteral("{")));
}

TEST(BacktestViewModel, DoesNotExpandLargeResultFilesDuringRefresh) {
    isolateSettings(QStringLiteral("large_result"));
    const QString session = makeTempSessionDir();
    QByteArray data = QByteArrayLiteral(R"json({
      "type":"run.result",
      "run_id":"run-large",
      "status":"complete",
      "strategy":"spread_maker1and2",
      "equity_points":[
    )json");
    data.append(QByteArray(9 * 1024 * 1024, ' '));
    data.append(QByteArrayLiteral(R"json({"ts_ns":100,"total_pnl_e8":1}],
      "summary":{"total_pnl_e8":1},
      "errors":[]
    })json"));
    writeFile(QDir(session).absoluteFilePath(QStringLiteral("backtests/run-large.json")), data);

    hftrec::gui::BacktestViewModel vm;
    vm.setSessionPath(session);

    ASSERT_EQ(vm.runCount(), 1);
    EXPECT_EQ(vm.selectedRunId(), QStringLiteral("run-large"));
    EXPECT_TRUE(vm.selectedJson().isEmpty());
    EXPECT_FALSE(vm.hasEquityPoints());
}

TEST(BacktestViewModel, PrefersCompactSummaryForLargeRawResult) {
    isolateSettings(QStringLiteral("large_compact_summary"));
    const QString session = makeTempSessionDir();
    QByteArray large = QByteArrayLiteral(R"json({
      "type":"run.result",
      "run_id":"run-large",
      "status":"complete",
      "strategy":"spread_maker1and2",
      "orders":[
    )json");
    large.append(QByteArray(9 * 1024 * 1024, ' '));
    large.append(QByteArrayLiteral(R"json()],"summary":{"total_pnl_e8":1},"errors":[]})json"));
    writeFile(QDir(session).absoluteFilePath(QStringLiteral("backtests/run-large.json")), large);
    writeFile(QDir(session).absoluteFilePath(QStringLiteral("backtests/run-large.summary.json")), R"json({
      "type":"run.summary",
      "run_id":"run-large",
      "status":"complete",
      "strategy":"spread_maker1and2",
      "equity_points":[
        {"ts_ns":100,"gross_total_pnl_e8":0,"net_total_pnl_e8":0,"fees_paid_e8":0},
        {"ts_ns":200,"gross_total_pnl_e8":200000000,"net_total_pnl_e8":150000000,"fees_paid_e8":50000000}
      ],
      "summary":{"total_pnl_e8":150000000,"gross_realized_pnl_e8":200000000,"fees_paid_e8":50000000,"strategy_closed":true},
      "errors":[]
    })json");

    hftrec::gui::BacktestViewModel vm;
    vm.setSessionPath(session);

    ASSERT_EQ(vm.runCount(), 1);
    EXPECT_EQ(vm.selectedRunId(), QStringLiteral("run-large"));
    EXPECT_TRUE(vm.hasEquityPoints());
    EXPECT_EQ(vm.selectedEquityPoints().size(), 2);
    EXPECT_EQ(metricValue(vm.selectedResultMetrics(), QStringLiteral("total_pnl_e8")), QStringLiteral("1.5"));
}

TEST(BacktestViewModel, ExposesEquityPointsAndMetricsForResultsTab) {
    isolateSettings(QStringLiteral("equity_points"));
    const QString session = makeTempSessionDir();
    writeFile(QDir(session).absoluteFilePath(QStringLiteral("backtests/run-equity.json")), R"json({
      "type":"run.result",
      "run_id":"run-equity",
      "status":"complete",
      "strategy":"spread_maker1and2",
      "equity_points":[
        {"ts_ns":100,"gross_realized_pnl_e8":0,"realized_pnl_e8":0,"unrealized_pnl_e8":50000000,"gross_total_pnl_e8":50000000,"net_total_pnl_e8":50000000,"total_pnl_e8":50000000,"fees_paid_e8":0,"wallet_balance_e8":100000000000,"position_qty_e8":100000000},
        {"ts_ns":200,"gross_realized_pnl_e8":120000000,"realized_pnl_e8":100000000,"unrealized_pnl_e8":50000000,"gross_total_pnl_e8":170000000,"net_total_pnl_e8":150000000,"total_pnl_e8":150000000,"fees_paid_e8":20000000,"wallet_balance_e8":100100000000,"position_qty_e8":100000000}
      ],
      "summary":{"orders":3,"fills":2,"initial_balance_e8":100000000000,"gross_realized_pnl_e8":120000000,"fees_paid_e8":20000000,"net_realized_pnl_e8":100000000,"total_pnl_e8":150000000,"realized_pnl_e8":100000000,"unrealized_pnl_e8":50000000,"wallet_balance_e8":100100000000,"open_position_qty_e8":100000000,"strategy_closed":false},
      "errors":[]
    })json");

    hftrec::gui::BacktestViewModel vm;
    vm.setSessionPath(session);

    const QVariantList points = vm.selectedEquityPoints();
    ASSERT_EQ(points.size(), 2);
    EXPECT_EQ(points.at(1).toMap().value(QStringLiteral("grossTotalPnlE8")).toLongLong(), 170000000ll);
    EXPECT_EQ(points.at(1).toMap().value(QStringLiteral("netTotalPnlE8")).toLongLong(), 150000000ll);
    EXPECT_EQ(points.at(1).toMap().value(QStringLiteral("feesPaidE8")).toLongLong(), 20000000ll);
    EXPECT_EQ(points.at(1).toMap().value(QStringLiteral("totalPnlE8")).toLongLong(), 150000000ll);
    EXPECT_TRUE(vm.hasEquityPoints());
    EXPECT_EQ(metricValue(vm.selectedResultMetrics(), QStringLiteral("gross_realized_pnl_e8")), QStringLiteral("1.2"));
    EXPECT_EQ(metricValue(vm.selectedResultMetrics(), QStringLiteral("fees_paid_e8")), QStringLiteral("0.2"));
    EXPECT_EQ(metricValue(vm.selectedResultMetrics(), QStringLiteral("total_pnl_e8")), QStringLiteral("1.5"));
    EXPECT_EQ(metricValue(vm.selectedResultMetrics(), QStringLiteral("initial_balance_e8")), QStringLiteral("1000"));
    EXPECT_EQ(metricValue(vm.selectedResultMetrics(), QStringLiteral("wallet_balance_e8")), QStringLiteral("1001"));
    EXPECT_EQ(metricLabelValue(vm.selectedResultMetrics(), QStringLiteral("wallet_balance_e8")), QStringLiteral("Final balance"));
    EXPECT_EQ(metricValue(vm.selectedResultMetrics(), QStringLiteral("fills")), QStringLiteral("2"));
}

TEST(BacktestViewModel, UsesOldEquityPointTotalAsNetFallback) {
    isolateSettings(QStringLiteral("old_equity_points"));
    const QString session = makeTempSessionDir();
    writeFile(QDir(session).absoluteFilePath(QStringLiteral("backtests/run-old-equity.json")), R"json({
      "type":"run.result",
      "run_id":"run-old-equity",
      "status":"complete",
      "strategy":"spread_maker1and2",
      "equity_points":[
        {"ts_ns":100,"realized_pnl_e8":0,"unrealized_pnl_e8":50000000,"total_pnl_e8":50000000,"wallet_balance_e8":100000000000,"position_qty_e8":100000000}
      ],
      "summary":{"total_pnl_e8":50000000},
      "errors":[]
    })json");

    hftrec::gui::BacktestViewModel vm;
    vm.setSessionPath(session);

    const QVariantMap point = vm.selectedEquityPoints().at(0).toMap();
    EXPECT_EQ(point.value(QStringLiteral("netTotalPnlE8")).toLongLong(), 50000000ll);
    EXPECT_EQ(point.value(QStringLiteral("grossTotalPnlE8")).toLongLong(), 50000000ll);
    EXPECT_EQ(point.value(QStringLiteral("feesPaidE8")).toLongLong(), 0ll);
}

TEST(BacktestViewModel, UsesOldEquityPointRealizedAsTotalFallbackAndAppendsSummary) {
    isolateSettings(QStringLiteral("old_realized_equity_points"));
    const QString session = makeTempSessionDir();
    writeFile(QDir(session).absoluteFilePath(QStringLiteral("backtests/run-old-realized-equity.json")), R"json({
      "type":"run.result",
      "run_id":"run-old-realized-equity",
      "status":"complete",
      "strategy":"spread_maker1and2",
      "equity_points":[
        {"ts_ns":100,"realized_pnl_e8":-500000},
        {"ts_ns":200,"realized_pnl_e8":11000000}
      ],
      "summary":{"gross_realized_pnl_e8":328410362,"fees_paid_e8":63598466,"net_realized_pnl_e8":264811896,"realized_pnl_e8":264811896,"unrealized_pnl_e8":229831830,"total_pnl_e8":494643726,"wallet_balance_e8":100494643726,"open_position_qty_e8":100000000},
      "errors":[]
    })json");

    hftrec::gui::BacktestViewModel vm;
    vm.setSessionPath(session);

    const QVariantList points = vm.selectedEquityPoints();
    ASSERT_EQ(points.size(), 3);
    EXPECT_EQ(points.at(1).toMap().value(QStringLiteral("netTotalPnlE8")).toLongLong(), 11000000ll);
    EXPECT_EQ(points.at(2).toMap().value(QStringLiteral("grossTotalPnlE8")).toLongLong(), 558242192ll);
    EXPECT_EQ(points.at(2).toMap().value(QStringLiteral("netTotalPnlE8")).toLongLong(), 494643726ll);
    EXPECT_EQ(points.at(2).toMap().value(QStringLiteral("feesPaidE8")).toLongLong(), 63598466ll);
}

TEST(BacktestViewModel, ReadsSymbolFromNestedManifestAndAllowsOverride) {
    isolateSettings(QStringLiteral("symbol"));
    const QString session = makeTempSessionDir();
    writeFile(QDir(session).absoluteFilePath(QStringLiteral("manifest.json")), R"json({
      "identity":{"exchange":"binance","market":"futures_usd","symbols":["AGTUSDT"]}
    })json");

    hftrec::gui::BacktestViewModel vm;
    vm.setSessionPath(session);

    EXPECT_EQ(vm.selectedSymbol(), QStringLiteral("AGTUSDT"));
    vm.setSelectedSymbol(QStringLiteral("btcusdt"));
    EXPECT_EQ(vm.selectedSymbol(), QStringLiteral("BTCUSDT"));
}

TEST(BacktestViewModel, ConfigModeFiltersFixedAndNatrParameterKeys) {
    isolateSettings(QStringLiteral("natr_filter"));

    hftrec::gui::BacktestViewModel vm;
    vm.setSelectedStrategy(QStringLiteral("spread_maker1and2"));

    vm.setConfigMode(QStringLiteral("fixed"));
    EXPECT_TRUE(hasParamKey(vm.strategyParameters(), QStringLiteral("distance_bps")));
    EXPECT_FALSE(hasParamKey(vm.strategyParameters(), QStringLiteral("distance_natr_pct")));
    EXPECT_FALSE(hasParamKey(vm.strategyParameters(), QStringLiteral("natr_ema_period_seconds")));

    vm.setConfigMode(QStringLiteral("natr"));
    EXPECT_FALSE(hasParamKey(vm.strategyParameters(), QStringLiteral("distance_bps")));
    EXPECT_TRUE(hasParamKey(vm.strategyParameters(), QStringLiteral("distance_natr_pct")));
    EXPECT_TRUE(hasParamKey(vm.strategyParameters(), QStringLiteral("natr_ema_period_seconds")));
}

TEST(BacktestViewModel, PersistsConfigButNotSession) {
    isolateSettings(QStringLiteral("persist"));
    const QString session = makeTempSessionDir();

    {
        hftrec::gui::BacktestViewModel vm;
        vm.setSessionPath(session);
        vm.setSelectedStrategy(QStringLiteral("spread_maker1and2"));
        vm.setConfigMode(QStringLiteral("natr"));
        vm.setPingLatencyUs(QStringLiteral("2500"));
        vm.setInitialBalanceUsdt(QStringLiteral("750.25"));
        vm.setMakerFeeBps(QStringLiteral("0.2"));
        vm.setTakerFeeBps(QStringLiteral("0.5"));
        vm.setStrategyParameter(QStringLiteral("distance_natr_pct"), QStringLiteral("700"));
    }

    hftrec::gui::BacktestViewModel restored;
    EXPECT_EQ(restored.selectedStrategy(), QStringLiteral("spread_maker1and2"));
    EXPECT_EQ(restored.configMode(), QStringLiteral("natr"));
    EXPECT_EQ(restored.pingLatencyUs(), QStringLiteral("2500"));
    EXPECT_EQ(restored.initialBalanceUsdt(), QStringLiteral("750.25"));
    EXPECT_EQ(restored.makerFeeBps(), QStringLiteral("0.2"));
    EXPECT_EQ(restored.takerFeeBps(), QStringLiteral("0.5"));
    EXPECT_TRUE(hasParamKey(restored.strategyParameters(), QStringLiteral("distance_natr_pct")));
    EXPECT_NE(restored.sessionPath(), session);
}

