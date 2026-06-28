#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QSettings>
#include <QString>

#include <cstdlib>

#include "core/capture/SessionManifest.hpp"
#include "gui/backtests/BacktestViewModel.hpp"

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

void writeRecordingManifest(const QString& sessionDir,
                            const QString& sessionId,
                            const QString& exchange,
                            const QString& market,
                            const QString& symbol,
                            qint64 startedAtNs) {
    QDir().mkpath(sessionDir);
    hftrec::capture::SessionManifest manifest{};
    manifest.sessionId = sessionId.toStdString();
    manifest.exchange = exchange.toStdString();
    manifest.market = market.toStdString();
    manifest.symbols = {symbol.toStdString()};
    manifest.sessionStatus = "complete";
    manifest.startedAtNs = startedAtNs;
    manifest.endedAtNs = startedAtNs + 60'000'000'000LL;
    manifest.bookTickerEnabled = true;
    manifest.bookTickerCount = 12;
    writeFile(QDir(sessionDir).absoluteFilePath(QStringLiteral("manifest.json")),
              QByteArray::fromStdString(hftrec::capture::renderManifestJson(manifest)));
}

QString makeRunDir(const QString& session, const QString& runId, const QByteArray& manifest, const QByteArray& equity = {}) {
    const QString dir = QDir(session).absoluteFilePath(QStringLiteral("backtests/%1").arg(runId));
    QDir().mkpath(dir);
    writeFile(QDir(dir).absoluteFilePath(QStringLiteral("manifest.json")), manifest);
    writeFile(QDir(dir).absoluteFilePath(QStringLiteral("equity.jsonl")), equity);
    writeFile(QDir(dir).absoluteFilePath(QStringLiteral("order_lifetimes.jsonl")), QByteArray{});
    writeFile(QDir(dir).absoluteFilePath(QStringLiteral("fills.jsonl")), QByteArray{});
    return dir;
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

bool hasChoiceId(const QVariantList& rows, const QString& id) {
    for (const QVariant& row : rows) {
        if (row.toMap().value(QStringLiteral("id")).toString() == id) return true;
    }
    return false;
}

QString choiceLabel(const QVariantList& rows, const QString& id) {
    for (const QVariant& row : rows) {
        const QVariantMap map = row.toMap();
        if (map.value(QStringLiteral("id")).toString() == id) return map.value(QStringLiteral("label")).toString();
    }
    return {};
}

QString metricValue(const QVariantList& rows, const QString& key) {
    for (const QVariant& row : rows) {
        const QVariantMap map = row.toMap();
        if (map.value(QStringLiteral("key")).toString() == key) return map.value(QStringLiteral("value")).toString();
    }
    return {};
}

QString paramValue(const QVariantList& rows, const QString& key) {
    return metricValue(rows, key);
}

QString metricLabelValue(const QVariantList& rows, const QString& key) {
    for (const QVariant& row : rows) {
        const QVariantMap map = row.toMap();
        if (map.value(QStringLiteral("key")).toString() == key) return map.value(QStringLiteral("label")).toString();
    }
    return {};
}

QString scopeLabelValue(const QVariantList& rows, const QString& id) {
    for (const QVariant& row : rows) {
        const QVariantMap map = row.toMap();
        if (map.value(QStringLiteral("id")).toString() == id) return map.value(QStringLiteral("label")).toString();
    }
    return {};
}

void waitForDetailsLoad(hftrec::gui::BacktestViewModel& vm) {
    QElapsedTimer timer;
    timer.start();
    while (vm.selectedDetailsLoading() && timer.elapsed() < 5000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    }
    ASSERT_FALSE(vm.selectedDetailsLoading());
}

}  // namespace

TEST(BacktestViewModel, LoadsValidResultAndSummary) {
    isolateSettings(QStringLiteral("valid"));
    const QString session = makeTempSessionDir();
    makeRunDir(session, QStringLiteral("run-a"), R"json({
      "type":"run.result.v2",
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
    EXPECT_FALSE(vm.selectedDetailsLoaded());
    EXPECT_FALSE(vm.selectedDetailsLoading());
}

TEST(BacktestViewModel, DeletesSelectedRunDirectoryAndKeepsOtherRuns) {
    isolateSettings(QStringLiteral("delete_selected"));
    const QString session = makeTempSessionDir();
    const QString runA = makeRunDir(session, QStringLiteral("run-a"), R"json({
      "type":"run.result.v2",
      "run_id":"run-a",
      "status":"complete",
      "strategy":"spread_maker1and2",
      "summary":{},
      "errors":[]
    })json");
    const QString runB = makeRunDir(session, QStringLiteral("run-b"), R"json({
      "type":"run.result.v2",
      "run_id":"run-b",
      "status":"complete",
      "strategy":"spread_maker1and2",
      "summary":{},
      "errors":[]
    })json");

    hftrec::gui::BacktestViewModel vm;
    vm.setSessionPath(session);
    vm.selectRun(QStringLiteral("run-a"));

    ASSERT_TRUE(vm.deleteSelectedRun());

    EXPECT_FALSE(QDir(runA).exists());
    EXPECT_TRUE(QDir(runB).exists());
    ASSERT_EQ(vm.runCount(), 1);
    EXPECT_EQ(vm.selectedRunId(), QStringLiteral("run-b"));
}

TEST(BacktestViewModel, FormatsSummaryE8FieldsForDisplayOnly) {
    isolateSettings(QStringLiteral("human_summary"));
    const QString session = makeTempSessionDir();
    makeRunDir(session, QStringLiteral("run-human"), R"json({
      "type":"run.result.v2",
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

TEST(BacktestViewModel, ExposesBacktestDiagnosticsAsMetrics) {
    isolateSettings(QStringLiteral("diagnostics_metrics"));
    const QString session = makeTempSessionDir();
    makeRunDir(session, QStringLiteral("run-diagnostics"), R"json({
      "type":"run.result.v2",
      "run_id":"run-diagnostics",
      "status":"complete",
      "strategy":"spread_maker1and2",
      "summary":{"orders":0,"fills":0,"initial_balance_e8":10000000000},
      "diagnostics":{
        "version":1,
        "zero_activity_reason":"no_signal_below_threshold",
        "signal":{
          "strategy_spread_rows":2,
          "entry_edge_bps_e8":5000000000,
          "max_edge_after_cost_bps_e8":4813596587
        },
        "rate_limits":{
          "rate_limit_rejected_orders":0,
          "rate_limit_rejected_cancels":0,
          "rate_limit_rejected_commands":0
        }
      },
      "errors":[]
    })json");

    hftrec::gui::BacktestViewModel vm;
    vm.setSessionPath(session);

    const QVariantList metrics = vm.selectedResultMetrics();
    EXPECT_EQ(metricValue(metrics, QStringLiteral("diagnostics.zero_activity_reason")),
              QStringLiteral("no_signal_below_threshold"));
    EXPECT_EQ(metricValue(metrics, QStringLiteral("diagnostics.max_edge_after_cost_bps_e8")),
              QStringLiteral("48.13596587 bps"));
    EXPECT_EQ(metricValue(metrics, QStringLiteral("diagnostics.entry_edge_bps_e8")),
              QStringLiteral("50 bps"));
    EXPECT_EQ(metricValue(metrics, QStringLiteral("diagnostics.rate_limit_rejected_commands")),
              QStringLiteral("0"));
}

TEST(BacktestViewModel, FallsBackToFileNameWhenRunIdMissing) {
    isolateSettings(QStringLiteral("fallback"));
    const QString session = makeTempSessionDir();
    makeRunDir(session, QStringLiteral("demo-run"), R"json({
      "type":"run.result.v2",
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
    const QString runId = QStringLiteral("spread_maker1and2-BTCUSDT-fixed-20260524-183012");
    QDir().mkpath(QDir(session).absoluteFilePath(QStringLiteral("backtests/%1").arg(runId)));
    const QString configPath = QDir(session).absoluteFilePath(QStringLiteral("backtests/%1/config.ini").arg(runId));
    writeFile(configPath, R"ini(# recorder backtest metadata
# display_name=spread_maker1and2 BTCUSDT fixed
# config_summary=fixed: distance_bps=20, trigger_bps=2, refresh_ms=1000

[strategy]
type=spread_maker1and2
)ini");
    const QByteArray json = QByteArrayLiteral(R"json({
      "type":"run.result.v2",
      "run_id":"spread_maker1and2-BTCUSDT-fixed-20260524-183012",
      "status":"complete",
      "strategy":"spread_maker1and2",
      "config_path":")json")
        + configPath.toUtf8().replace('\\', '/')
        + QByteArrayLiteral(R"json(",
      "summary":{},
      "errors":[]
    })json");
    makeRunDir(session, runId, json);

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
    const QString dir = QDir(session).absoluteFilePath(QStringLiteral("backtests/bad"));
    QDir().mkpath(dir);
    writeFile(QDir(dir).absoluteFilePath(QStringLiteral("manifest.json")), QByteArrayLiteral("{"));

    hftrec::gui::BacktestViewModel vm;
    vm.setSessionPath(session);

    ASSERT_EQ(vm.runCount(), 1);
    EXPECT_EQ(vm.selectedRunId(), QStringLiteral("bad"));
    EXPECT_FALSE(vm.selectedErrorText().isEmpty());
    EXPECT_TRUE(vm.selectedJson().contains(QStringLiteral("{")));
}

TEST(BacktestViewModel, IgnoresLooseLegacyJsonResultFiles) {
    isolateSettings(QStringLiteral("legacy_ignored"));
    const QString session = makeTempSessionDir();
    writeFile(QDir(session).absoluteFilePath(QStringLiteral("backtests/run-legacy.json")), R"json({
      "type":"run.result",
      "run_id":"run-legacy",
      "status":"complete",
      "summary":{},
      "errors":[]
    })json");
    writeFile(QDir(session).absoluteFilePath(QStringLiteral("backtests/run-legacy.summary.json")), R"json({
      "type":"run.summary",
      "run_id":"run-legacy",
      "summary":{},
      "errors":[]
    })json");

    hftrec::gui::BacktestViewModel vm;
    vm.setSessionPath(session);

    EXPECT_EQ(vm.runCount(), 0);
    EXPECT_TRUE(vm.selectedRunId().isEmpty());
}

TEST(BacktestViewModel, DefersEquityPointsAndMetricsUntilDetailsLoad) {
    isolateSettings(QStringLiteral("equity_points"));
    const QString session = makeTempSessionDir();
    makeRunDir(session, QStringLiteral("run-equity"), R"json({
      "type":"run.result.v2",
      "run_id":"run-equity",
      "status":"complete",
      "strategy":"spread_maker1and2",
      "streams":{"equity":{"rows":2}},
      "summary":{"orders":3,"fills":2,"initial_balance_e8":100000000000,"gross_realized_pnl_e8":120000000,"fees_paid_e8":20000000,"net_realized_pnl_e8":100000000,"total_pnl_e8":150000000,"realized_pnl_e8":100000000,"unrealized_pnl_e8":50000000,"wallet_balance_e8":100100000000,"open_position_qty_e8":100000000,"strategy_closed":false},
      "errors":[]
    })json", QByteArrayLiteral(
        "[100,0,0,50000000,50000000,50000000,50000000,0,100000000000,100000000,0]\n"
        "[200,120000000,100000000,50000000,170000000,150000000,150000000,20000000,100100000000,100000000,2]\n"));

    hftrec::gui::BacktestViewModel vm;
    vm.setSessionPath(session);

    EXPECT_FALSE(vm.selectedDetailsLoaded());
    EXPECT_TRUE(vm.selectedEquityPoints().empty());
    EXPECT_TRUE(vm.selectedResultMetrics().empty());
    EXPECT_FALSE(vm.hasEquityPoints());

    vm.loadSelectedRunDetails();
    waitForDetailsLoad(vm);

    EXPECT_TRUE(vm.selectedDetailsLoaded());
    EXPECT_TRUE(vm.selectedDetailsErrorText().isEmpty());
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

TEST(BacktestViewModel, ExposesPortfolioAndLegResultScopes) {
    isolateSettings(QStringLiteral("result_scopes"));
    const QString session = makeTempSessionDir();
    const QString runDir = makeRunDir(session, QStringLiteral("run-legs"), R"json({
      "type":"run.result.v2",
      "run_id":"run-legs",
      "status":"complete",
      "strategy":"stat_arb_band_ladder",
      "streams":{"equity":{"rows":2}},
      "summary":{"orders":4,"fills":4,"initial_balance_e8":30000000000,"gross_realized_pnl_e8":90000000,"fees_paid_e8":30000000,"net_realized_pnl_e8":60000000,"total_pnl_e8":70000000,"realized_pnl_e8":60000000,"unrealized_pnl_e8":10000000,"wallet_balance_e8":30060000000},
      "legs":[
        {"leg_index":0,"exchange":"binance","market":"futures","symbol":"BTCUSDT","initial_balance_e8":10000000000,"wallet_balance_e8":10025000000,"gross_realized_pnl_e8":40000000,"fees_paid_e8":15000000,"net_realized_pnl_e8":25000000,"unrealized_pnl_e8":5000000,"total_pnl_e8":30000000,"orders":2,"fills":2,"equity":{"path":"legs/0/equity.jsonl","rows":2}},
        {"leg_index":1,"exchange":"okx","market":"futures","symbol":"ETHUSDT","initial_balance_e8":20000000000,"wallet_balance_e8":20035000000,"gross_realized_pnl_e8":50000000,"fees_paid_e8":15000000,"net_realized_pnl_e8":35000000,"unrealized_pnl_e8":5000000,"total_pnl_e8":40000000,"orders":2,"fills":2,"equity":{"path":"legs/1/equity.jsonl","rows":2}}
      ],
      "errors":[]
    })json", QByteArrayLiteral(
        "[100,0,0,0,0,0,0,0,30000000000,0,0]\n"
        "[200,90000000,60000000,10000000,100000000,70000000,70000000,30000000,30060000000,0,4]\n"));
    QDir().mkpath(QDir(runDir).absoluteFilePath(QStringLiteral("legs/0")));
    QDir().mkpath(QDir(runDir).absoluteFilePath(QStringLiteral("legs/1")));
    writeFile(QDir(runDir).absoluteFilePath(QStringLiteral("legs/0/equity.jsonl")), QByteArrayLiteral(
        "[100,0,0,0,0,0,0,0,10000000000,0,0]\n"
        "[200,40000000,25000000,5000000,45000000,30000000,30000000,15000000,10025000000,0,2]\n"));
    writeFile(QDir(runDir).absoluteFilePath(QStringLiteral("legs/1/equity.jsonl")), QByteArrayLiteral(
        "[100,0,0,0,0,0,0,0,20000000000,0,0]\n"
        "[200,50000000,35000000,5000000,55000000,40000000,40000000,15000000,20035000000,0,2]\n"));

    hftrec::gui::BacktestViewModel vm;
    vm.setSessionPath(session);
    vm.loadSelectedRunDetails();
    waitForDetailsLoad(vm);

    const QVariantList scopes = vm.resultScopeChoices();
    ASSERT_EQ(scopes.size(), 3);
    EXPECT_EQ(scopeLabelValue(scopes, QStringLiteral("portfolio")), QStringLiteral("Portfolio"));
    EXPECT_TRUE(scopeLabelValue(scopes, QStringLiteral("leg_0")).contains(QStringLiteral("binance")));
    EXPECT_TRUE(scopeLabelValue(scopes, QStringLiteral("leg_1")).contains(QStringLiteral("okx")));
    EXPECT_EQ(vm.selectedResultScope(), QStringLiteral("portfolio"));
    EXPECT_EQ(metricValue(vm.selectedResultMetrics(), QStringLiteral("total_pnl_e8")), QStringLiteral("0.7"));

    vm.setSelectedResultScope(QStringLiteral("leg_1"));

    EXPECT_EQ(vm.selectedResultScope(), QStringLiteral("leg_1"));
    EXPECT_EQ(metricValue(vm.selectedResultMetrics(), QStringLiteral("total_pnl_e8")), QStringLiteral("0.4"));
    EXPECT_EQ(metricValue(vm.selectedResultMetrics(), QStringLiteral("wallet_balance_e8")), QStringLiteral("200.35"));
    const QVariantList points = vm.selectedEquityPoints();
    ASSERT_EQ(points.size(), 2);
    EXPECT_EQ(points.at(1).toMap().value(QStringLiteral("totalPnlE8")).toLongLong(), 40000000ll);
    EXPECT_EQ(points.at(1).toMap().value(QStringLiteral("walletBalanceE8")).toLongLong(), 20035000000ll);
}

TEST(BacktestViewModel, SynthesizesPortfolioEquityFromLegStreamsWhenAggregateHasOnlyFinalPoint) {
    isolateSettings(QStringLiteral("portfolio_from_legs"));
    const QString session = makeTempSessionDir();
    const QString runDir = makeRunDir(session, QStringLiteral("run-leg-series"), R"json({
      "type":"run.result.v2",
      "run_id":"run-leg-series",
      "status":"complete",
      "strategy":"stat_arb_band_ladder",
      "streams":{"equity":{"rows":1}},
      "summary":{"fills":4,"initial_balance_e8":30000000000,"gross_realized_pnl_e8":90000000,"fees_paid_e8":30000000,"net_realized_pnl_e8":60000000,"total_pnl_e8":70000000,"realized_pnl_e8":60000000,"unrealized_pnl_e8":10000000,"wallet_balance_e8":30060000000},
      "legs":[
        {"leg_index":0,"exchange":"binance","market":"futures","symbol":"BTCUSDT","initial_balance_e8":10000000000,"wallet_balance_e8":10025000000,"gross_realized_pnl_e8":40000000,"fees_paid_e8":15000000,"net_realized_pnl_e8":25000000,"unrealized_pnl_e8":5000000,"total_pnl_e8":30000000,"fills":2,"equity":{"path":"legs/0/equity.jsonl","rows":2}},
        {"leg_index":1,"exchange":"okx","market":"futures","symbol":"ETHUSDT","initial_balance_e8":20000000000,"wallet_balance_e8":20035000000,"gross_realized_pnl_e8":50000000,"fees_paid_e8":15000000,"net_realized_pnl_e8":35000000,"unrealized_pnl_e8":5000000,"total_pnl_e8":40000000,"fills":2,"equity":{"path":"legs/1/equity.jsonl","rows":2}}
      ],
      "errors":[]
    })json", QByteArrayLiteral(
        "[200,90000000,60000000,10000000,100000000,70000000,70000000,30000000,30060000000,0,4]\n"));
    QDir().mkpath(QDir(runDir).absoluteFilePath(QStringLiteral("legs/0")));
    QDir().mkpath(QDir(runDir).absoluteFilePath(QStringLiteral("legs/1")));
    writeFile(QDir(runDir).absoluteFilePath(QStringLiteral("legs/0/equity.jsonl")), QByteArrayLiteral(
        "[100,0,0,0,0,0,0,0,10000000000,0,0]\n"
        "[200,40000000,25000000,5000000,45000000,30000000,30000000,15000000,10025000000,0,2]\n"));
    writeFile(QDir(runDir).absoluteFilePath(QStringLiteral("legs/1/equity.jsonl")), QByteArrayLiteral(
        "[100,0,0,0,0,0,0,0,20000000000,0,0]\n"
        "[200,50000000,35000000,5000000,55000000,40000000,40000000,15000000,20035000000,0,2]\n"));

    hftrec::gui::BacktestViewModel vm;
    vm.setSessionPath(session);
    vm.loadSelectedRunDetails();
    waitForDetailsLoad(vm);

    ASSERT_TRUE(vm.selectedDetailsLoaded());
    ASSERT_EQ(vm.selectedResultScope(), QStringLiteral("portfolio"));
    const QVariantList points = vm.selectedEquityPoints();
    ASSERT_EQ(points.size(), 2);
    EXPECT_EQ(points.at(0).toMap().value(QStringLiteral("walletBalanceE8")).toLongLong(), 30000000000ll);
    EXPECT_EQ(points.at(1).toMap().value(QStringLiteral("totalPnlE8")).toLongLong(), 70000000ll);
    EXPECT_EQ(points.at(1).toMap().value(QStringLiteral("feesPaidE8")).toLongLong(), 30000000ll);
    EXPECT_EQ(points.at(1).toMap().value(QStringLiteral("fillCount")).toLongLong(), 4);
}

TEST(BacktestViewModel, ClearsLoadedDetailsWhenRunChanges) {
    isolateSettings(QStringLiteral("clear_details"));
    const QString session = makeTempSessionDir();
    makeRunDir(session, QStringLiteral("run-a"), R"json({
      "type":"run.result.v2",
      "run_id":"run-a",
      "status":"complete",
      "strategy":"spread_maker1and2",
      "streams":{"equity":{"rows":1}},
      "summary":{"total_pnl_e8":100000000,"initial_balance_e8":100000000000},
      "errors":[]
    })json", QByteArrayLiteral("[100,0,0,0,100000000,100000000,100000000,0,100000000000,0,0]\n"));
    makeRunDir(session, QStringLiteral("run-b"), R"json({
      "type":"run.result.v2",
      "run_id":"run-b",
      "status":"complete",
      "strategy":"spread_maker1and2",
      "streams":{"equity":{"rows":1}},
      "summary":{"total_pnl_e8":200000000,"initial_balance_e8":100000000000},
      "errors":[]
    })json", QByteArrayLiteral("[100,0,0,0,200000000,200000000,200000000,0,100000000000,0,0]\n"));

    hftrec::gui::BacktestViewModel vm;
    vm.setSessionPath(session);
    vm.selectRun(QStringLiteral("run-a"));
    vm.loadSelectedRunDetails();
    waitForDetailsLoad(vm);
    ASSERT_TRUE(vm.selectedDetailsLoaded());

    vm.selectRun(QStringLiteral("run-b"));

    EXPECT_FALSE(vm.selectedDetailsLoaded());
    EXPECT_TRUE(vm.selectedEquityPoints().empty());
    EXPECT_TRUE(vm.selectedResultMetrics().empty());
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

TEST(BacktestViewModel, ExplainsBacktestConfigDirectoryWriteFailure) {
    isolateSettings(QStringLiteral("config_write_failure"));
    const QString session = makeTempSessionDir();
    writeRecordingManifest(session,
                           QStringLiteral("session-config-write-failure"),
                           QStringLiteral("binance"),
                           QStringLiteral("futures"),
                           QStringLiteral("BTCUSDT"),
                           1'700'000'000'000'000'000LL);
    QDir(QDir(session).absoluteFilePath(QStringLiteral("backtests"))).removeRecursively();
    writeFile(QDir(session).absoluteFilePath(QStringLiteral("backtests")), QByteArrayLiteral("not a directory"));

    hftrec::gui::BacktestViewModel vm;
    vm.setSessionPath(session);
    vm.setSelectedStrategy(QStringLiteral("spread_maker1and2"));

    ASSERT_TRUE(vm.canRun());
    vm.startBacktest();

    EXPECT_FALSE(vm.running());
    EXPECT_TRUE(vm.statusText().contains(QStringLiteral("Failed to write backtest config")));
    EXPECT_TRUE(vm.statusText().contains(QStringLiteral("cannot create directory")));
    EXPECT_TRUE(vm.statusText().contains(QDir(session).absoluteFilePath(QStringLiteral("backtests"))));
}

TEST(BacktestViewModel, ExposesOnlyMetadataParameterKeys) {
    isolateSettings(QStringLiteral("metadata_params"));

    hftrec::gui::BacktestViewModel vm;
    vm.setSelectedStrategy(QStringLiteral("spread_maker1and2"));

    EXPECT_EQ(vm.configMode(), QStringLiteral("fixed"));
    EXPECT_EQ(vm.strategyParameters().size(), 6);
    EXPECT_TRUE(hasParamKey(vm.strategyParameters(), QStringLiteral("distance_bps")));
    EXPECT_EQ(metricLabelValue(vm.strategyParameters(), QStringLiteral("distance_bps")), QStringLiteral("distance_bps"));
    EXPECT_TRUE(hasParamKey(vm.strategyParameters(), QStringLiteral("trigger_bps")));
    EXPECT_TRUE(hasParamKey(vm.strategyParameters(), QStringLiteral("refresh_ms")));
    EXPECT_TRUE(hasParamKey(vm.strategyParameters(), QStringLiteral("close_delay_us")));
    EXPECT_FALSE(hasParamKey(vm.strategyParameters(), QStringLiteral("rate_limit_guard_min_remaining")));
    EXPECT_EQ(vm.riskRateLimitGuardMinRemaining(), QStringLiteral("2"));
    EXPECT_TRUE(hasParamKey(vm.strategyParameters(), QStringLiteral("sizing_mode")));
    EXPECT_TRUE(hasParamKey(vm.strategyParameters(), QStringLiteral("amount_qty")));
    EXPECT_FALSE(hasParamKey(vm.strategyParameters(), QStringLiteral("order_qty")));
    vm.setStrategyParameterGroup(1, QStringLiteral("order_qty"));
    EXPECT_FALSE(hasParamKey(vm.strategyParameters(), QStringLiteral("amount_qty")));
    EXPECT_TRUE(hasParamKey(vm.strategyParameters(), QStringLiteral("order_qty")));
    EXPECT_FALSE(hasParamKey(vm.strategyParameters(), QStringLiteral("distance_natr_pct")));
    EXPECT_FALSE(hasParamKey(vm.strategyParameters(), QStringLiteral("natr_ema_period_seconds")));

    vm.setConfigMode(QStringLiteral("natr"));
    EXPECT_EQ(vm.configMode(), QStringLiteral("fixed"));
    EXPECT_FALSE(hasParamKey(vm.strategyParameters(), QStringLiteral("distance_natr_pct")));
}

TEST(BacktestViewModel, ExposesTemplateStrategyParameters) {
    isolateSettings(QStringLiteral("template_params"));

    hftrec::gui::BacktestViewModel vm;
    vm.setSelectedStrategy(QStringLiteral("stat_arb_band_ladder"));

    EXPECT_TRUE(hasParamKey(vm.strategyParameters(), QStringLiteral("ema_window_ms")));
    EXPECT_TRUE(hasParamKey(vm.strategyParameters(), QStringLiteral("entry_edge_bps")));
    EXPECT_TRUE(hasParamKey(vm.strategyParameters(), QStringLiteral("ladder_step_bps")));
    EXPECT_TRUE(hasParamKey(vm.strategyParameters(), QStringLiteral("exit_edge_bps")));
    EXPECT_TRUE(hasParamKey(vm.strategyParameters(), QStringLiteral("max_ladder_levels")));
    EXPECT_FALSE(hasParamKey(vm.strategyParameters(), QStringLiteral("rate_limit_guard_min_remaining")));
    EXPECT_EQ(vm.riskRateLimitGuardMinRemaining(), QStringLiteral("2"));
    EXPECT_TRUE(hasParamKey(vm.strategyParameters(), QStringLiteral("amount_qty")));
    EXPECT_FALSE(hasParamKey(vm.strategyParameters(), QStringLiteral("pending_timeout_ms")));
    EXPECT_FALSE(hasParamKey(vm.strategyParameters(), QStringLiteral("max_quote_age_ms")));
    EXPECT_FALSE(hasParamKey(vm.strategyParameters(), QStringLiteral("max_leg_skew_ms")));
    EXPECT_FALSE(hasParamKey(vm.strategyParameters(), QStringLiteral("type")));
    EXPECT_FALSE(hasParamKey(vm.strategyParameters(), QStringLiteral("enabled")));
}

TEST(BacktestViewModel, PersistsNegativeStatArbEdgeMakerEntryExitParams) {
    isolateSettings(QStringLiteral("negative_edge_params"));

    {
        hftrec::gui::BacktestViewModel vm;
        vm.setSelectedStrategy(QStringLiteral("stat_arb_edge_maker"));
        vm.setStrategyParameter(QStringLiteral("entry_edge_bps"), QStringLiteral("-5"));
        vm.setStrategyParameter(QStringLiteral("exit_edge_bps"), QStringLiteral("-2.5"));

        EXPECT_EQ(paramValue(vm.strategyParameters(), QStringLiteral("entry_edge_bps")), QStringLiteral("-5"));
        EXPECT_EQ(paramValue(vm.strategyParameters(), QStringLiteral("exit_edge_bps")), QStringLiteral("-2.5"));
    }

    hftrec::gui::BacktestViewModel restored;
    restored.setSelectedStrategy(QStringLiteral("stat_arb_edge_maker"));
    EXPECT_EQ(paramValue(restored.strategyParameters(), QStringLiteral("entry_edge_bps")), QStringLiteral("-5"));
    EXPECT_EQ(paramValue(restored.strategyParameters(), QStringLiteral("exit_edge_bps")), QStringLiteral("-2.5"));
}

TEST(BacktestViewModel, HidesUndeclaredIndicatorProfiles) {
    isolateSettings(QStringLiteral("indicator_metadata"));

    hftrec::gui::BacktestViewModel vm;
    vm.setSelectedStrategy(QStringLiteral("spread_maker1and2"));

    EXPECT_TRUE(vm.indicatorProfileChoices().empty());
    EXPECT_TRUE(vm.selectedIndicatorProfile().isEmpty());
    vm.setSelectedIndicatorProfile(QStringLiteral("trend_score"));
    EXPECT_TRUE(vm.selectedIndicatorProfile().isEmpty());
}

TEST(BacktestViewModel, ExposesTrendProbeIndicatorWithoutStrategyParams) {
    isolateSettings(QStringLiteral("trend_probe"));

    hftrec::gui::BacktestViewModel vm;

    EXPECT_TRUE(hasChoiceId(vm.strategyChoices(), QStringLiteral("trend_probe")));
    vm.setSelectedStrategy(QStringLiteral("trend_probe"));
    EXPECT_TRUE(vm.strategyParameters().empty());
    EXPECT_EQ(vm.configModeChoices().size(), 1);
    const QVariantList indicators = vm.indicatorProfileChoices();
    ASSERT_EQ(indicators.size(), 1);
    EXPECT_EQ(choiceLabel(indicators, QStringLiteral("trend_score")), QStringLiteral("Trend score"));
    EXPECT_EQ(vm.selectedIndicatorProfile(), QStringLiteral("trend_score"));
}

TEST(BacktestViewModel, ExposesVolatilityProbeIndicatorWithoutStrategyParams) {
    isolateSettings(QStringLiteral("volatility_probe"));

    hftrec::gui::BacktestViewModel vm;

    EXPECT_TRUE(hasChoiceId(vm.strategyChoices(), QStringLiteral("volatility_probe")));
    vm.setSelectedStrategy(QStringLiteral("volatility_probe"));
    EXPECT_TRUE(vm.strategyParameters().empty());
    EXPECT_EQ(vm.configModeChoices().size(), 1);
    const QVariantList indicators = vm.indicatorProfileChoices();
    ASSERT_EQ(indicators.size(), 1);
    EXPECT_EQ(choiceLabel(indicators, QStringLiteral("volatility_bps")), QStringLiteral("Volatility"));
    EXPECT_EQ(vm.selectedIndicatorProfile(), QStringLiteral("volatility_bps"));
}

TEST(BacktestViewModel, ExposesToxicFlowProbeIndicatorWithoutStrategyParams) {
    isolateSettings(QStringLiteral("toxic_flow_probe"));

    hftrec::gui::BacktestViewModel vm;

    EXPECT_TRUE(hasChoiceId(vm.strategyChoices(), QStringLiteral("toxic_flow_probe")));
    vm.setSelectedStrategy(QStringLiteral("toxic_flow_probe"));
    EXPECT_TRUE(vm.strategyParameters().empty());
    EXPECT_EQ(vm.configModeChoices().size(), 1);
    const QVariantList indicators = vm.indicatorProfileChoices();
    ASSERT_EQ(indicators.size(), 1);
    EXPECT_EQ(choiceLabel(indicators, QStringLiteral("toxic_flow")), QStringLiteral("Toxic flow"));
    EXPECT_EQ(vm.selectedIndicatorProfile(), QStringLiteral("toxic_flow"));
}

TEST(BacktestViewModel, ExposesStatArbBandLadderForTwoSessions) {
    isolateSettings(QStringLiteral("stat_arb_band_ladder"));

    hftrec::gui::BacktestViewModel vm;
    const QString primary = QDir(vm.recordingsRoot()).absoluteFilePath(QStringLiteral("hftrec_stat_arb_primary_%1_%2")
                                                                           .arg(QCoreApplication::applicationPid())
                                                                           .arg(std::rand()));
    const QString secondary = QDir(vm.recordingsRoot()).absoluteFilePath(QStringLiteral("hftrec_stat_arb_secondary_%1_%2")
                                                                             .arg(QCoreApplication::applicationPid())
                                                                             .arg(std::rand()));
    QDir().mkpath(primary);
    QDir().mkpath(secondary);
    writeFile(QDir(primary).absoluteFilePath(QStringLiteral("manifest.json")),
              QByteArrayLiteral("{\"exchange\":\"binance\",\"market\":\"futures\",\"symbols\":\"BTCUSDT\"}"));
    writeFile(QDir(secondary).absoluteFilePath(QStringLiteral("manifest.json")),
              QByteArrayLiteral("{\"exchange\":\"okx\",\"market\":\"futures\",\"symbols\":\"BTCUSDT\"}"));

    vm.setSessionPath(primary);
    vm.setExtraSessionIds(secondary);

    EXPECT_EQ(vm.selectedSessionCount(), 2);
    EXPECT_TRUE(hasChoiceId(vm.strategyChoices(), QStringLiteral("stat_arb_band_ladder")));
    EXPECT_FALSE(hasChoiceId(vm.strategyChoices(), QStringLiteral("spread_maker1and2")));
    EXPECT_EQ(vm.selectedStrategy(), QStringLiteral("stat_arb_band_ladder"));
    EXPECT_FALSE(vm.strategyParameters().empty());
    EXPECT_EQ(vm.configModeChoices().size(), 1);
    EXPECT_TRUE(vm.indicatorProfileChoices().empty());
    EXPECT_TRUE(vm.selectedIndicatorProfile().isEmpty());

    QDir(primary).removeRecursively();
    QDir(secondary).removeRecursively();
}

TEST(BacktestViewModel, ExposesStrategyChoicesFromBacktestMetadata) {
    isolateSettings(QStringLiteral("strategy_discovery"));

    hftrec::gui::BacktestViewModel vm;
    const QVariantList choices = vm.strategyChoices();

    EXPECT_TRUE(hasChoiceId(choices, QStringLiteral("spread_maker1and2")));
    EXPECT_EQ(choiceLabel(choices, QStringLiteral("spread_maker1and2")), QStringLiteral("Спред-мейкер 1/2"));
    EXPECT_TRUE(hasChoiceId(choices, QStringLiteral("trend_probe")));
    EXPECT_EQ(choiceLabel(choices, QStringLiteral("trend_probe")), QStringLiteral("Диагностика тренда"));
    EXPECT_TRUE(hasChoiceId(choices, QStringLiteral("volatility_probe")));
    EXPECT_EQ(choiceLabel(choices, QStringLiteral("volatility_probe")), QStringLiteral("Диагностика волатильности"));
    EXPECT_TRUE(hasChoiceId(choices, QStringLiteral("toxic_flow_probe")));
    EXPECT_EQ(choiceLabel(choices, QStringLiteral("toxic_flow_probe")), QStringLiteral("Диагностика toxic flow"));
    EXPECT_FALSE(hasChoiceId(choices, QStringLiteral("removed_strategy")));
    EXPECT_FALSE(hasChoiceId(choices, QStringLiteral("strategyMD")));
    EXPECT_FALSE(hasChoiceId(choices, QStringLiteral("horizontal_levels")));
    EXPECT_FALSE(hasChoiceId(choices, QStringLiteral("iceberg_detector")));
    EXPECT_FALSE(hasChoiceId(choices, QStringLiteral("liquidity_wall_breakout")));
    EXPECT_FALSE(hasChoiceId(choices, QStringLiteral("liquidity_wall_rebound")));
    EXPECT_FALSE(hasChoiceId(choices, QStringLiteral("liquidity_volume_maker")));
}

TEST(BacktestViewModel, IgnoresPersistedDeletedStrategy) {
    isolateSettings(QStringLiteral("deleted_strategy_setting"));
    QSettings settings;
    settings.setValue(QStringLiteral("backtests/selected_strategy"), QStringLiteral("liquidity_volume_maker"));
    settings.sync();

    hftrec::gui::BacktestViewModel vm;

    EXPECT_EQ(vm.selectedStrategy(), QStringLiteral("spread_maker1and2"));
}

TEST(BacktestViewModel, SessionRowsAreCachedUntilExplicitReload) {
    isolateSettings(QStringLiteral("session_counter"));

    hftrec::gui::BacktestViewModel vm;
    const QString sessionId = QStringLiteral("hftrec_session_counter_%1_%2")
                                  .arg(QCoreApplication::applicationPid())
                                  .arg(std::rand());
    const QString session = QDir(vm.recordingsRoot()).absoluteFilePath(sessionId);
    QDir().mkpath(QDir(session).absoluteFilePath(QStringLiteral("backtests/run-a")));
    QDir().mkpath(QDir(session).absoluteFilePath(QStringLiteral("backtests/run-without-manifest")));
    QDir().mkpath(QDir(session).absoluteFilePath(QStringLiteral("backtests/sweeps/sweep-a")));
    writeFile(QDir(session).absoluteFilePath(QStringLiteral("backtests/run-a/manifest.json")), QByteArrayLiteral("{}"));
    writeFile(QDir(session).absoluteFilePath(QStringLiteral("backtests/sweeps/sweep-a/manifest.json")), QByteArrayLiteral("{}"));

    bool foundBeforeReload = false;
    for (const QVariant& row : vm.sessions()) {
        if (row.toMap().value(QStringLiteral("id")).toString() == sessionId) {
            foundBeforeReload = true;
            break;
        }
    }
    EXPECT_FALSE(foundBeforeReload);

    vm.reloadSessions();

    QVariantMap sessionRow;
    for (const QVariant& row : vm.sessions()) {
        const QVariantMap map = row.toMap();
        if (map.value(QStringLiteral("id")).toString() == sessionId) {
            sessionRow = map;
            break;
        }
    }
    QDir(session).removeRecursively();

    ASSERT_FALSE(sessionRow.isEmpty());
    EXPECT_EQ(sessionRow.value(QStringLiteral("backtestCount")).toInt(), 2);
    EXPECT_TRUE(sessionRow.value(QStringLiteral("rightText")).toString().contains(QStringLiteral("BT 2")));
}

TEST(BacktestViewModel, GroupedSessionRowsUseGroupsAsTreeHeaders) {
    isolateSettings(QStringLiteral("grouped_session_tree_rows"));

    hftrec::gui::BacktestViewModel vm;
    const QString unique = QStringLiteral("TREESEL%1").arg(std::rand());
    const QString groupId = QStringLiteral("2026-06-22_18-25-31_%1").arg(unique);
    const QString groupDir = QDir(vm.recordingsRoot()).absoluteFilePath(groupId);
    const qint64 startNs = 1782141931000000000LL;
    const QString binanceId = QStringLiteral("tree_binance_%1").arg(unique);
    const QString asterId = QStringLiteral("tree_aster_%1").arg(unique);

    writeRecordingManifest(QDir(groupDir).absoluteFilePath(binanceId),
                           binanceId,
                           QStringLiteral("binance"),
                           QStringLiteral("futures"),
                           unique,
                           startNs);
    writeRecordingManifest(QDir(groupDir).absoluteFilePath(asterId),
                           asterId,
                           QStringLiteral("aster"),
                           QStringLiteral("futures"),
                           unique,
                           startNs + 1'000'000LL);

    vm.reloadSessions();

    QVariantMap groupRow;
    QVariantMap binanceRow;
    QVariantMap asterRow;
    for (const QVariant& row : vm.sessions()) {
        const QVariantMap map = row.toMap();
        if (map.value(QStringLiteral("id")).toString() == QStringLiteral("group:%1").arg(groupId)) groupRow = map;
        if (map.value(QStringLiteral("id")).toString() == binanceId) binanceRow = map;
        if (map.value(QStringLiteral("id")).toString() == asterId) asterRow = map;
    }
    QDir(groupDir).removeRecursively();

    ASSERT_FALSE(groupRow.isEmpty());
    EXPECT_TRUE(groupRow.value(QStringLiteral("isGroup")).toBool());
    EXPECT_FALSE(groupRow.value(QStringLiteral("selectable")).toBool());
    EXPECT_EQ(groupRow.value(QStringLiteral("groupId")).toString(), groupId);
    EXPECT_TRUE(groupRow.value(QStringLiteral("label")).toString().contains(unique));

    ASSERT_FALSE(binanceRow.isEmpty());
    EXPECT_FALSE(binanceRow.value(QStringLiteral("isGroup")).toBool());
    EXPECT_TRUE(binanceRow.value(QStringLiteral("selectable")).toBool());
    EXPECT_EQ(binanceRow.value(QStringLiteral("parentGroupId")).toString(), groupId);
    EXPECT_EQ(binanceRow.value(QStringLiteral("label")).toString(), QStringLiteral("binance/futures %1").arg(unique));

    ASSERT_FALSE(asterRow.isEmpty());
    EXPECT_TRUE(asterRow.value(QStringLiteral("selectable")).toBool());
    EXPECT_EQ(asterRow.value(QStringLiteral("parentGroupId")).toString(), groupId);
    EXPECT_EQ(asterRow.value(QStringLiteral("label")).toString(), QStringLiteral("aster/futures %1").arg(unique));
}

TEST(BacktestViewModel, ExplainsWhenSelectedStrategyDoesNotSupportExtraSessions) {
    isolateSettings(QStringLiteral("multi_session_gate"));

    hftrec::gui::BacktestViewModel vm;
    const QString primaryId = QStringLiteral("hftrec_primary_%1_%2")
                                  .arg(QCoreApplication::applicationPid())
                                  .arg(std::rand());
    const QString secondaryId = QStringLiteral("hftrec_secondary_%1_%2")
                                    .arg(QCoreApplication::applicationPid())
                                    .arg(std::rand());
    const QString primary = QDir(vm.recordingsRoot()).absoluteFilePath(primaryId);
    const QString secondary = QDir(vm.recordingsRoot()).absoluteFilePath(secondaryId);
    QDir().mkpath(primary);
    QDir().mkpath(secondary);
    writeFile(QDir(primary).absoluteFilePath(QStringLiteral("manifest.json")),
              QByteArrayLiteral("{\"exchange\":\"binance\",\"market\":\"futures\",\"symbols\":\"BTCUSDT\"}"));
    writeFile(QDir(secondary).absoluteFilePath(QStringLiteral("manifest.json")),
              QByteArrayLiteral("{\"exchange\":\"okx\",\"market\":\"futures\",\"symbols\":\"BTCUSDT\"}"));

    vm.setSessionPath(primary);
    vm.setExtraSessionIds(secondary);
    vm.setSelectedStrategy(QStringLiteral("spread_maker1and2"));

    EXPECT_EQ(vm.selectedSessionCount(), 2);
    EXPECT_FALSE(vm.canRun());
    EXPECT_TRUE(vm.statusText().contains(QStringLiteral("Selected 2 sessions")));
    EXPECT_TRUE(vm.statusText().contains(QStringLiteral("supports 1 session")));

    QDir(primary).removeRecursively();
    QDir(secondary).removeRecursively();
}

TEST(BacktestViewModel, AllowsStatArbBandLadderOnlyForTwoSessions) {
    isolateSettings(QStringLiteral("stat_arb_gate"));

    hftrec::gui::BacktestViewModel vm;
    const QString primaryId = QStringLiteral("hftrec_primary_%1_%2")
                                  .arg(QCoreApplication::applicationPid())
                                  .arg(std::rand());
    const QString secondaryId = QStringLiteral("hftrec_secondary_%1_%2")
                                    .arg(QCoreApplication::applicationPid())
                                    .arg(std::rand());
    const QString primary = QDir(vm.recordingsRoot()).absoluteFilePath(primaryId);
    const QString secondary = QDir(vm.recordingsRoot()).absoluteFilePath(secondaryId);
    QDir().mkpath(primary);
    QDir().mkpath(secondary);
    writeFile(QDir(primary).absoluteFilePath(QStringLiteral("manifest.json")),
              QByteArrayLiteral("{\"exchange\":\"binance\",\"market\":\"futures\",\"symbols\":\"BTCUSDT\"}"));
    writeFile(QDir(secondary).absoluteFilePath(QStringLiteral("manifest.json")),
              QByteArrayLiteral("{\"exchange\":\"okx\",\"market\":\"futures\",\"symbols\":\"ETHUSDT\"}"));

    vm.setSessionPath(primary);

    EXPECT_EQ(vm.selectedSessionCount(), 1);
    EXPECT_TRUE(hasChoiceId(vm.strategyChoices(), QStringLiteral("spread_maker1and2")));
    EXPECT_FALSE(hasChoiceId(vm.strategyChoices(), QStringLiteral("stat_arb_band_ladder")));

    vm.setExtraSessionIds(secondary);

    EXPECT_EQ(vm.selectedSessionCount(), 2);
    EXPECT_TRUE(hasChoiceId(vm.strategyChoices(), QStringLiteral("stat_arb_band_ladder")));
    EXPECT_FALSE(hasChoiceId(vm.strategyChoices(), QStringLiteral("spread_maker1and2")));
    EXPECT_EQ(vm.selectedStrategy(), QStringLiteral("stat_arb_band_ladder"));
    EXPECT_TRUE(vm.canRun());
    EXPECT_FALSE(vm.statusText().contains(QStringLiteral("strategy supports")));
    const QVariantList legs = vm.selectedSessionLegs();
    ASSERT_EQ(legs.size(), 2);
    EXPECT_EQ(legs.at(0).toMap().value(QStringLiteral("index")).toInt(), 0);
    EXPECT_EQ(legs.at(1).toMap().value(QStringLiteral("index")).toInt(), 1);
    EXPECT_EQ(legs.at(0).toMap().value(QStringLiteral("venue")).toString(), QStringLiteral("bybit_futures"));
    EXPECT_EQ(legs.at(1).toMap().value(QStringLiteral("venue")).toString(), QStringLiteral("okx_futures"));

    QDir(primary).removeRecursively();
    QDir(secondary).removeRecursively();
}

TEST(BacktestViewModel, MapsFinamSessionsToFinamVenues) {
    isolateSettings(QStringLiteral("finam_venues"));

    hftrec::gui::BacktestViewModel vm;
    const QString primaryId = QStringLiteral("hftrec_finam_spot_%1_%2")
                                  .arg(QCoreApplication::applicationPid())
                                  .arg(std::rand());
    const QString secondaryId = QStringLiteral("hftrec_finam_futures_%1_%2")
                                    .arg(QCoreApplication::applicationPid())
                                    .arg(std::rand());
    const QString primary = QDir(vm.recordingsRoot()).absoluteFilePath(primaryId);
    const QString secondary = QDir(vm.recordingsRoot()).absoluteFilePath(secondaryId);
    QDir().mkpath(primary);
    QDir().mkpath(secondary);
    writeFile(QDir(primary).absoluteFilePath(QStringLiteral("manifest.json")),
              QByteArrayLiteral("{\"identity\":{\"exchange\":\"finam\",\"market\":\"spot\",\"symbols\":[\"SBER@MISX\"]}}"));
    writeFile(QDir(secondary).absoluteFilePath(QStringLiteral("manifest.json")),
              QByteArrayLiteral("{\"identity\":{\"exchange\":\"finam\",\"market\":\"futures\",\"symbols\":[\"SRU6@RTSX\"]}}"));

    vm.setSessionPath(primary);
    vm.setExtraSessionIds(secondary);

    const QVariantList legs = vm.selectedSessionLegs();
    ASSERT_EQ(legs.size(), 2);
    EXPECT_EQ(legs.at(0).toMap().value(QStringLiteral("venue")).toString(), QStringLiteral("finam_spot"));
    EXPECT_EQ(legs.at(0).toMap().value(QStringLiteral("symbol")).toString(), QStringLiteral("SBER@MISX"));
    EXPECT_EQ(legs.at(1).toMap().value(QStringLiteral("venue")).toString(), QStringLiteral("finam_futures"));
    EXPECT_EQ(legs.at(1).toMap().value(QStringLiteral("symbol")).toString(), QStringLiteral("SRU6@RTSX"));

    QDir(primary).removeRecursively();
    QDir(secondary).removeRecursively();
}

TEST(BacktestViewModel, StoresVenueLatencyValuesPerExchangeMarketAndShowsPresetSummary) {
    isolateSettings(QStringLiteral("venue_execution_values"));

    hftrec::gui::BacktestViewModel vm;
    const QString primaryId = QStringLiteral("hftrec_fee_primary_%1_%2")
                                  .arg(QCoreApplication::applicationPid())
                                  .arg(std::rand());
    const QString secondaryId = QStringLiteral("hftrec_fee_secondary_%1_%2")
                                    .arg(QCoreApplication::applicationPid())
                                    .arg(std::rand());
    const QString primary = QDir(vm.recordingsRoot()).absoluteFilePath(primaryId);
    const QString secondary = QDir(vm.recordingsRoot()).absoluteFilePath(secondaryId);
    QDir().mkpath(primary);
    QDir().mkpath(secondary);
    writeFile(QDir(primary).absoluteFilePath(QStringLiteral("manifest.json")),
              QByteArrayLiteral("{\"exchange\":\"okx\",\"market\":\"futures\",\"symbols\":\"BTCUSDT\"}"));
    writeFile(QDir(secondary).absoluteFilePath(QStringLiteral("manifest.json")),
              QByteArrayLiteral("{\"exchange\":\"bybit\",\"market\":\"futures\",\"symbols\":\"ETHUSDT\"}"));

    vm.setSessionPath(primary);
    vm.setExtraSessionIds(secondary);
    vm.setVenueExecutionValue(0, QStringLiteral("market_data_latency_us"), QStringLiteral("111"));
    vm.setVenueExecutionValue(1, QStringLiteral("market_data_latency_us"), QStringLiteral("333"));

    QVariantList rows = vm.selectedSessionLegs();
    ASSERT_EQ(rows.size(), 2);
    EXPECT_EQ(rows.at(0).toMap().value(QStringLiteral("exchange")).toString(), QStringLiteral("okx"));
    EXPECT_FALSE(rows.at(0).toMap().contains(QStringLiteral("makerFeeBps")));
    EXPECT_FALSE(rows.at(0).toMap().contains(QStringLiteral("takerFeeBps")));
    EXPECT_TRUE(rows.at(0).toMap().value(QStringLiteral("executionPresetSummary")).toString().contains(QStringLiteral("Fees M/T")));
    EXPECT_EQ(rows.at(0).toMap().value(QStringLiteral("marketDataLatencyUs")).toString(), QStringLiteral("111"));
    EXPECT_EQ(rows.at(1).toMap().value(QStringLiteral("exchange")).toString(), QStringLiteral("bybit"));
    EXPECT_FALSE(rows.at(1).toMap().contains(QStringLiteral("makerFeeBps")));
    EXPECT_FALSE(rows.at(1).toMap().contains(QStringLiteral("takerFeeBps")));
    EXPECT_TRUE(rows.at(1).toMap().value(QStringLiteral("executionPresetSummary")).toString().contains(QStringLiteral("RL")));
    EXPECT_EQ(rows.at(1).toMap().value(QStringLiteral("marketDataLatencyUs")).toString(), QStringLiteral("333"));

    vm.setVenueExecutionValue(0, QStringLiteral("maker_fee_bps"), QStringLiteral("0.7"));
    vm.setVenueExecutionValue(0, QStringLiteral("taker_fee_bps"), QStringLiteral("1.1"));
    rows = vm.selectedSessionLegs();
    ASSERT_EQ(rows.size(), 2);
    EXPECT_EQ(rows.at(0).toMap().value(QStringLiteral("makerFeeBps")).toString(), QStringLiteral("0.7"));
    EXPECT_EQ(rows.at(0).toMap().value(QStringLiteral("takerFeeBps")).toString(), QStringLiteral("1.1"));
    EXPECT_FALSE(rows.at(1).toMap().contains(QStringLiteral("makerFeeBps")));

    hftrec::gui::BacktestViewModel restored;
    restored.setSessionPath(primary);
    restored.setExtraSessionIds(secondary);
    rows = restored.selectedSessionLegs();
    ASSERT_EQ(rows.size(), 2);
    EXPECT_EQ(rows.at(1).toMap().value(QStringLiteral("marketDataLatencyUs")).toString(), QStringLiteral("333"));
    EXPECT_EQ(rows.at(0).toMap().value(QStringLiteral("makerFeeBps")).toString(), QStringLiteral("0.7"));
    EXPECT_EQ(rows.at(0).toMap().value(QStringLiteral("takerFeeBps")).toString(), QStringLiteral("1.1"));

    QDir(primary).removeRecursively();
    QDir(secondary).removeRecursively();
}

TEST(BacktestViewModel, ExposesSweepDistributionBarsGroupedBySelectedParameter) {
    isolateSettings(QStringLiteral("sweep_distribution"));
    const QString session = makeTempSessionDir();
    const QString sweepDir = QDir(session).absoluteFilePath(QStringLiteral("backtests/sweeps/sweep-dist"));
    QDir().mkpath(sweepDir);
    writeFile(QDir(sweepDir).absoluteFilePath(QStringLiteral("manifest.json")), R"json({
      "type":"sweep.result.v1",
      "sweep_id":"sweep-dist",
      "strategy":"spread_maker1and2",
      "budget":4,
      "search_seed":0,
      "points_evaluated":4,
      "rows":{"path":"sweep_results.jsonl","row_schema":"object.v1"},
      "curves":{"path":"sweep_curves.jsonl","row_schema":"object.v1"},
      "ranges":[
        {"key":"close_delay_us","mode":"grid","min_raw":100,"max_raw":200,"step_raw":100},
        {"key":"distance_bps","mode":"grid","min_raw":10,"max_raw":20,"step_raw":10}
      ],
      "errors":[]
    })json");
    writeFile(QDir(sweepDir).absoluteFilePath(QStringLiteral("sweep_results.jsonl")), QByteArrayLiteral(
        "{\"point_id\":1,\"params\":{\"close_delay_us\":100,\"distance_bps\":10},\"status\":\"Ok\",\"total_pnl_e8\":-100000000,\"legs\":[{\"leg_index\":0,\"exchange\":\"binance\",\"symbol\":\"BTCUSDT\",\"initial_balance_e8\":10000000000,\"total_pnl_e8\":50000000},{\"leg_index\":1,\"exchange\":\"okx\",\"symbol\":\"ETHUSDT\",\"initial_balance_e8\":20000000000,\"total_pnl_e8\":-150000000}]}\n"
        "{\"point_id\":2,\"params\":{\"close_delay_us\":100,\"distance_bps\":20},\"status\":\"Ok\",\"total_pnl_e8\":300000000,\"legs\":[{\"leg_index\":0,\"exchange\":\"binance\",\"symbol\":\"BTCUSDT\",\"initial_balance_e8\":10000000000,\"total_pnl_e8\":100000000},{\"leg_index\":1,\"exchange\":\"okx\",\"symbol\":\"ETHUSDT\",\"initial_balance_e8\":20000000000,\"total_pnl_e8\":200000000}]}\n"
        "{\"point_id\":3,\"params\":{\"close_delay_us\":200,\"distance_bps\":10},\"status\":\"Ok\",\"total_pnl_e8\":200000000,\"legs\":[{\"leg_index\":0,\"exchange\":\"binance\",\"symbol\":\"BTCUSDT\",\"initial_balance_e8\":10000000000,\"total_pnl_e8\":-200000000},{\"leg_index\":1,\"exchange\":\"okx\",\"symbol\":\"ETHUSDT\",\"initial_balance_e8\":20000000000,\"total_pnl_e8\":400000000}]}\n"
        "{\"point_id\":4,\"params\":{\"close_delay_us\":200,\"distance_bps\":20},\"status\":\"Ok\",\"total_pnl_e8\":-50000000,\"legs\":[{\"leg_index\":0,\"exchange\":\"binance\",\"symbol\":\"BTCUSDT\",\"initial_balance_e8\":10000000000,\"total_pnl_e8\":0},{\"leg_index\":1,\"exchange\":\"okx\",\"symbol\":\"ETHUSDT\",\"initial_balance_e8\":20000000000,\"total_pnl_e8\":-50000000}]}\n"));
    writeFile(QDir(sweepDir).absoluteFilePath(QStringLiteral("sweep_curves.jsonl")), QByteArrayLiteral(
        "{\"point_id\":2,\"params\":{\"close_delay_us\":100,\"distance_bps\":20},\"status\":\"Ok\",\"initial_balance_e8\":30000000000,\"total_pnl_e8\":300000000,\"curve_e8\":[0,300000000],\"legs\":[{\"leg_index\":0,\"exchange\":\"binance\",\"symbol\":\"BTCUSDT\",\"initial_balance_e8\":10000000000,\"total_pnl_e8\":100000000,\"curve_e8\":[0,100000000]},{\"leg_index\":1,\"exchange\":\"okx\",\"symbol\":\"ETHUSDT\",\"initial_balance_e8\":20000000000,\"total_pnl_e8\":200000000,\"curve_e8\":[0,200000000]}]}\n"
        "{\"point_id\":3,\"params\":{\"close_delay_us\":200,\"distance_bps\":10},\"status\":\"Ok\",\"initial_balance_e8\":30000000000,\"total_pnl_e8\":200000000,\"curve_e8\":[0,200000000],\"legs\":[{\"leg_index\":0,\"exchange\":\"binance\",\"symbol\":\"BTCUSDT\",\"initial_balance_e8\":10000000000,\"total_pnl_e8\":-200000000,\"curve_e8\":[0,-200000000]},{\"leg_index\":1,\"exchange\":\"okx\",\"symbol\":\"ETHUSDT\",\"initial_balance_e8\":20000000000,\"total_pnl_e8\":400000000,\"curve_e8\":[0,400000000]}]}\n"));

    hftrec::gui::BacktestViewModel vm;
    vm.setSessionPath(session);
    vm.selectRun(QStringLiteral("sweep-dist"));

    EXPECT_TRUE(hasChoiceId(vm.sweepViewChoices(), QStringLiteral("distribution")));
    EXPECT_EQ(vm.selectedSweepMetric(), QStringLiteral("total_pnl_e8"));
    EXPECT_FALSE(vm.selectedDetailsLoaded());
    EXPECT_TRUE(vm.selectedSweepDistributionParamChoices().empty());
    EXPECT_TRUE(vm.selectedSweepDistributionBars().empty());

    vm.loadSelectedRunDetails();
    waitForDetailsLoad(vm);

    ASSERT_TRUE(vm.selectedDetailsLoaded());
    EXPECT_TRUE(hasChoiceId(vm.sweepMetricChoices(), QStringLiteral("leg_0_total_pnl_e8")));
    EXPECT_TRUE(hasChoiceId(vm.sweepMetricChoices(), QStringLiteral("leg_1_total_pnl_e8")));
    ASSERT_EQ(vm.selectedSweepDistributionParamChoices().size(), 2);
    EXPECT_EQ(vm.selectedSweepDistributionParamChoices().at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("close_delay_us"));
    EXPECT_EQ(vm.selectedSweepDistributionParam(), QStringLiteral("close_delay_us"));

    const QVariantList bars = vm.selectedSweepDistributionBars();
    ASSERT_EQ(bars.size(), 4);
    EXPECT_EQ(bars.at(0).toMap().value(QStringLiteral("pointId")).toInt(), 2);
    EXPECT_EQ(bars.at(0).toMap().value(QStringLiteral("paramRaw")).toLongLong(), 100);
    EXPECT_EQ(bars.at(0).toMap().value(QStringLiteral("metricRaw")).toLongLong(), 300000000ll);
    EXPECT_EQ(bars.at(1).toMap().value(QStringLiteral("pointId")).toInt(), 1);
    EXPECT_EQ(bars.at(1).toMap().value(QStringLiteral("paramRaw")).toLongLong(), 100);
    EXPECT_EQ(bars.at(2).toMap().value(QStringLiteral("pointId")).toInt(), 3);
    EXPECT_EQ(bars.at(2).toMap().value(QStringLiteral("paramRaw")).toLongLong(), 200);
    EXPECT_EQ(bars.at(3).toMap().value(QStringLiteral("pointId")).toInt(), 4);
    EXPECT_EQ(bars.at(3).toMap().value(QStringLiteral("metricText")).toString(), QStringLiteral("-0.5"));

    vm.setSelectedSweepMetric(QStringLiteral("leg_1_total_pnl_e8"));

    const QVariantList legRows = vm.selectedSweepRows();
    ASSERT_EQ(legRows.size(), 4);
    EXPECT_EQ(legRows.at(0).toMap().value(QStringLiteral("pointId")).toInt(), 3);
    EXPECT_EQ(legRows.at(0).toMap().value(QStringLiteral("metricRaw")).toLongLong(), 400000000ll);
    EXPECT_TRUE(legRows.at(0).toMap().value(QStringLiteral("metricLabel")).toString().contains(QStringLiteral("okx")));
    const QVariantList legCurves = vm.selectedSweepCurves();
    ASSERT_EQ(legCurves.size(), 2);
    EXPECT_EQ(legCurves.at(0).toMap().value(QStringLiteral("pointId")).toInt(), 3);
    EXPECT_EQ(legCurves.at(0).toMap().value(QStringLiteral("totalPnlE8")).toLongLong(), 400000000ll);
    EXPECT_EQ(legCurves.at(0).toMap().value(QStringLiteral("initialBalanceE8")).toLongLong(), 20000000000ll);
    EXPECT_EQ(legCurves.at(0).toMap().value(QStringLiteral("curve")).toList().at(1).toLongLong(), 400000000ll);

    vm.setSelectedSweepMetric(QStringLiteral("total_pnl_e8"));
    vm.setSelectedSweepDistributionParam(QStringLiteral("distance_bps"));
    const QVariantList distanceBars = vm.selectedSweepDistributionBars();
    ASSERT_EQ(distanceBars.size(), 4);
    EXPECT_EQ(distanceBars.at(0).toMap().value(QStringLiteral("paramRaw")).toLongLong(), 10);
    EXPECT_EQ(distanceBars.at(1).toMap().value(QStringLiteral("paramRaw")).toLongLong(), 10);
    EXPECT_EQ(distanceBars.at(2).toMap().value(QStringLiteral("paramRaw")).toLongLong(), 20);
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
        vm.setLatencySeed(QStringLiteral("42"));
        vm.setMarketDataLatencyUs(QStringLiteral("250"));
        vm.setMarketDataJitterUs(QStringLiteral("100"));
        vm.setMarketOrderLatencyUs(QStringLiteral("2500"));
        vm.setMarketOrderJitterUs(QStringLiteral("1000"));
        vm.setLimitOrderLatencyUs(QStringLiteral("1800"));
        vm.setLimitOrderJitterUs(QStringLiteral("700"));
        vm.setInitialBalanceUsdt(QStringLiteral("750.25"));
        vm.setStrategyParameter(QStringLiteral("distance_bps"), QStringLiteral("700"));
    }

    hftrec::gui::BacktestViewModel restored;
    EXPECT_EQ(restored.selectedStrategy(), QStringLiteral("spread_maker1and2"));
    EXPECT_EQ(restored.configMode(), QStringLiteral("fixed"));
    EXPECT_EQ(restored.pingLatencyUs(), QStringLiteral("2500"));
    EXPECT_EQ(restored.latencySeed(), QStringLiteral("42"));
    EXPECT_EQ(restored.marketDataLatencyUs(), QStringLiteral("250"));
    EXPECT_EQ(restored.marketDataJitterUs(), QStringLiteral("100"));
    EXPECT_EQ(restored.marketOrderLatencyUs(), QStringLiteral("2500"));
    EXPECT_EQ(restored.marketOrderJitterUs(), QStringLiteral("1000"));
    EXPECT_EQ(restored.limitOrderLatencyUs(), QStringLiteral("1800"));
    EXPECT_EQ(restored.limitOrderJitterUs(), QStringLiteral("700"));
    EXPECT_EQ(restored.initialBalanceUsdt(), QStringLiteral("750.25"));
    EXPECT_EQ(restored.makerFeeBps(), QStringLiteral("0"));
    EXPECT_EQ(restored.takerFeeBps(), QStringLiteral("0"));
    EXPECT_TRUE(hasParamKey(restored.strategyParameters(), QStringLiteral("distance_bps")));
    EXPECT_NE(restored.sessionPath(), session);
}

