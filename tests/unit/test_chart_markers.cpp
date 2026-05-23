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

std::string tradeLineWithSide(std::int64_t tsNs, std::int64_t priceE8, std::int64_t qtyE8, std::int64_t side) {
    return "[" + std::to_string(priceE8)
        + "," + std::to_string(qtyE8)
        + "," + std::to_string(side)
        + "," + std::to_string(tsNs)
        + "]"
        + "\n";
}

std::string tradeLine(std::int64_t tsNs, std::int64_t priceE8, std::int64_t) {
    return tradeLineWithSide(tsNs, priceE8, e8(1), 1);
}

std::string bookTickerLine(std::int64_t tsNs,
                           std::int64_t bidPriceE8,
                           std::int64_t askPriceE8,
                           std::int64_t) {
    return "[" + std::to_string(bidPriceE8)
        + "," + std::to_string(e8(1))
        + "," + std::to_string(askPriceE8)
        + "," + std::to_string(e8(1))
        + "," + std::to_string(tsNs)
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
    auto snap = chart.buildSnapshot(800.0, 600.0, SnapshotInputs{true, false, false, true});
    EXPECT_EQ(snap.tradeDots.size(), 3u);
    EXPECT_FALSE(snap.bookTickerTrace.samples.empty());

    chart.setRenderWindowSeconds(30);
    EXPECT_EQ(chart.tsMax(), 61000000000ll);
    EXPECT_EQ(chart.tsMin(), 31000000000ll);
    snap = chart.buildSnapshot(800.0, 600.0, SnapshotInputs{true, false, false, true});
    ASSERT_EQ(snap.tradeDots.size(), 2u);
    EXPECT_EQ(snap.tradeDots.front().tsNs, 31000000000ll);
    EXPECT_EQ(snap.tradeDots.back().tsNs, 61000000000ll);

    chart.setRenderWindowSeconds(-1);
    EXPECT_EQ(chart.tsMax(), 61000000000ll);
    EXPECT_EQ(chart.tsMin(), 60999999999ll);
    snap = chart.buildSnapshot(800.0, 600.0, SnapshotInputs{true, false, false, true});
    ASSERT_EQ(snap.tradeDots.size(), 1u);
    EXPECT_EQ(snap.tradeDots.front().tsNs, 61000000000ll);
    ASSERT_EQ(snap.bookTickerTrace.samples.size(), 1u);
    EXPECT_EQ(snap.bookTickerTrace.samples.front().tsNs, 61000000000ll);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(ChartRenderWindow, LoadSessionOpensAtLatestWindowWhenConfigured) {
    const auto dir = makeTmpDir();
    writeFile(dir / "trades.jsonl",
              tradeLine(1000000000ll, e8(100), 1)
              + tradeLine(61000000000ll, e8(102), 2));
    writeFile(dir / "manifest.json", R"json({
  "manifest_schema_version": 1,
  "corpus_schema_version": 2,
  "replay": { "structurally_loadable": true },
  "channels": {
    "trades": { "enabled": true, "required_when_enabled": true, "path": "trades.jsonl" }
  }
})json");

    ChartController chart;
    chart.setRenderWindowSeconds(30);
    ASSERT_TRUE(chart.loadSession(QString::fromStdString(dir.string())));
    ASSERT_TRUE(chart.loaded());
    EXPECT_EQ(chart.tsMax(), 61000000000ll);
    EXPECT_EQ(chart.tsMin(), 31000000000ll);

    const auto snap = chart.buildSnapshot(800.0, 600.0, SnapshotInputs{true, false, false});
    ASSERT_EQ(snap.tradeDots.size(), 1u);
    EXPECT_EQ(snap.tradeDots.front().tsNs, 61000000000ll);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(ChartTradeGrouping, GroupsOnlyContiguousSameTimestampPriceAndSide) {
    ChartController chart;
    const auto dir = makeTmpDir();
    writeFile(dir / "trades.jsonl",
              tradeLineWithSide(1000, e8(100), e8(1), 1)
              + tradeLineWithSide(1000, e8(100), e8(2), 1)
              + tradeLineWithSide(1000, e8(100), e8(4), 0)
              + tradeLineWithSide(1000, e8(101), e8(3), 1));

    ASSERT_TRUE(chart.addTradesFile(QString::fromStdString((dir / "trades.jsonl").string())));
    chart.finalizeFiles();
    ASSERT_TRUE(chart.loaded());
    chart.setViewport(0, 2000, e8(90), e8(110));

    const auto snap = chart.buildSnapshot(800.0, 600.0, SnapshotInputs{});
    ASSERT_EQ(snap.tradeDots.size(), 3u);
    EXPECT_EQ(snap.tradeDots[0].tsNs, 1000);
    EXPECT_EQ(snap.tradeDots[0].priceE8, e8(100));
    EXPECT_TRUE(snap.tradeDots[0].sideBuy);
    EXPECT_EQ(snap.tradeDots[0].groupEntries.size(), 2u);
    EXPECT_EQ(snap.tradeDots[0].totalQtyE8, e8(3));

    EXPECT_EQ(snap.tradeDots[1].priceE8, e8(100));
    EXPECT_FALSE(snap.tradeDots[1].sideBuy);
    EXPECT_EQ(snap.tradeDots[1].groupEntries.size(), 1u);

    EXPECT_EQ(snap.tradeDots[2].priceE8, e8(101));
    EXPECT_TRUE(snap.tradeDots[2].sideBuy);
    EXPECT_EQ(snap.tradeDots[2].groupEntries.size(), 1u);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(ChartTradeLod, AggregatesDenseTradesByScreenPixel) {
    ChartController chart;
    const auto dir = makeTmpDir();
    std::string lines;
    for (int i = 0; i < 25000; ++i) {
        lines += tradeLineWithSide(1000 + i, e8(100 + (i % 5)), e8(1), i % 2);
    }
    writeFile(dir / "trades.jsonl", lines);

    ASSERT_TRUE(chart.addTradesFile(QString::fromStdString((dir / "trades.jsonl").string())));
    chart.finalizeFiles();
    ASSERT_TRUE(chart.loaded());
    chart.setViewport(1000, 25999, e8(95), e8(110));

    const auto snap = chart.buildSnapshot(100.0, 300.0, SnapshotInputs{});
    EXPECT_TRUE(snap.tradeDecimated);
    EXPECT_FALSE(snap.tradeConnectorsVisible);
    ASSERT_LE(snap.tradeDots.size(), 100u);
    ASSERT_FALSE(snap.tradeDots.empty());
    EXPECT_TRUE(snap.tradeDots.front().aggregated);
    EXPECT_GT(snap.tradeDots.front().tradeCount, 1);
    EXPECT_GT(snap.tradeDots.front().totalQtyE8, 0);
    EXPECT_GT(snap.tradeDots.front().buyQtyE8 + snap.tradeDots.front().sellQtyE8, 0);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(ChartTradeLod, UsesHysteresisBeforeReturningToExact) {
    ChartController chart;
    const auto dir = makeTmpDir();
    std::string lines;
    for (int i = 0; i < 25000; ++i) {
        lines += tradeLineWithSide(1000 + i, e8(100), e8(1), 1);
    }
    writeFile(dir / "trades.jsonl", lines);

    ASSERT_TRUE(chart.addTradesFile(QString::fromStdString((dir / "trades.jsonl").string())));
    chart.finalizeFiles();
    ASSERT_TRUE(chart.loaded());

    chart.setViewport(1000, 25999, e8(95), e8(105));
    auto snap = chart.buildSnapshot(100.0, 300.0, SnapshotInputs{});
    ASSERT_TRUE(snap.tradeDecimated);

    chart.setViewport(1000, 21000, e8(95), e8(105));
    snap = chart.buildSnapshot(100.0, 300.0, SnapshotInputs{});
    EXPECT_TRUE(snap.tradeDecimated);

    chart.setViewport(1000, 20000, e8(95), e8(105));
    snap = chart.buildSnapshot(100.0, 300.0, SnapshotInputs{});
    EXPECT_FALSE(snap.tradeDecimated);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

}  // namespace
