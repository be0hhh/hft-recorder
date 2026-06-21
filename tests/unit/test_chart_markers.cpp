#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <QString>

#include "gui/viewer/ChartController.hpp"
#include "gui/viewer/ChartItemInternal.hpp"
#include "gui/viewer/hit_test/HoverDetection.hpp"

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

TEST(ChartStrategyOverlay, LoadsBacktestResultIntoSnapshot) {
    ChartController chart;
    const auto dir = makeTmpDir();
    fs::create_directories(dir / "backtests");
    writeFile(dir / "trades.jsonl", tradeLine(1000, e8(100), 1) + tradeLine(2500, e8(101), 2));
    writeFile(dir / "manifest.json", R"json({
  "manifest_schema_version": 1,
  "corpus_schema_version": 2,
  "replay": { "structurally_loadable": true },
  "channels": {
    "trades": { "enabled": true, "required_when_enabled": true, "path": "trades.jsonl" }
  }
})json");
    const auto resultPath = dir / "backtests" / "run-a";
    fs::create_directories(resultPath);
    writeFile(resultPath / "manifest.json", R"json({"type":"run.result.v2","run_id":"run-a","strategy":"test","session_path":"session","summary":{},"errors":[]})json");
    writeFile(resultPath / "order_lifetimes.jsonl",
              "[1200,1800,9900000000,100000000,1,0]\n");
    writeFile(resultPath / "fills.jsonl",
              "[1,1200,1800,1,9900000000,100000000,0,0]\n"
              "[2,1500,1600,0,10100000000,100000000,0,1]\n");
    writeFile(resultPath / "strategy_range.jsonl",
              "[1100,9800000000,10000000000,10200000000]\n"
              "[2100,9900000000,10100000000,10300000000]\n");

    ASSERT_TRUE(chart.addTradesFile(QString::fromStdString((dir / "trades.jsonl").string())));
    chart.finalizeFiles();
    ASSERT_TRUE(chart.loaded());
    chart.setViewport(0, 3000, e8(90), e8(110));
    chart.refreshBacktestResults(QString::fromStdString(dir.string()));
    ASSERT_EQ(chart.backtestResults().size(), 1);
    ASSERT_TRUE(chart.selectBacktestResult(QString::fromStdString(resultPath.string())));

    const auto snap = chart.buildSnapshot(800.0, 600.0, SnapshotInputs{});
    ASSERT_EQ(snap.strategyOrderSegments.size(), 1u);
    EXPECT_EQ(snap.strategyOrderSegments.front().tsStartNs, 1200);
    EXPECT_EQ(snap.strategyOrderSegments.front().tsEndNs, 1800);
    EXPECT_TRUE(snap.strategyOrderSegments.front().sideBuy);
    ASSERT_EQ(snap.strategyFillMarkers.size(), 2u);
    EXPECT_FALSE(snap.strategyFillMarkers[0].sideBuy);
    EXPECT_EQ(snap.strategyFillMarkers[0].shape, hftrec::gui::viewer::StrategyFillShape::SellDown);
    EXPECT_TRUE(snap.strategyFillMarkers[0].reduceOnly);
    EXPECT_TRUE(snap.strategyFillMarkers[1].sideBuy);
    EXPECT_EQ(snap.strategyFillMarkers[1].shape, hftrec::gui::viewer::StrategyFillShape::BuyUp);
    EXPECT_FALSE(snap.strategyFillMarkers[1].reduceOnly);
    ASSERT_EQ(snap.strategyRangePoints.size(), 2u);
    EXPECT_EQ(snap.strategyRangePoints[0].tsNs, 1100);
    EXPECT_EQ(snap.strategyRangePoints[0].lowE8, e8(98));
    EXPECT_EQ(snap.strategyRangePoints[0].midE8, e8(100));
    EXPECT_EQ(snap.strategyRangePoints[0].highE8, e8(102));

    hftrec::gui::viewer::HoverInfo hover{};
    hftrec::gui::viewer::hit_test::computeHover(snap,
                                                QPointF{snap.vp.toX(1600), snap.vp.toY(e8(101))},
                                                true,
                                                hover);
    EXPECT_TRUE(hover.strategyFillHit);
    EXPECT_EQ(hover.strategyFillTsNs, 1600);
    EXPECT_EQ(hover.strategyFillPriceE8, e8(101));
    EXPECT_EQ(hover.strategyFillQtyE8, e8(1));
    EXPECT_EQ(hover.strategyFillAmountE8, e8(101));
    EXPECT_FALSE(hover.strategyFillSideBuy);
    EXPECT_TRUE(hover.strategyFillReduceOnly);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(ChartStrategyOverlay, FinalOverlayPassDoesNotDependOnTradesLayer) {
    hftrec::gui::viewer::RenderSnapshot snap{};
    snap.strategyFillMarkers.push_back(hftrec::gui::viewer::StrategyFillMarker{
        1600,
        e8(101),
        e8(1),
        0,
        true,
        false,
        false,
        hftrec::gui::viewer::StrategyFillShape::BuyUp,
    });

    EXPECT_TRUE(hftrec::gui::viewer::detail::shouldRenderStrategyOverlayInFinalPass(snap, false));
    EXPECT_FALSE(hftrec::gui::viewer::detail::shouldRenderStrategyOverlayInFinalPass(snap, true));

    hftrec::gui::viewer::RenderSnapshot rangeOnly{};
    rangeOnly.strategyRangePoints.push_back(hftrec::gui::viewer::StrategyRangePoint{
        1600,
        e8(99),
        e8(100),
        e8(101),
    });
    EXPECT_FALSE(hftrec::gui::viewer::detail::shouldRenderStrategyOverlayInFinalPass(rangeOnly, false));
}

TEST(ChartFundingOverlay, ContextHoverSelectsNearestFundingMarker) {
    hftrec::gui::viewer::RenderSnapshot snap{};
    snap.loaded = true;
    snap.fundingVisible = true;
    snap.vp = hftrec::gui::viewer::ViewportMap{0, 3000, e8(90), e8(110), 800.0, 600.0};
    snap.fundings.push_back(hftrec::replay::FundingRow{1000, 25000, 900, 1100});
    snap.fundings.push_back(hftrec::replay::FundingRow{2000, -12500, 1900, 2100});

    hftrec::gui::viewer::HoverInfo hover{};
    hftrec::gui::viewer::hit_test::computeHover(snap,
                                                QPointF{snap.vp.toX(2000), 582.0},
                                                true,
                                                hover);

    EXPECT_TRUE(hover.fundingHit);
    EXPECT_EQ(hover.fundingEventTsNs, 2000);
    EXPECT_EQ(hover.fundingRateE8, -12500);
    EXPECT_EQ(hover.fundingTsNs, 1900);
    EXPECT_EQ(hover.nextFundingTsNs, 2100);
    EXPECT_EQ(hover.fundingCadenceNs, 200);
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

    SnapshotInputs inputs{};
    inputs.tradesVisible = true;
    inputs.liquidationsVisible = false;
    inputs.candlesVisible = false;
    inputs.bookTickerVisible = true;

    chart.setRenderWindowSeconds(0);
    auto snap = chart.buildSnapshot(800.0, 600.0, inputs);
    EXPECT_EQ(snap.tradeDots.size(), 3u);
    EXPECT_FALSE(snap.bookTickerTrace.samples.empty());

    chart.setRenderWindowSeconds(30);
    EXPECT_EQ(chart.tsMax(), 61000000000ll);
    EXPECT_EQ(chart.tsMin(), 31000000000ll);
    snap = chart.buildSnapshot(800.0, 600.0, inputs);
    ASSERT_EQ(snap.tradeDots.size(), 2u);
    EXPECT_EQ(snap.tradeDots.front().tsNs, 31000000000ll);
    EXPECT_EQ(snap.tradeDots.back().tsNs, 61000000000ll);

    chart.setRenderWindowSeconds(-1);
    EXPECT_EQ(chart.tsMax(), 61000000000ll);
    EXPECT_EQ(chart.tsMin(), 60999999999ll);
    snap = chart.buildSnapshot(800.0, 600.0, inputs);
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
    ASSERT_TRUE(chart.addTradesFile(QString::fromStdString((dir / "trades.jsonl").string())));
    chart.finalizeFiles();
    ASSERT_TRUE(chart.loaded());
    ASSERT_TRUE(chart.loaded());
    EXPECT_EQ(chart.tsMax(), 61000000000ll);
    EXPECT_EQ(chart.tsMin(), 31000000000ll);

    SnapshotInputs inputs{};
    inputs.tradesVisible = true;
    inputs.liquidationsVisible = false;
    inputs.candlesVisible = false;

    const auto snap = chart.buildSnapshot(800.0, 600.0, inputs);
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

TEST(ChartTradesFullDots, KeepsDenseTradesExact) {
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
    EXPECT_FALSE(snap.tradeDecimated);
    ASSERT_EQ(snap.tradeDots.size(), 25000u);
    ASSERT_FALSE(snap.tradeDots.empty());
    EXPECT_FALSE(snap.tradeDots.front().aggregated);
    EXPECT_FALSE(snap.tradeDots.back().aggregated);
    EXPECT_EQ(snap.tradeDots.front().tsNs, 1000);
    EXPECT_EQ(snap.tradeDots.back().tsNs, 25999);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(ChartTradesFullDots, KeepsSemanticSameTradeGroupingOnly) {
    ChartController chart;
    const auto dir = makeTmpDir();
    std::string lines;
    for (int i = 0; i < 25000; ++i) {
        lines += tradeLineWithSide(1000 + (i / 10), e8(100), e8(1), 1);
    }
    writeFile(dir / "trades.jsonl", lines);

    ASSERT_TRUE(chart.addTradesFile(QString::fromStdString((dir / "trades.jsonl").string())));
    chart.finalizeFiles();
    ASSERT_TRUE(chart.loaded());
    chart.setViewport(1000, 3499, e8(95), e8(110));

    const auto snap = chart.buildSnapshot(100.0, 300.0, SnapshotInputs{});
    EXPECT_FALSE(snap.tradeDecimated);
    ASSERT_EQ(snap.tradeDots.size(), 2500u);
    EXPECT_FALSE(snap.tradeDots.front().aggregated);
    EXPECT_EQ(snap.tradeDots.front().groupEntries.size(), 10u);
    EXPECT_EQ(snap.tradeDots.front().totalQtyE8, e8(10));
    EXPECT_EQ(snap.tradeDots.back().groupEntries.size(), 10u);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(ChartCandles, KeepsTierDirectionWhenViewportStartsAfterPreviousCandle) {
    ChartController chart;
    const auto dir = makeTmpDir();
    writeFile(dir / "candles.jsonl",
              "[1,1000,11000000000,9000000000,100000000]\n"
              "[1,2000,10000000000,9000000000,100000000]\n");

    ASSERT_TRUE(chart.addCandlesFile(QString::fromStdString((dir / "candles.jsonl").string())));
    chart.finalizeFiles();
    ASSERT_TRUE(chart.loaded());
    chart.setViewport(1900, 2100, e8(80), e8(120));

    SnapshotInputs inputs{};
    inputs.tradesVisible = false;
    inputs.candlesVisible = true;
    const auto snap = chart.buildSnapshot(400.0, 300.0, inputs);
    ASSERT_EQ(snap.candleRects.size(), 1u);
    EXPECT_FALSE(snap.candleRects.front().up);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

}  // namespace
