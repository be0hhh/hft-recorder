#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <QString>

#include "gui/viewer/ChartController.hpp"

namespace fs = std::filesystem;

namespace {

using hftrec::gui::viewer::ChartController;
using hftrec::gui::viewer::SnapshotInputs;

constexpr std::int64_t kScaleE8 = 100000000ll;

std::int64_t e8(std::int64_t value) {
    return value * kScaleE8;
}

fs::path makeTmpDir() {
    const auto base = fs::temp_directory_path();
    auto dir = base / ("hftrec_chart_markers_" + std::to_string(std::rand()));
    fs::create_directories(dir);
    return dir;
}

void writeFile(const fs::path& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << data;
}

std::string tradeLine(std::int64_t tsNs, std::int64_t priceE8, std::int64_t captureSeq) {
    return "[" + std::to_string(priceE8)
        + "," + std::to_string(e8(1))
        + ",1"
        + "," + std::to_string(tsNs)
        + R"(,0,0,0,0,0,"BTCUSDT","binance","futures_usd",)"
        + std::to_string(captureSeq)
        + "," + std::to_string(captureSeq)
        + "]"
        + "\n";
}

std::string bookTickerLine(std::int64_t tsNs,
                           std::int64_t bidPriceE8,
                           std::int64_t askPriceE8,
                           std::int64_t captureSeq) {
    return "[" + std::to_string(bidPriceE8)
        + "," + std::to_string(e8(1))
        + "," + std::to_string(askPriceE8)
        + "," + std::to_string(e8(1))
        + "," + std::to_string(tsNs)
        + R"(,"BTCUSDT","binance","futures_usd",)"
        + std::to_string(captureSeq)
        + "," + std::to_string(captureSeq)
        + "]"
        + "\n";
}

TEST(ChartMarkers, RequiresLoadedChartAndAppearsInSnapshot) {
    ChartController chart;
    EXPECT_FALSE(chart.addVerticalMarker(1000, QStringLiteral("early")));
    EXPECT_EQ(chart.verticalMarkerCount(), 0);

    const auto dir = makeTmpDir();
    writeFile(dir / "trades.jsonl", tradeLine(1000, e8(100), 1) + tradeLine(2000, e8(101), 2));

    ASSERT_TRUE(chart.addTradesFile(QString::fromStdString((dir / "trades.jsonl").string())));
    chart.finalizeFiles();
    ASSERT_TRUE(chart.loaded());

    EXPECT_TRUE(chart.addVerticalMarker(1500, QStringLiteral("signal")));
    EXPECT_EQ(chart.verticalMarkerCount(), 1);

    const auto snap = chart.buildSnapshot(800.0, 600.0, SnapshotInputs{});
    ASSERT_EQ(snap.verticalMarkers.size(), 1u);
    EXPECT_EQ(snap.verticalMarkers.front().tsNs, 1500);
    EXPECT_EQ(snap.verticalMarkers.front().label, QStringLiteral("signal"));

    chart.clearVerticalMarkers();
    EXPECT_EQ(chart.verticalMarkerCount(), 0);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(ChartRenderWindow, ClipsRecordedRowsAndSupportsLatestOnly) {
    ChartController chart;
    const auto dir = makeTmpDir();
    writeFile(dir / "trades.jsonl",
              tradeLine(1000000000ll, e8(100), 1)
              + tradeLine(31000000000ll, e8(101), 2)
              + tradeLine(61000000000ll, e8(102), 3));
    writeFile(dir / "bookticker.jsonl",
              bookTickerLine(1000000000ll, e8(99), e8(101), 1)
              + bookTickerLine(61000000000ll, e8(101), e8(103), 2));

    ASSERT_TRUE(chart.addTradesFile(QString::fromStdString((dir / "trades.jsonl").string())));
    ASSERT_TRUE(chart.addBookTickerFile(QString::fromStdString((dir / "bookticker.jsonl").string())));
    chart.finalizeFiles();
    ASSERT_TRUE(chart.loaded());
    chart.setViewport(0, 70000000000ll, e8(90), e8(110));

    chart.setRenderWindowSeconds(0);
    auto snap = chart.buildSnapshot(800.0, 600.0, SnapshotInputs{true, false, true});
    EXPECT_EQ(snap.tradeDots.size(), 3u);
    EXPECT_FALSE(snap.bookTickerTrace.samples.empty());

    chart.setRenderWindowSeconds(30);
    snap = chart.buildSnapshot(800.0, 600.0, SnapshotInputs{true, false, true});
    ASSERT_EQ(snap.tradeDots.size(), 2u);
    EXPECT_EQ(snap.tradeDots.front().tsNs, 31000000000ll);
    EXPECT_EQ(snap.tradeDots.back().tsNs, 61000000000ll);

    chart.setRenderWindowSeconds(-1);
    snap = chart.buildSnapshot(800.0, 600.0, SnapshotInputs{true, false, true});
    ASSERT_EQ(snap.tradeDots.size(), 1u);
    EXPECT_EQ(snap.tradeDots.front().tsNs, 61000000000ll);
    ASSERT_EQ(snap.bookTickerTrace.samples.size(), 1u);
    EXPECT_EQ(snap.bookTickerTrace.samples.front().tsNs, 61000000000ll);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

}  // namespace
