#include <gtest/gtest.h>

#include <QByteArray>
#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QObject>
#include <QSettings>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <atomic>
#include <cstdlib>

#include "gui/backtests/BacktestViewModel.hpp"

namespace {

void ensureCoreApplication() {
    static int argc = 1;
    static char appName[] = "hftrec_backtest_leg_selection_tests";
    static char* argv[] = {appName, nullptr};
    static QCoreApplication* app = nullptr;
    if (QCoreApplication::instance() == nullptr) {
        app = new QCoreApplication(argc, argv);
    }
    (void)app;
}

void isolateSettings(QStringView suffix) {
    ensureCoreApplication();
    static std::atomic<unsigned> counter{0};
    const unsigned id = counter.fetch_add(1, std::memory_order_relaxed);
    const QString root = QDir::temp().absoluteFilePath(QStringLiteral("hftrec_leg_selection_settings_%1_%2")
                                                           .arg(QCoreApplication::applicationPid())
                                                           .arg(id));
    const QString recordingsRoot = QDir::temp().absoluteFilePath(QStringLiteral("hftrec_leg_selection_recordings_%1_%2")
                                                                     .arg(QCoreApplication::applicationPid())
                                                                     .arg(id));
    QDir().mkpath(root);
    QDir().mkpath(recordingsRoot);
    const QByteArray recordingsRootBytes = recordingsRoot.toLocal8Bit();
    setenv("HFTREC_RECORDINGS_ROOT", recordingsRootBytes.constData(), 1);
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, root);
    QCoreApplication::setOrganizationName(QStringLiteral("hftrec_backtest_leg_selection_tests"));
    QCoreApplication::setApplicationName(QStringLiteral("case_%1_%2").arg(suffix, QString::number(std::rand())));
    QSettings settings;
    settings.clear();
    settings.sync();
}

QString makeTempSessionDir(const QString& suffix) {
    const QString path = QDir::temp().absoluteFilePath(QStringLiteral("hftrec_leg_selection_%1_%2_%3")
                                                           .arg(QCoreApplication::applicationPid())
                                                           .arg(QDateTime::currentMSecsSinceEpoch())
                                                           .arg(suffix));
    QDir(path).removeRecursively();
    QDir().mkpath(path);
    return path;
}

void writeFile(const QString& path, const QByteArray& data) {
    QFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(file.write(data), data.size());
}

void writeManifest(const QString& sessionDir, const QString& exchange, const QString& market, const QString& symbol) {
    writeFile(QDir(sessionDir).absoluteFilePath(QStringLiteral("manifest.json")),
              QStringLiteral(R"json({"exchange":"%1","market":"%2","symbols":"%3"})json")
                  .arg(exchange, market, symbol)
                  .toUtf8());
}

QVariantMap rowForPath(const QVariantList& rows, const QString& path) {
    for (const QVariant& row : rows) {
        const QVariantMap map = row.toMap();
        if (map.value(QStringLiteral("path")).toString() == path) return map;
    }
    return {};
}

}  // namespace

TEST(BacktestLegSelection, StagedChangesDoNotPublishCommittedSelection) {
    isolateSettings(QStringLiteral("staged_signal_boundary"));

    const QString primary = makeTempSessionDir(QStringLiteral("primary"));
    const QString secondary = makeTempSessionDir(QStringLiteral("secondary"));
    writeManifest(primary, QStringLiteral("binance"), QStringLiteral("futures"), QStringLiteral("BTC_USDT"));
    writeManifest(secondary, QStringLiteral("okx"), QStringLiteral("futures"), QStringLiteral("ETH_USDT"));

    hftrec::gui::BacktestViewModel vm;
    vm.reloadSessions();
    vm.setSessionPath(primary);
    vm.setExtraSessionIds(secondary);
    ASSERT_EQ(vm.selectedSessionCount(), 2);

    int legSelectionSignals = 0;
    QObject::connect(&vm, &hftrec::gui::BacktestViewModel::legSelectionChanged, [&legSelectionSignals]() {
        ++legSelectionSignals;
    });

    vm.setSessionLegSelectionStaged(secondary, false);

    EXPECT_EQ(legSelectionSignals, 0);
    EXPECT_EQ(vm.selectedSessionCount(), 2);
    const QVariantMap stagedSecondary = rowForPath(vm.legSelectionRows(), secondary);
    ASSERT_FALSE(stagedSecondary.isEmpty());
    EXPECT_TRUE(stagedSecondary.value(QStringLiteral("enabled")).toBool());
    EXPECT_FALSE(stagedSecondary.value(QStringLiteral("stagedEnabled")).toBool());

    vm.applyLegSelection();

    EXPECT_GE(legSelectionSignals, 1);
    EXPECT_EQ(vm.selectedSessionCount(), 1);
    const QVariantMap appliedSecondary = rowForPath(vm.legSelectionRows(), secondary);
    ASSERT_FALSE(appliedSecondary.isEmpty());
    EXPECT_FALSE(appliedSecondary.value(QStringLiteral("enabled")).toBool());
    EXPECT_FALSE(appliedSecondary.value(QStringLiteral("stagedEnabled")).toBool());

    QDir(primary).removeRecursively();
    QDir(secondary).removeRecursively();
}

TEST(BacktestLegSelection, PopupRowsApplyAsExactStagedSelection) {
    isolateSettings(QStringLiteral("popup_rows_source_of_truth"));

    const QString primary = makeTempSessionDir(QStringLiteral("primary"));
    const QString secondary = makeTempSessionDir(QStringLiteral("secondary"));
    const QString mexc = makeTempSessionDir(QStringLiteral("mexc"));
    writeManifest(primary, QStringLiteral("binance"), QStringLiteral("futures"), QStringLiteral("BTC_USDT"));
    writeManifest(secondary, QStringLiteral("okx"), QStringLiteral("futures"), QStringLiteral("BTC_USDT"));
    writeManifest(mexc, QStringLiteral("mexc"), QStringLiteral("futures"), QStringLiteral("BTC_USDT"));

    hftrec::gui::BacktestViewModel vm;
    vm.reloadSessions();
    vm.setSessionPath(primary);
    vm.setExtraSessionIds(QStringList{secondary, mexc}.join(QLatin1Char(',')));
    ASSERT_EQ(vm.selectedSessionCount(), 3);

    QVariantList popupRows = vm.legSelectionRows();
    bool foundMexc = false;
    for (QVariant& value : popupRows) {
        QVariantMap row = value.toMap();
        if (row.value(QStringLiteral("path")).toString() != mexc) continue;
        row.insert(QStringLiteral("stagedEnabled"), false);
        value = row;
        foundMexc = true;
    }
    ASSERT_TRUE(foundMexc);

    vm.setLegSelectionRowsStaged(popupRows);

    EXPECT_EQ(vm.selectedSessionCount(), 3);
    EXPECT_FALSE(rowForPath(vm.legSelectionRows(), mexc).value(QStringLiteral("stagedEnabled")).toBool());

    vm.applyLegSelection();

    EXPECT_EQ(vm.selectedSessionCount(), 2);
    EXPECT_FALSE(rowForPath(vm.legSelectionRows(), mexc).value(QStringLiteral("enabled")).toBool());
    EXPECT_TRUE(rowForPath(vm.legSelectionRows(), primary).value(QStringLiteral("enabled")).toBool());
    EXPECT_TRUE(rowForPath(vm.legSelectionRows(), secondary).value(QStringLiteral("enabled")).toBool());

    QDir(primary).removeRecursively();
    QDir(secondary).removeRecursively();
    QDir(mexc).removeRecursively();
}
