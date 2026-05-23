#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
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

}  // namespace

TEST(BacktestViewModel, LoadsValidResultAndSummary) {
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
    const QString session = makeTempSessionDir();
    writeFile(QDir(session).absoluteFilePath(QStringLiteral("backtests/bad.json")), QByteArrayLiteral("{"));

    hftrec::gui::BacktestViewModel vm;
    vm.setSessionPath(session);

    ASSERT_EQ(vm.runCount(), 1);
    EXPECT_EQ(vm.selectedRunId(), QStringLiteral("bad"));
    EXPECT_FALSE(vm.selectedErrorText().isEmpty());
    EXPECT_TRUE(vm.selectedJson().contains(QStringLiteral("{")));
}

