#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <QCoreApplication>
#include <QPointF>
#include <QSettings>
#include <QString>

#include "core/storage/EventStorage.hpp"
#include "gui/viewer/BookTickerCompareController.hpp"
#include "gui/viewer/ChartController.hpp"
#include "gui/viewer/hit_test/HoverDetection.hpp"

namespace fs = std::filesystem;

namespace {

using hftrec::gui::viewer::BookTickerCompareController;
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

void isolateViewerSettings(QStringView suffix) {
    QCoreApplication::setOrganizationName(QStringLiteral("hftrec_viewer_tests"));
    QCoreApplication::setApplicationName(QStringLiteral("case_%1_%2").arg(suffix, QString::number(std::rand())));
    QSettings{}.clear();
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

std::string fundingLine(std::int64_t tsNs, std::int64_t rateE8) {
    return "[" + std::to_string(tsNs)
        + "," + std::to_string(rateE8)
        + "," + std::to_string(tsNs)
        + "," + std::to_string(tsNs + 8LL * 60LL * 60LL * 1000000000LL)
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

TEST(ViewerBookTickerTrace, BuildsSampleTickerTraceWithoutBookSegments) {
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
    ASSERT_FALSE(snap.bookTickerTrace.bidLines.empty());
    ASSERT_FALSE(snap.bookTickerTrace.askLines.empty());
    ASSERT_GE(snap.bookTickerTrace.samples.size(), 2u);

    const qreal secondX = std::round(snap.vp.toX(2000));
    const qreal oldBidY = std::round(snap.vp.toY(e8(10000)));
    const qreal newBidY = std::round(snap.vp.toY(e8(10050)));
    bool hasBidHorizontal = false;
    bool hasBidVertical = false;
    for (const auto& line : snap.bookTickerTrace.bidLines) {
        EXPECT_TRUE(line.x0 == line.x1 || line.y0 == line.y1);
        EXPECT_LE(line.x1, secondX + 1.0);
        if (line.y0 == oldBidY && line.y1 == oldBidY && line.x0 == 0.0 && line.x1 == secondX) {
            hasBidHorizontal = true;
        }
        if (line.x0 == secondX && line.x1 == secondX && line.y0 == oldBidY && line.y1 == newBidY) {
            hasBidVertical = true;
        }
    }
    EXPECT_TRUE(hasBidHorizontal);
    EXPECT_TRUE(hasBidVertical);

    HoverInfo hover{};
    hftrec::gui::viewer::hit_test::computeHover(
        snap,
        QPointF{0.0, snap.vp.toY(e8(10000))},
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

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(ViewerBookTickerTrace, MaterializesSamplesAcrossSparseHoldButStopsAtLastTicker) {
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
    ASSERT_GT(snap.bookTickerTrace.samples.size(), 2u);

    for (const auto& line : snap.bookTickerTrace.bidLines) {
        EXPECT_TRUE(line.x0 == line.x1 || line.y0 == line.y1);
        EXPECT_LE(line.x1, std::round(snap.vp.toX(8'500'000'000ll)) + 1.0);
    }

    HoverInfo hover{};
    hftrec::gui::viewer::hit_test::computeHover(
        snap,
        QPointF{100.0, snap.vp.toY(e8(10000))},
        true,
        hover);
    EXPECT_EQ(hover.bookKind, 1);
    EXPECT_EQ(hover.bookPriceE8, e8(10000));

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(ViewerBookTickerTrace, KeepsStepContinuityWhenUpdatesShareRoundedPixel) {
    const auto dir = makeTmpDir();
    writeFile(
        dir / "bookticker.jsonl",
        bookTickerLine(1000, 1, e8(10000), e8(10100))
            + bookTickerLine(1001, 2, e8(10010), e8(10110))
            + bookTickerLine(1002, 3, e8(10020), e8(10120))
            + bookTickerLine(2000, 4, e8(10030), e8(10130)));

    ChartController chart;
    EXPECT_TRUE(chart.addBookTickerFile(QString::fromStdString((dir / "bookticker.jsonl").string())));
    chart.finalizeFiles();
    ASSERT_TRUE(chart.loaded());
    chart.setViewport(1000, 3000, e8(9900), e8(10200));

    const auto snap = chart.buildSnapshot(200.0, 300.0, bookTickerInputs());
    ASSERT_FALSE(snap.bookTickerTrace.bidLines.empty());

    for (std::size_t i = 1; i < snap.bookTickerTrace.bidLines.size(); ++i) {
        const auto& prev = snap.bookTickerTrace.bidLines[i - 1u];
        const auto& cur = snap.bookTickerTrace.bidLines[i];
        EXPECT_EQ(prev.x1, cur.x0) << "line " << i;
        EXPECT_TRUE(prev.y1 == cur.y0 || cur.x0 == cur.x1) << "line " << i;
    }

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(ViewerBookTickerCompare, RebuildPreservesManualTimeViewport) {
    const auto dirA = makeTmpDir();
    const auto dirB = makeTmpDir();
    writeFile(
        dirA / "bookticker.jsonl",
        bookTickerLine(1000, 1, e8(10000), e8(10010))
            + bookTickerLine(2000, 2, e8(10020), e8(10030))
            + bookTickerLine(3000, 3, e8(10040), e8(10050)));
    writeFile(
        dirB / "bookticker.jsonl",
        bookTickerLine(1000, 1, e8(10005), e8(10015))
            + bookTickerLine(2000, 2, e8(10025), e8(10035))
            + bookTickerLine(3000, 3, e8(10045), e8(10055)));

    BookTickerCompareController compare;
    ASSERT_TRUE(compare.setPrimarySource(QStringLiteral("a"), QStringLiteral("recorded"), QString::fromStdString(dirA.string())));
    ASSERT_TRUE(compare.setSecondarySource(QStringLiteral("b"), QStringLiteral("recorded"), QString::fromStdString(dirB.string())));
    ASSERT_TRUE(compare.ready());

    const auto fullMin = compare.tsMin();
    const auto fullMax = compare.tsMax();
    ASSERT_LT(fullMin, fullMax);

    compare.panTime(0.25);
    const auto pannedMin = compare.tsMin();
    const auto pannedMax = compare.tsMax();
    ASSERT_NE(pannedMin, fullMin);
    ASSERT_NE(pannedMax, fullMax);

    compare.setMeanWindowSeconds(12.0);
    EXPECT_EQ(compare.tsMin(), pannedMin);
    EXPECT_EQ(compare.tsMax(), pannedMax);

    compare.autoFit();
    EXPECT_EQ(compare.tsMin(), fullMin);
    EXPECT_EQ(compare.tsMax(), fullMax);

    std::error_code ec;
    fs::remove_all(dirA, ec);
    fs::remove_all(dirB, ec);
}

TEST(ViewerBookTickerCompare, RecordedSourceUsesBookTickerWithoutFullSessionReplay) {
    const auto dirA = makeTmpDir();
    const auto dirB = makeTmpDir();
    fs::create_directories(dirA / "jsonl");
    fs::create_directories(dirB / "jsonl");

    writeFile(
        dirA / "jsonl" / "bookticker.jsonl",
        bookTickerLine(1000, 1, e8(10000), e8(10010))
            + bookTickerLine(2000, 2, e8(10020), e8(10030)));
    writeFile(
        dirB / "jsonl" / "bookticker.jsonl",
        bookTickerLine(1000, 1, e8(10005), e8(10015))
            + bookTickerLine(2000, 2, e8(10025), e8(10035)));

    writeFile(dirA / "jsonl" / "depth_tape.jsonl", "[9223372036854776808,100,2]\n[9223372036854777808,101,2]\n");
    writeFile(dirA / "jsonl" / "depth_sidecar.jsonl", "[9223372036854776808,0,1]\n");
    writeFile(dirB / "jsonl" / "depth_tape.jsonl", "[9223372036854776808,100,2]\n[9223372036854777808,101,2]\n");
    writeFile(dirB / "jsonl" / "depth_sidecar.jsonl", "[9223372036854776808,0,1]\n");
    writeFile(dirA / "jsonl" / "funding.jsonl", fundingLine(1500, 1000));
    writeFile(dirB / "jsonl" / "funding.jsonl", fundingLine(1700, -2000));

    BookTickerCompareController compare;
    ASSERT_TRUE(compare.setPrimarySource(QStringLiteral("a"), QStringLiteral("recorded"), QString::fromStdString(dirA.string())));
    ASSERT_TRUE(compare.setSecondarySource(QStringLiteral("b"), QStringLiteral("recorded"), QString::fromStdString(dirB.string())));
    EXPECT_EQ(compare.primaryCount(), 2);
    EXPECT_EQ(compare.secondaryCount(), 2);
    ASSERT_EQ(compare.primaryFundingRows().size(), 1u);
    ASSERT_EQ(compare.secondaryFundingRows().size(), 1u);
    EXPECT_EQ(compare.primaryFundingRows().front().tsNs, 1500);
    EXPECT_EQ(compare.secondaryFundingRows().front().fundingRateE8, -2000);
    EXPECT_TRUE(compare.ready());

    std::error_code ec;
    fs::remove_all(dirA, ec);
    fs::remove_all(dirB, ec);
}

TEST(ViewerBookTickerCompare, ValueScaleZoomPanAndAutoFitReset) {
    const auto dirA = makeTmpDir();
    const auto dirB = makeTmpDir();
    writeFile(
        dirA / "bookticker.jsonl",
        bookTickerLine(1000, 1, e8(10000), e8(10010))
            + bookTickerLine(2000, 2, e8(10020), e8(10030)));
    writeFile(
        dirB / "bookticker.jsonl",
        bookTickerLine(1000, 1, e8(10005), e8(10015))
            + bookTickerLine(2000, 2, e8(10025), e8(10035)));

    BookTickerCompareController compare;
    ASSERT_TRUE(compare.setPrimarySource(QStringLiteral("a"), QStringLiteral("recorded"), QString::fromStdString(dirA.string())));
    ASSERT_TRUE(compare.setSecondarySource(QStringLiteral("b"), QStringLiteral("recorded"), QString::fromStdString(dirB.string())));

    compare.zoomPrice(2.0);
    compare.panPrice(0.10);
    compare.zoomSpread(3.0);
    compare.panSpread(-0.10);
    EXPECT_GT(compare.priceZoom(), 1.0);
    EXPECT_NE(compare.pricePan(), 0.0);
    EXPECT_GT(compare.spreadZoom(), 1.0);
    EXPECT_NE(compare.spreadPan(), 0.0);

    compare.autoFit();
    EXPECT_DOUBLE_EQ(compare.priceZoom(), 1.0);
    EXPECT_DOUBLE_EQ(compare.pricePan(), 0.0);
    EXPECT_DOUBLE_EQ(compare.spreadZoom(), 1.0);
    EXPECT_DOUBLE_EQ(compare.spreadPan(), 0.0);

    std::error_code ec;
    fs::remove_all(dirA, ec);
    fs::remove_all(dirB, ec);
}

TEST(ViewerBookTickerCompare, ValueScaleZoomAnchorsAtCursorFraction) {
    BookTickerCompareController compare;

    compare.zoomPriceAt(2.0, 1.0);
    EXPECT_DOUBLE_EQ(compare.priceZoom(), 2.0);
    EXPECT_NEAR(compare.pricePan(), 0.25, 0.000001);

    compare.resetValueScale();
    compare.zoomSpreadAt(2.0, 0.0);
    EXPECT_DOUBLE_EQ(compare.spreadZoom(), 2.0);
    EXPECT_NEAR(compare.spreadPan(), -0.25, 0.000001);
}

TEST(ViewerBookTickerCompare, PersistsMeanWindowSeconds) {
    isolateViewerSettings(QStringLiteral("mean_window"));

    {
        BookTickerCompareController compare;
        EXPECT_NEAR(compare.meanWindowSeconds(), 5.0, 0.000001);
        compare.setMeanWindowSeconds(12.5);
    }

    BookTickerCompareController restored;
    EXPECT_NEAR(restored.meanWindowSeconds(), 12.5, 0.000001);
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

TEST(ViewerLiveSource, RepeatedActivationKeepsManualViewport) {
    hftrec::storage::LiveEventStore store{};
    ASSERT_EQ(store.appendTrade(tradeRow(100, 1, e8(100), e8(2))), hftrec::Status::Ok);
    StubIngress ingress(&store);

    auto& registry = LiveDataRegistry::instance();
    registry.setSources({
        {"live:test:futures:SOLUSDT", "Test", "Futures", "SOLUSDT", "s4", {}, &ingress},
    });

    ChartController chart;
    ASSERT_TRUE(chart.activateLiveSource(QStringLiteral("live:test:futures:SOLUSDT"), QString{}));
    chart.refreshLiveDataWindow(0, 200);
    chart.setViewport(10, 110, e8(90), e8(120));

    ASSERT_TRUE(chart.activateLiveSource(QStringLiteral("live:test:futures:SOLUSDT"), QString{}));
    EXPECT_EQ(chart.tsMin(), 10);
    EXPECT_EQ(chart.tsMax(), 110);
    EXPECT_EQ(chart.priceMinE8(), e8(90));
    EXPECT_EQ(chart.priceMaxE8(), e8(120));

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
