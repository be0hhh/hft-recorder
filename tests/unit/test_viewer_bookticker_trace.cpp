#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include <QPointF>
#include <QString>

#include "gui/viewer/ChartController.hpp"
#include "gui/viewer/hit_test/HoverDetection.hpp"

namespace fs = std::filesystem;

namespace {

using hftrec::gui::viewer::ChartController;
using hftrec::gui::viewer::HoverInfo;
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
                           std::int64_t captureSeq,
                           std::int64_t bidPriceE8,
                           std::int64_t askPriceE8) {
    return "{\"tsNs\":" + std::to_string(tsNs)
        + ",\"captureSeq\":" + std::to_string(captureSeq)
        + ",\"ingestSeq\":" + std::to_string(captureSeq)
        + ",\"bidPriceE8\":" + std::to_string(bidPriceE8)
        + ",\"bidQtyE8\":" + std::to_string(e8(2))
        + ",\"askPriceE8\":" + std::to_string(askPriceE8)
        + ",\"askQtyE8\":" + std::to_string(e8(3)) + "}\n";
}

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

}  // namespace
