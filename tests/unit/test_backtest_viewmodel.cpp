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

