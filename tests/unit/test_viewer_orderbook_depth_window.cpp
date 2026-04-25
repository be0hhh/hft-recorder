#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <QPointF>
#include <QString>

#include "gui/viewer/ChartController.hpp"
#include "gui/viewer/hit_test/HoverDetection.hpp"

namespace fs = std::filesystem;

namespace {

using hftrec::gui::viewer::BookLevel;
using hftrec::gui::viewer::ChartController;
using hftrec::gui::viewer::HoverInfo;
using hftrec::gui::viewer::RenderSnapshot;
using hftrec::gui::viewer::SnapshotInputs;

constexpr std::int64_t kScaleE8 = 100000000ll;

std::int64_t e8(std::int64_t value) {
    return value * kScaleE8;
}

fs::path makeTmpDir() {
    const auto base = fs::temp_directory_path();
    auto dir = base / ("hftrec_viewer_depth_window_" + std::to_string(std::rand()));
    fs::create_directories(dir);
    return dir;
}

void writeFile(const fs::path& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << data;
}

std::string level(std::int64_t priceE8, std::int64_t qtyE8) {
    return "[" + std::to_string(priceE8) + "," + std::to_string(qtyE8) + ",0,0]";
}

std::string depthLine(std::int64_t tsNs,
                      std::int64_t updateId,
                      const std::vector<BookLevel>& bids,
                      const std::vector<BookLevel>& asks) {
    std::string out = "[[";
    for (std::size_t i = 0; i < bids.size(); ++i) {
        if (i != 0u) out += ",";
        out += level(bids[i].priceE8, bids[i].qtyE8);
    }
    out += "],[";
    for (std::size_t i = 0; i < asks.size(); ++i) {
        if (i != 0u) out += ",";
        out += level(asks[i].priceE8, asks[i].qtyE8);
    }
    out += "]," + std::to_string(tsNs)
        + ",\"BTCUSDT\",\"binance\",\"futures_usd\","
        + std::to_string(updateId)
        + "," + std::to_string(updateId)
        + "," + std::to_string(updateId)
        + "," + std::to_string(updateId)
        + "]\n";
    return out;
}

std::string bookTickerLine(std::int64_t tsNs,
                           std::int64_t bidPriceE8,
                           std::int64_t askPriceE8) {
    return "[" + std::to_string(bidPriceE8)
        + "," + std::to_string(e8(2))
        + "," + std::to_string(askPriceE8)
        + "," + std::to_string(e8(2))
        + "," + std::to_string(tsNs)
        + ",\"BTCUSDT\",\"binance\",\"futures_usd\""
        + ",100,100]\n";
}

std::string tradeLine(std::int64_t tsNs, std::int64_t priceE8) {
    return "[" + std::to_string(priceE8)
        + "," + std::to_string(e8(1))
        + ",1"
        + "," + std::to_string(tsNs)
        + ",0,0,0,0,0,\"BTCUSDT\",\"binance\",\"futures_usd\",200,200]\n";
}

SnapshotInputs orderbookInputs(qreal depthWindowPct) {
    SnapshotInputs in{};
    in.tradesVisible = false;
    in.orderbookVisible = true;
    in.bookTickerVisible = false;
    in.exactTradeRendering = true;
    in.bookOpacityGain = 100.0;
    in.bookRenderDetail = 100.0;
    in.bookDepthWindowPct = depthWindowPct;
    return in;
}

RenderSnapshot buildSnapshot(const std::vector<BookLevel>& bids,
                             const std::vector<BookLevel>& asks,
                             std::string bookTickerJson,
                             qreal depthWindowPct,
                             qreal heightPx = 800.0) {
    const auto dir = makeTmpDir();
    writeFile(dir / "depth.jsonl", depthLine(1000, 1, bids, asks));
    writeFile(dir / "trades.jsonl", tradeLine(2000, e8(10000)));
    if (!bookTickerJson.empty()) {
        writeFile(dir / "bookticker.jsonl", bookTickerJson);
    }

    ChartController chart;
    EXPECT_TRUE(chart.addDepthFile(QString::fromStdString((dir / "depth.jsonl").string())));
    EXPECT_TRUE(chart.addTradesFile(QString::fromStdString((dir / "trades.jsonl").string())));
    if (!bookTickerJson.empty()) {
        EXPECT_TRUE(chart.addBookTickerFile(QString::fromStdString((dir / "bookticker.jsonl").string())));
    }
    chart.finalizeFiles();
    EXPECT_TRUE(chart.loaded());
    chart.setViewport(1000, 2000, e8(7000), e8(13000));

    auto snap = chart.buildSnapshot(800.0, heightPx, orderbookInputs(depthWindowPct));
    std::error_code ec;
    fs::remove_all(dir, ec);
    return snap;
}

bool containsPrice(const std::vector<BookLevel>& levels, std::int64_t priceE8) {
    return std::any_of(levels.begin(), levels.end(), [&](const BookLevel& level) {
        return level.priceE8 == priceE8;
    });
}

const hftrec::gui::viewer::BookSegment& onlySegment(const RenderSnapshot& snap) {
    EXPECT_EQ(snap.bookSegments.size(), 1u);
    return snap.bookSegments.front();
}

TEST(ViewerOrderbookDepthWindow, FiltersAroundBookTickerCheckpoint) {
    const auto qty = e8(2);
    const auto snap = buildSnapshot(
        {{e8(10000), qty}, {e8(9600), qty}, {e8(9400), qty}},
        {{e8(10100), qty}, {e8(10500), qty}, {e8(10700), qty}},
        bookTickerLine(1000, e8(10000), e8(10100)),
        5.0);
    const auto& seg = onlySegment(snap);

    EXPECT_TRUE(containsPrice(seg.bids, e8(10000)));
    EXPECT_TRUE(containsPrice(seg.bids, e8(9600)));
    EXPECT_FALSE(containsPrice(seg.bids, e8(9400)));
    EXPECT_TRUE(containsPrice(seg.asks, e8(10100)));
    EXPECT_TRUE(containsPrice(seg.asks, e8(10500)));
    EXPECT_FALSE(containsPrice(seg.asks, e8(10700)));
}

TEST(ViewerOrderbookDepthWindow, FallsBackToCurrentBookBestWithoutTicker) {
    const auto qty = e8(2);
    const auto snap = buildSnapshot(
        {{e8(10000), qty}, {e8(9600), qty}, {e8(9400), qty}},
        {{e8(10100), qty}, {e8(10500), qty}, {e8(10700), qty}},
        {},
        5.0);
    const auto& seg = onlySegment(snap);

    EXPECT_TRUE(containsPrice(seg.bids, e8(9600)));
    EXPECT_FALSE(containsPrice(seg.bids, e8(9400)));
    EXPECT_TRUE(containsPrice(seg.asks, e8(10500)));
    EXPECT_FALSE(containsPrice(seg.asks, e8(10700)));
}

TEST(ViewerOrderbookDepthWindow, UsesBookTickerAnchorWhenBookBestIsStale) {
    const auto qty = e8(2);
    const auto snap = buildSnapshot(
        {{e8(10000), qty}, {e8(9000), qty}, {e8(8400), qty}},
        {{e8(10100), qty}, {e8(11100), qty}, {e8(11700), qty}},
        bookTickerLine(1000, e8(9000), e8(11100)),
        5.0);
    const auto& seg = onlySegment(snap);

    EXPECT_TRUE(containsPrice(seg.bids, e8(10000)));
    EXPECT_TRUE(containsPrice(seg.bids, e8(9000)));
    EXPECT_FALSE(containsPrice(seg.bids, e8(8400)));
    EXPECT_TRUE(containsPrice(seg.asks, e8(10100)));
    EXPECT_TRUE(containsPrice(seg.asks, e8(11100)));
    EXPECT_FALSE(containsPrice(seg.asks, e8(11700)));
}

TEST(ViewerOrderbookDepthWindow, ClampsPercentRange) {
    const auto qty = e8(2);
    const std::vector<BookLevel> bids{{e8(10000), qty}, {e8(9899), qty}, {e8(7600), qty}, {e8(7400), qty}};
    const std::vector<BookLevel> asks{{e8(10100), qty}, {e8(10202), qty}, {e8(12600), qty}, {e8(12700), qty}};

    const auto low = buildSnapshot(bids, asks, {}, 0.0);
    EXPECT_DOUBLE_EQ(low.bookDepthWindowPct, 1.0);
    EXPECT_TRUE(containsPrice(onlySegment(low).bids, e8(10000)));
    EXPECT_FALSE(containsPrice(onlySegment(low).bids, e8(9899)));
    EXPECT_TRUE(containsPrice(onlySegment(low).asks, e8(10100)));
    EXPECT_FALSE(containsPrice(onlySegment(low).asks, e8(10202)));

    const auto high = buildSnapshot(bids, asks, {}, 40.0);
    EXPECT_DOUBLE_EQ(high.bookDepthWindowPct, 25.0);
    EXPECT_TRUE(containsPrice(onlySegment(high).bids, e8(7600)));
    EXPECT_FALSE(containsPrice(onlySegment(high).bids, e8(7400)));
    EXPECT_TRUE(containsPrice(onlySegment(high).asks, e8(12600)));
    EXPECT_FALSE(containsPrice(onlySegment(high).asks, e8(12700)));
}

TEST(ViewerOrderbookDepthWindow, KeepsOneRepresentativePerPixel) {
    const auto qty = e8(2);
    const auto snap = buildSnapshot(
        {{e8(10000), qty}, {e8(9999), qty}},
        {{e8(10100), qty}, {e8(10101), qty}},
        bookTickerLine(1000, e8(10000), e8(10100)),
        5.0,
        600.0);
    const auto& seg = onlySegment(snap);

    ASSERT_EQ(seg.bids.size(), 1u);
    EXPECT_EQ(seg.bids.front().priceE8, e8(10000));
    ASSERT_EQ(seg.asks.size(), 1u);
    EXPECT_EQ(seg.asks.front().priceE8, e8(10100));
}

TEST(ViewerOrderbookHover, UsesSegmentUnderMouseX) {
    RenderSnapshot snap{};
    snap.loaded = true;
    snap.orderbookVisible = true;
    snap.tradesVisible = false;
    snap.vp = hftrec::gui::viewer::ViewportMap{0, 100, 0, e8(1000), 100.0, 100.0};
    snap.bookSegments.push_back(hftrec::gui::viewer::BookSegment{
        0,
        50,
        {{e8(900), e8(1)}},
        {{e8(950), e8(2)}},
    });
    snap.bookSegments.push_back(hftrec::gui::viewer::BookSegment{
        50,
        100,
        {{e8(500), e8(3)}},
        {{e8(550), e8(4)}},
    });

    HoverInfo hover{};
    hftrec::gui::viewer::hit_test::computeHover(
        snap,
        QPointF{25.0, snap.vp.toY(e8(950))},
        true,
        hover);
    EXPECT_EQ(hover.bookKind, 4);
    EXPECT_EQ(hover.bookPriceE8, e8(950));
    EXPECT_EQ(hover.bookQtyE8, e8(2));
    EXPECT_EQ(hover.bookTsNs, 25);

    hftrec::gui::viewer::hit_test::computeHover(
        snap,
        QPointF{75.0, snap.vp.toY(e8(500))},
        true,
        hover);
    EXPECT_EQ(hover.bookKind, 3);
    EXPECT_EQ(hover.bookPriceE8, e8(500));
    EXPECT_EQ(hover.bookQtyE8, e8(3));
    EXPECT_EQ(hover.bookTsNs, 75);
}

}  // namespace
