#include <gtest/gtest.h>

#include <cstdlib>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include <QString>

#include "gui/viewer/ChartController.hpp"

namespace fs = std::filesystem;

namespace {

fs::path fixtureDir(const char* name) {
    return fs::path(HFTRREC_SOURCE_DIR) / "tests" / "fixtures" / "session_corpus" / name;
}

fs::path makeTmpDir(const char* prefix) {
    const auto dir = fs::temp_directory_path() / (std::string{prefix} + "_" + std::to_string(std::rand()));
    fs::create_directories(dir);
    return dir;
}

void writeFile(const fs::path& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << data;
}

std::string tradeLine(std::int64_t tsNs, std::int64_t priceE8) {
    return "[" + std::to_string(priceE8)
        + ",100000000,1," + std::to_string(tsNs) + "]\n";
}

class CountingLiveDataProvider final : public hftrec::gui::viewer::ILiveDataProvider {
  public:
    void start(const hftrec::gui::viewer::LiveDataProviderConfig&) override { ++startCount; }
    void stop() noexcept override { ++stopCount; }
    hftrec::gui::viewer::LiveDataPollResult pollHot(std::uint64_t) override { return {}; }
    hftrec::gui::viewer::LiveDataBatch materializeRange(const hftrec::gui::viewer::LiveDataRangeRequest&,
                                                        std::uint64_t) const override {
        ++materializeCount;
        return {};
    }
    hftrec::gui::viewer::LiveDataStats stats() const noexcept override { return {}; }

    int startCount{0};
    int stopCount{0};
    mutable int materializeCount{0};
};

}  // namespace

TEST(RecordedRenderOnce, LoadSessionMaterializesRowsAndDoesNotStartLiveProvider) {
    auto provider = std::make_unique<CountingLiveDataProvider>();
    auto* providerPtr = provider.get();

    hftrec::gui::viewer::ChartController chart;
    chart.setLiveDataProvider(std::move(provider));

    ASSERT_TRUE(chart.loadSession(QString::fromStdString(fixtureDir("clean_full").string())));
    EXPECT_TRUE(chart.loaded());
    EXPECT_EQ(chart.replay().trades().size(), 1u);
    EXPECT_EQ(chart.replay().bookTickers().size(), 1u);
    EXPECT_EQ(chart.replay().depths().size(), 2u);
    EXPECT_EQ(providerPtr->startCount, 0);
    chart.refreshLiveDataWindow(0, 4000);
    EXPECT_EQ(providerPtr->materializeCount, 0);
}

TEST(ViewerTradeRendering, DenseTradesRemainExactDots) {
    const auto dir = makeTmpDir("hftrec_dense_trades_exact");
    std::string trades;
    trades.reserve(24002u * 32u);
    for (std::int64_t ts = 1; ts <= 24002; ++ts) {
        trades += tradeLine(ts, 10000000000ll);
    }
    writeFile(dir / "trades.jsonl", trades);

    hftrec::gui::viewer::ChartController chart;
    ASSERT_TRUE(chart.addTradesFile(QString::fromStdString((dir / "trades.jsonl").string())));
    chart.finalizeFiles();
    ASSERT_TRUE(chart.loaded());
    chart.setViewport(1, 24002, 9900000000ll, 10100000000ll);

    hftrec::gui::viewer::SnapshotInputs inputs{};
    inputs.tradesVisible = true;
    const auto snap = chart.buildSnapshot(10.0, 200.0, inputs);
    EXPECT_FALSE(snap.tradeDecimated);
    EXPECT_EQ(snap.tradeDots.size(), 24002u);
    EXPECT_FALSE(snap.tradeDots.front().aggregated);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(LiveTailProvider, PollHotPublishesNewSnapshotAfterStart) {
    const auto dir = makeTmpDir("hftrec_live_tail_snapshot");
    writeFile(dir / "snapshot_000.json", "[[9900000000,100000000,1],100]\n");

    hftrec::gui::viewer::JsonTailLiveDataProvider provider;
    provider.start(hftrec::gui::viewer::LiveDataProviderConfig{dir, {}, {}});
    EXPECT_EQ(provider.stats().snapshotsTotal, 1u);
    EXPECT_TRUE(provider.pollHot(1u).batch.snapshots.empty());

    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    writeFile(dir / "snapshot_001.json", "[[9900000000,100000000,1],200]\n");
    const auto poll = provider.pollHot(2u);
    ASSERT_EQ(poll.batch.snapshots.size(), 1u);
    EXPECT_EQ(poll.batch.snapshots.front().tsNs, 200);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(ViewerBacktestResults, DiscoversRunDirectoriesAndVisibleSweepsOnly) {
    const auto session = makeTmpDir("hftrec_viewer_backtest_results");
    fs::create_directories(session / "backtests");
    fs::create_directories(session / "backtests" / "run-a");
    fs::create_directories(session / "backtests" / "sweeps" / "sweep-a");
    writeFile(session / "backtests" / "run-a" / "manifest.json", R"json({"type":"run.result.v2","run_id":"run-a","summary":{},"errors":[]})json");
    writeFile(session / "backtests" / "sweeps" / "sweep-a" / "manifest.json", R"json({"type":"sweep.result.v1","sweep_id":"sweep-a","summary":{},"errors":[]})json");
    writeFile(session / "backtests" / "legacy.json", R"json({"type":"run.result","run_id":"legacy","orders":[],"fills":[],"summary":{},"errors":[]})json");

    hftrec::gui::viewer::ChartController chart;
    chart.refreshBacktestResults(QString::fromStdString(session.string()));

    ASSERT_EQ(chart.backtestResults().size(), 2);
    const QVariantMap firstRow = chart.backtestResults().at(0).toMap();
    const QVariantMap secondRow = chart.backtestResults().at(1).toMap();
    EXPECT_TRUE(firstRow.value(QStringLiteral("path")).toString().endsWith(QStringLiteral("run-a")));
    EXPECT_TRUE(secondRow.value(QStringLiteral("path")).toString().endsWith(QStringLiteral("sweep-a")));
    EXPECT_TRUE(firstRow.value(QStringLiteral("selectable")).toBool());
    EXPECT_FALSE(secondRow.value(QStringLiteral("selectable")).toBool());
    EXPECT_FALSE(firstRow.value(QStringLiteral("path")).toString().endsWith(QStringLiteral("legacy.json")));
    EXPECT_FALSE(secondRow.value(QStringLiteral("path")).toString().endsWith(QStringLiteral("legacy.json")));

    std::error_code ec;
    fs::remove_all(session, ec);
}

TEST(ViewerBacktestResults, DiscoversTwoLegRunWhenSelectedSessionsAreSwapped) {
    const auto sessionA = makeTmpDir("hftrec_viewer_backtest_pair_a");
    const auto sessionB = makeTmpDir("hftrec_viewer_backtest_pair_b");
    const auto runDir = sessionA / "backtests" / "run-ab";
    fs::create_directories(runDir);
    const QString manifest = QStringLiteral(R"json({
        "type":"run.result.v2",
        "run_id":"run-ab",
        "strategy":"stat_arb_band_ladder",
        "summary":{},
        "errors":[],
        "legs":[
            {"leg_index":0,"session_path":"%1"},
            {"leg_index":1,"session_path":"%2"}
        ]
    })json").arg(QString::fromStdString(sessionA.string()), QString::fromStdString(sessionB.string()));
    writeFile(runDir / "manifest.json", manifest.toStdString());

    hftrec::gui::viewer::ChartController chart;
    chart.refreshBacktestResults(QString::fromStdString(sessionB.string()), QString::fromStdString(sessionA.string()));

    ASSERT_EQ(chart.backtestResults().size(), 1);
    EXPECT_TRUE(chart.backtestResults().at(0).toMap().value(QStringLiteral("path")).toString().endsWith(QStringLiteral("run-ab")));

    std::error_code ec;
    fs::remove_all(sessionA, ec);
    fs::remove_all(sessionB, ec);
}

TEST(ViewerBacktestResults, HidesTwoLegSiblingRunUntilBothSessionsAreSelected) {
    const auto tempRoot = makeTmpDir("hftrec_viewer_backtest_sibling_root");
    const auto root = tempRoot / "recordings";
    const auto sessionA = root / "session-a";
    const auto sessionB = root / "session-b";
    fs::create_directories(sessionA);
    fs::create_directories(sessionB);
    const auto runDir = sessionA / "backtests" / "run-ab";
    fs::create_directories(runDir);
    const QString manifest = QStringLiteral(R"json({
        "type":"run.result.v2",
        "run_id":"run-ab",
        "strategy":"stat_arb_band_ladder",
        "summary":{},
        "errors":[],
        "legs":[
            {"leg_index":0,"session_path":"%1"},
            {"leg_index":1,"session_path":"%2"}
        ]
    })json").arg(QString::fromStdString(sessionA.string()), QString::fromStdString(sessionB.string()));
    writeFile(runDir / "manifest.json", manifest.toStdString());

    hftrec::gui::viewer::ChartController chart;
    chart.refreshBacktestResults(QString::fromStdString(sessionB.string()));

    EXPECT_EQ(chart.backtestResults().size(), 0);

    chart.refreshBacktestResults(QString::fromStdString(sessionB.string()), QString::fromStdString(sessionA.string()));

    ASSERT_EQ(chart.backtestResults().size(), 1);
    const QVariantMap row = chart.backtestResults().at(0).toMap();
    EXPECT_TRUE(row.value(QStringLiteral("path")).toString().endsWith(QStringLiteral("run-ab")));
    EXPECT_EQ(row.value(QStringLiteral("sessionPath")).toString(), QString::fromStdString(sessionA.string()));

    std::error_code ec;
    fs::remove_all(tempRoot, ec);
}

TEST(ViewerBacktestResults, DiscoversSingleLegRunUnderSelectedRecordingSession) {
    const auto tempRoot = makeTmpDir("hftrec_viewer_backtest_single_root");
    const auto root = tempRoot / "recordings";
    const auto session = root / "session-a";
    const auto runDir = session / "backtests" / "run-a";
    fs::create_directories(runDir);
    const QString manifest = QStringLiteral(R"json({
        "type":"run.result.v2",
        "run_id":"run-a",
        "strategy":"spread_maker1and2",
        "session_path":"%1",
        "summary":{},
        "errors":[]
    })json").arg(QString::fromStdString(session.string()));
    writeFile(runDir / "manifest.json", manifest.toStdString());

    hftrec::gui::viewer::ChartController chart;
    chart.refreshBacktestResults(QString::fromStdString(session.string()));

    ASSERT_EQ(chart.backtestResults().size(), 1);
    const QVariantMap row = chart.backtestResults().at(0).toMap();
    EXPECT_TRUE(row.value(QStringLiteral("path")).toString().endsWith(QStringLiteral("run-a")));
    EXPECT_EQ(row.value(QStringLiteral("sessionPath")).toString(), QString::fromStdString(session.string()));

    std::error_code ec;
    fs::remove_all(tempRoot, ec);
}

TEST(ViewerBacktestResults, SelectsDiscoveredRunResultWithoutTreatingSweepAsRun) {
    const auto session = makeTmpDir("hftrec_viewer_backtest_select_run");
    const auto runDir = session / "backtests" / "run-a";
    const auto sweepDir = session / "backtests" / "sweeps" / "sweep-a";
    fs::create_directories(runDir);
    fs::create_directories(sweepDir);
    writeFile(runDir / "manifest.json", R"json({"type":"run.result.v2","run_id":"run-a","strategy":"spread_maker1and2","session_path":"/tmp/session-a","summary":{},"errors":[]})json");
    writeFile(runDir / "order_lifetimes.jsonl", "[1000,1100,9900000000,100000000,1,0,0]\n");
    writeFile(runDir / "fills.jsonl", "[10,1000,1100,1,9900000000,100000000,0,0,0]\n");
    writeFile(sweepDir / "manifest.json", R"json({"type":"sweep.result.v1","sweep_id":"sweep-a","summary":{},"errors":[]})json");

    hftrec::gui::viewer::ChartController chart;
    chart.refreshBacktestResults(QString::fromStdString(session.string()));

    ASSERT_EQ(chart.backtestResults().size(), 2);
    const QVariantMap runRow = chart.backtestResults().at(0).toMap();
    const QVariantMap sweepRow = chart.backtestResults().at(1).toMap();
    ASSERT_TRUE(runRow.value(QStringLiteral("selectable")).toBool());
    ASSERT_FALSE(sweepRow.value(QStringLiteral("selectable")).toBool());

    const QString runPath = runRow.value(QStringLiteral("path")).toString();
    ASSERT_TRUE(chart.selectBacktestResult(runPath));
    EXPECT_EQ(chart.selectedBacktestResult(), runPath);

    std::error_code ec;
    fs::remove_all(session, ec);
}

TEST(ViewerBacktestResults, SelectingDetailRunReportsLoadedOverlay) {
    const auto session = makeTmpDir("hftrec_viewer_backtest_select_detail");
    const auto runDir = session / "backtests" / "run-a-detail";
    fs::create_directories(runDir);
    writeFile(runDir / "manifest.json", R"json({"type":"run.result.v2","run_id":"run-a-detail","strategy":"spread_maker1and2","session_path":"/tmp/session-a","summary":{},"errors":[]})json");
    writeFile(runDir / "order_lifetimes.jsonl", "[1000,1100,9900000000,100000000,1,0,0]\n");
    writeFile(runDir / "fills.jsonl", "[10,1000,1100,1,9900000000,100000000,0,0,0]\n");

    hftrec::gui::viewer::ChartController chart;
    chart.refreshBacktestResults(QString::fromStdString(session.string()));

    ASSERT_EQ(chart.backtestResults().size(), 1);
    const QString runPath = chart.backtestResults().at(0).toMap().value(QStringLiteral("path")).toString();
    ASSERT_TRUE(chart.selectBacktestResult(runPath));
    EXPECT_TRUE(chart.statusText().contains(QStringLiteral("Backtest loaded")));
    EXPECT_TRUE(chart.statusText().contains(QStringLiteral("run-a-detail")));
    EXPECT_TRUE(chart.statusText().contains(QStringLiteral("fills 1")));

    std::error_code ec;
    fs::remove_all(session, ec);
}
