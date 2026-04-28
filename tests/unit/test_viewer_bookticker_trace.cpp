#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <QPointF>
#include <QString>

#include "core/storage/EventStorage.hpp"
#include "gui/viewer/ChartController.hpp"
#include "gui/viewer/hit_test/HoverDetection.hpp"

namespace fs = std::filesystem;

namespace {

using hftrec::gui::viewer::ChartController;
using hftrec::gui::viewer::HoverInfo;
using hftrec::gui::viewer::LiveDataRegistry;
using hftrec::gui::viewer::SnapshotInputs;

constexpr std::int64_t kScaleE8 = 100000000ll;

std::int64_t e8(std::int64_t value) {
    return value * kScaleE8;
}

fs::path makeTmpDir() {
    const auto base = fs::temp_directory_path();
    auto dir = base / ("hftrec_viewer_bookticker_trace_" + std::to_string(std::rand()));
    fs::create_directories(dir);
    return dir;
}

void writeFile(const fs::path& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << data;
}

std::string bookTickerLine(std::int64_t tsNs,
                           std::int64_t,
                           std::int64_t bidPriceE8,
                           std::int64_t askPriceE8) {
    return "[" + std::to_string(bidPriceE8)
        + "," + std::to_string(e8(2))
        + "," + std::to_string(askPriceE8)
        + "," + std::to_string(e8(3))
        + "," + std::to_string(tsNs)
        + "]\n";
}

hftrec::replay::TradeRow tradeRow(std::int64_t tsNs,
                                  std::int64_t captureSeq,
                                  std::int64_t priceE8,
                                  std::int64_t qtyE8) {
    hftrec::replay::TradeRow row{};
    row.tsNs = tsNs;
    row.captureSeq = captureSeq;
    row.ingestSeq = captureSeq;
    row.priceE8 = priceE8;
    row.qtyE8 = qtyE8;
    row.side = 1;
    row.sideBuy = 1u;
    return row;
}

hftrec::replay::DepthRow depthRow(std::int64_t tsNs,
                                  std::int64_t,
                                  std::int64_t bidPriceE8,
                                  std::int64_t askPriceE8) {
    hftrec::replay::DepthRow row{};
    row.tsNs = tsNs;
    row.levels.push_back(hftrec::replay::PricePair{bidPriceE8, e8(2), 0});
    row.levels.push_back(hftrec::replay::PricePair{askPriceE8, e8(3), 1});
    return row;
}

class StubIngress final : public hftrec::market_data::IMarketDataIngress {
  public:
    explicit StubIngress(const hftrec::storage::IEventSource* source) : source_(source) {}

    const hftrec::storage::IEventSource* eventSource() const noexcept override { return source_; }
    const hftrec::storage::IHotEventCache* hotCache() const noexcept override { return nullptr; }

  private:
    const hftrec::storage::IEventSource* source_{nullptr};
};

SnapshotInputs bookTickerInputs() {
    SnapshotInputs in{};
    in.tradesVisible = false;
    in.orderbookVisible = false;
    in.bookTickerVisible = true;
    in.exactTradeRendering = true;
    return in;
}

TEST(ViewerBookTickerTrace, BuildsContinuousTickerTraceWithoutBookSegments) {
    const auto dir = makeTmpDir();
    writeFile(
        dir / "bookticker.jsonl",
        bookTickerLine(1000, 1, e8(10000), e8(10100))
            + bookTickerLine(2000, 2, e8(10050), e8(10150)));

    ChartController chart;
    EXPECT_TRUE(chart.addBookTickerFile(QString::fromStdString((dir / "bookticker.jsonl").string())));
    chart.finalizeFiles();
    ASSERT_TRUE(chart.loaded());
    chart.setViewport(1000, 3000, e8(9900), e8(10200));

    const auto snap = chart.buildSnapshot(200.0, 300.0, bookTickerInputs());
    EXPECT_TRUE(snap.bookSegments.empty());
    EXPECT_FALSE(snap.bookTickerTrace.bidLines.empty());
    EXPECT_FALSE(snap.bookTickerTrace.askLines.empty());
    EXPECT_LT(snap.bookTickerTrace.samples.size(), 120u);

    HoverInfo hover{};
    hftrec::gui::viewer::hit_test::computeHover(
        snap,
        QPointF{50.0, snap.vp.toY(e8(10000))},
        true,
        hover);
    EXPECT_EQ(hover.bookKind, 1);
    EXPECT_EQ(hover.bookPriceE8, e8(10000));
    EXPECT_EQ(hover.bookQtyE8, e8(2));

    hftrec::gui::viewer::hit_test::computeHover(
        snap,
        QPointF{100.0, snap.vp.toY(e8(10150))},
        true,
        hover);
    EXPECT_EQ(hover.bookKind, 2);
    EXPECT_EQ(hover.bookPriceE8, e8(10150));
    EXPECT_EQ(hover.bookQtyE8, e8(3));

    hftrec::gui::viewer::hit_test::computeHover(
        snap,
        QPointF{150.0, snap.vp.toY(e8(10150))},
        true,
        hover);
    EXPECT_EQ(hover.bookKind, 0);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(ViewerBookTickerTrace, BreaksTraceAcrossStaleTickerGap) {
    const auto dir = makeTmpDir();
    writeFile(
        dir / "bookticker.jsonl",
        bookTickerLine(1'000'000'000ll, 1, e8(10000), e8(10100))
            + bookTickerLine(8'500'000'000ll, 2, e8(10050), e8(10150)));

    ChartController chart;
    EXPECT_TRUE(chart.addBookTickerFile(QString::fromStdString((dir / "bookticker.jsonl").string())));
    chart.finalizeFiles();
    ASSERT_TRUE(chart.loaded());
    chart.setViewport(0, 10'000'000'000ll, e8(9900), e8(10200));

    const auto snap = chart.buildSnapshot(200.0, 300.0, bookTickerInputs());
    ASSERT_FALSE(snap.bookTickerTrace.bidLines.empty());

    HoverInfo hover{};
    hftrec::gui::viewer::hit_test::computeHover(
        snap,
        QPointF{100.0, snap.vp.toY(e8(10000))},
        true,
        hover);
    EXPECT_EQ(hover.bookKind, 0);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(ViewerLiveSource, RegistrySelectionUsesInMemoryProviderForViewportCache) {
    hftrec::storage::LiveEventStore store{};
    ASSERT_EQ(store.appendTrade(tradeRow(100, 1, e8(100), e8(2))), hftrec::Status::Ok);
    StubIngress ingress(&store);

    auto& registry = LiveDataRegistry::instance();
    registry.setSources({
        {"live:test:futures:BTCUSDT", "Test", "Futures", "BTCUSDT", "s1", {}, &ingress},
    });

    ChartController chart;
    ASSERT_TRUE(chart.activateLiveSource(QStringLiteral("live:test:futures:BTCUSDT"), QString{}));
    chart.refreshLiveDataWindow(0, 200);

    ASSERT_EQ(chart.liveDataCache().stableRows.trades.size(), 1u);
    EXPECT_EQ(chart.liveDataCache().stableRows.trades.front().tsNs, 100);
    EXPECT_TRUE(chart.loaded());

    registry.clear();
}

TEST(ViewerLiveSource, MaterializesPriorDepthForOrderbookState) {
    hftrec::storage::LiveEventStore store{};
    ASSERT_EQ(store.appendDepth(depthRow(100, 1, e8(100), e8(101))), hftrec::Status::Ok);
    ASSERT_EQ(store.appendDepth(depthRow(150, 2, e8(99), e8(102))), hftrec::Status::Ok);
    StubIngress ingress(&store);

    auto& registry = LiveDataRegistry::instance();
    registry.setSources({
        {"live:test:futures:BNBUSDT", "Test", "Futures", "BNBUSDT", "s3", {}, &ingress},
    });

    ChartController chart;
    ASSERT_TRUE(chart.activateLiveSource(QStringLiteral("live:test:futures:BNBUSDT"), QString{}));
    chart.refreshLiveDataWindow(120, 200);

    ASSERT_EQ(chart.liveDataCache().stableRows.depths.size(), 2u);
    EXPECT_EQ(chart.liveDataCache().stableRows.depths.front().tsNs, 100);
    EXPECT_EQ(chart.liveDataCache().stableRows.depths.back().tsNs, 150);

    registry.clear();
}

TEST(ViewerSelection, IncludesLiveStableRowsInRectangleSummary) {
    hftrec::storage::LiveEventStore store{};
    ASSERT_EQ(store.appendTrade(tradeRow(100, 1, e8(100), e8(2))), hftrec::Status::Ok);
    StubIngress ingress(&store);

    auto& registry = LiveDataRegistry::instance();
    registry.setSources({
        {"live:test:futures:ETHUSDT", "Test", "Futures", "ETHUSDT", "s2", {}, &ingress},
    });

    ChartController chart;
    ASSERT_TRUE(chart.activateLiveSource(QStringLiteral("live:test:futures:ETHUSDT"), QString{}));
    chart.refreshLiveDataWindow(0, 200);
    chart.setViewport(0, 200, e8(90), e8(110));

    ASSERT_TRUE(chart.commitSelectionRect(200.0, 200.0, 0.0, 0.0, 200.0, 200.0));
    EXPECT_TRUE(chart.selectionSummaryText().contains(QStringLiteral("Count  1")));

    registry.clear();
}

TEST(ViewerTradeHover, HitsTradeAndLeavesEmptySpaceUnmatched) {
    hftrec::gui::viewer::RenderSnapshot snap{};
    snap.loaded = true;
    snap.tradesVisible = true;
    snap.vp = hftrec::gui::viewer::ViewportMap{0, 200, e8(90), e8(110), 200.0, 200.0};
    snap.tradeDots.push_back(hftrec::gui::viewer::TradeDot{100, e8(100), e8(2), true, 7});

    HoverInfo hover{};
    hftrec::gui::viewer::hit_test::computeHover(
        snap,
        QPointF{snap.vp.toX(100), snap.vp.toY(e8(100))},
        true,
        hover);
    EXPECT_TRUE(hover.tradeHit);
    EXPECT_EQ(hover.tradeOrigIndex, 7);
    EXPECT_EQ(hover.tradePriceE8, e8(100));
    EXPECT_EQ(hover.tradeQtyE8, e8(2));

    hftrec::gui::viewer::hit_test::computeHover(
        snap,
        QPointF{10.0, 10.0},
        true,
        hover);
    EXPECT_FALSE(hover.tradeHit);
    EXPECT_EQ(hover.bookKind, 0);
}

}  // namespace
