#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "gui/viewer/StrategyOverlay.hpp"

namespace fs = std::filesystem;

namespace {

constexpr std::int64_t kScaleE8 = 100000000ll;

std::int64_t e8(std::int64_t value) {
    return value * kScaleE8;
}

fs::path makeTmpDir() {
    const auto dir = fs::temp_directory_path() / ("hftrec_strategy_overlay_" + std::to_string(std::rand()));
    fs::create_directories(dir);
    return dir;
}

void writeFile(const fs::path& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << data;
}

fs::path makeRunResult(const fs::path& parent,
                       const std::string& runId,
                       const std::string& ordersJsonl,
                       const std::string& fillsJsonl) {
    const fs::path resultPath = parent / runId;
    fs::create_directories(resultPath);
    writeFile(resultPath / "manifest.json", std::string{"{\"type\":\"run.result.v2\",\"run_id\":\""} + runId + "\",\"strategy\":\"spread_maker1and2\",\"session_path\":\"/tmp/session-a\",\"summary\":{},\"errors\":[]}");
    writeFile(resultPath / "orders.jsonl", ordersJsonl);
    writeFile(resultPath / "fills.jsonl", fillsJsonl);
    writeFile(resultPath / "equity.jsonl", "");
    return resultPath;
}

}  // namespace

TEST(StrategyOverlay, AcceptsRunManifestWithUint64LatencySeed) {
    const auto dir = makeTmpDir();
    const fs::path resultPath = dir / "spread-seed";
    fs::create_directories(resultPath);
    writeFile(resultPath / "manifest.json",
              "{\n"
              "  \"type\": \"run.result.v2\",\n"
              "  \"run_id\": \"spread-seed\",\n"
              "  \"status\": \"complete\",\n"
              "  \"strategy\": \"spread_maker1and2\",\n"
              "  \"session_path\": \"/tmp/session-a\",\n"
              "  \"execution\": {\"latency_seed\": 16501133602812054649},\n"
              "  \"summary\": {},\n"
              "  \"errors\": []\n"
              "}\n");
    writeFile(resultPath / "orders.jsonl",
              "[1,0,900,1000,1000,1,1,1,3,9900000000,100000000,0]\n");
    writeFile(resultPath / "fills.jsonl",
              "[1,1000,1100,1,9900000000,100000000,0,0]\n");

    hftrec::gui::viewer::StrategyOverlayData overlay;
    std::string error;
    ASSERT_TRUE(hftrec::gui::viewer::loadStrategyOverlayFromResult(resultPath, 9000, overlay, error)) << error;
    EXPECT_EQ(overlay.runId, QStringLiteral("spread-seed"));
    EXPECT_EQ(overlay.strategy, QStringLiteral("spread_maker1and2"));
    ASSERT_EQ(overlay.fillMarkers.size(), 1u);
    EXPECT_EQ(overlay.fillMarkers[0].tsNs, 1100);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(StrategyOverlay, MaterializesLimitLifetimesAndFillMarkers) {
    const auto dir = makeTmpDir();
    const auto resultPath = makeRunResult(dir, "run-a",
        "[1,0,900,1000,1000,1,1,1,2,9900000000,100000000,0]\n"
        "[2,1,1900,2000,2000,3,0,0,2,0,0,0]\n"
        "[3,0,2900,3000,3000,1,0,1,3,10500000000,200000000,0]\n"
        "[4,0,3900,4000,4000,1,1,2,3,0,300000000,0]\n"
        "[5,0,4900,5000,5000,1,0,1,2,11000000000,100000000,0]\n"
        "[7,5,5900,6000,6000,3,0,0,2,0,0,0]\n"
        "[7,5,5900,6000,6000,1,0,1,3,11200000000,100000000,0]\n",
        "[3,3000,3500,0,10500000000,200000000,0,0]\n"
        "[4,4000,4100,1,10100000000,300000000,0,1]\n"
        "[7,6000,6500,0,11200000000,100000000,0,0]\n");

    hftrec::gui::viewer::StrategyOverlayData overlay;
    std::string error;
    ASSERT_TRUE(hftrec::gui::viewer::loadStrategyOverlayFromResult(resultPath, 9000, overlay, error)) << error;

    ASSERT_EQ(overlay.orderSegments.size(), 4u);
    EXPECT_EQ(overlay.orderSegments[0].tsStartNs, 1000);
    EXPECT_EQ(overlay.orderSegments[0].tsEndNs, 2000);
    EXPECT_EQ(overlay.orderSegments[0].priceE8, e8(99));
    EXPECT_TRUE(overlay.orderSegments[0].sideBuy);
    EXPECT_FALSE(overlay.orderSegments[0].openEnded);

    EXPECT_EQ(overlay.orderSegments[1].tsStartNs, 3000);
    EXPECT_EQ(overlay.orderSegments[1].tsEndNs, 3500);
    EXPECT_EQ(overlay.orderSegments[1].priceE8, e8(105));
    EXPECT_FALSE(overlay.orderSegments[1].sideBuy);

    EXPECT_EQ(overlay.orderSegments[2].tsStartNs, 5000);
    EXPECT_EQ(overlay.orderSegments[2].tsEndNs, 6000);
    EXPECT_EQ(overlay.orderSegments[2].priceE8, e8(110));
    EXPECT_FALSE(overlay.orderSegments[2].openEnded);

    EXPECT_EQ(overlay.orderSegments[3].tsStartNs, 6000);
    EXPECT_EQ(overlay.orderSegments[3].tsEndNs, 6500);
    EXPECT_EQ(overlay.orderSegments[3].priceE8, e8(112));
    EXPECT_FALSE(overlay.orderSegments[3].sideBuy);
    EXPECT_FALSE(overlay.orderSegments[3].openEnded);

    ASSERT_EQ(overlay.fillMarkers.size(), 3u);
    EXPECT_EQ(overlay.fillMarkers[0].tsNs, 3500);
    EXPECT_EQ(overlay.fillMarkers[0].priceE8, e8(105));
    EXPECT_FALSE(overlay.fillMarkers[0].sideBuy);
    EXPECT_FALSE(overlay.fillMarkers[0].marketOrder);
    EXPECT_EQ(overlay.fillMarkers[0].shape, hftrec::gui::viewer::StrategyFillShape::SellDown);

    EXPECT_EQ(overlay.fillMarkers[1].tsNs, 4100);
    EXPECT_EQ(overlay.fillMarkers[1].priceE8, e8(101));
    EXPECT_TRUE(overlay.fillMarkers[1].sideBuy);
    EXPECT_TRUE(overlay.fillMarkers[1].marketOrder);
    EXPECT_EQ(overlay.fillMarkers[1].shape, hftrec::gui::viewer::StrategyFillShape::BuyUp);

    EXPECT_EQ(overlay.fillMarkers[2].tsNs, 6500);
    EXPECT_EQ(overlay.fillMarkers[2].priceE8, e8(112));
    EXPECT_FALSE(overlay.fillMarkers[2].sideBuy);
    EXPECT_FALSE(overlay.fillMarkers[2].marketOrder);
    EXPECT_EQ(overlay.fillMarkers[2].shape, hftrec::gui::viewer::StrategyFillShape::SellDown);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(StrategyOverlay, LoadsStrategyRangeRows) {
    const auto dir = makeTmpDir();
    const auto resultPath = makeRunResult(dir, "run-range", "", "");
    writeFile(resultPath / "strategy_range.jsonl",
              "[1000,9900000000,10000000000,10100000000]\n"
              "[2000,9950000000,10050000000,10150000000]\n");

    hftrec::gui::viewer::StrategyOverlayData overlay;
    std::string error;
    ASSERT_TRUE(hftrec::gui::viewer::loadStrategyOverlayFromResult(resultPath, 9000, overlay, error)) << error;

    ASSERT_EQ(overlay.rangePoints.size(), 2u);
    EXPECT_EQ(overlay.rangePoints[0].tsNs, 1000);
    EXPECT_EQ(overlay.rangePoints[0].lowE8, e8(99));
    EXPECT_EQ(overlay.rangePoints[0].midE8, e8(100));
    EXPECT_EQ(overlay.rangePoints[0].highE8, e8(101));
    EXPECT_EQ(overlay.rangePoints[1].tsNs, 2000);
    EXPECT_EQ(overlay.rangePoints[1].lowE8, 9950000000ll);
    EXPECT_EQ(overlay.rangePoints[1].midE8, 10050000000ll);
    EXPECT_EQ(overlay.rangePoints[1].highE8, 10150000000ll);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(StrategyOverlay, AcceptsBacktestRowsWithTrailingLegIndex) {
    const auto dir = makeTmpDir();
    const auto resultPath = makeRunResult(dir, "run-leg-index",
        "[10,0,900,1000,1000,1,1,1,3,9900000000,100000000,0,1]\n",
        "[10,1000,1100,1,9900000000,100000000,0,0,1]\n");

    hftrec::gui::viewer::StrategyOverlayData overlay;
    std::string error;
    ASSERT_TRUE(hftrec::gui::viewer::loadStrategyOverlayFromResult(resultPath, 9000, overlay, error)) << error;

    ASSERT_EQ(overlay.orderSegments.size(), 1u);
    EXPECT_EQ(overlay.orderSegments[0].legIndex, 1u);
    ASSERT_EQ(overlay.fillMarkers.size(), 1u);
    EXPECT_EQ(overlay.fillMarkers[0].tsNs, 1100);
    EXPECT_EQ(overlay.fillMarkers[0].priceE8, e8(99));
    EXPECT_TRUE(overlay.fillMarkers[0].sideBuy);
    EXPECT_EQ(overlay.fillMarkers[0].legIndex, 1u);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(StrategyOverlay, DefaultsMissingTrailingLegIndexToPrimaryLeg) {
    const auto dir = makeTmpDir();
    const auto resultPath = makeRunResult(dir, "run-legacy-leg-index",
        "[10,0,900,1000,1000,1,1,1,3,9900000000,100000000,0]\n",
        "[10,1000,1100,1,9900000000,100000000,0,0]\n");

    hftrec::gui::viewer::StrategyOverlayData overlay;
    std::string error;
    ASSERT_TRUE(hftrec::gui::viewer::loadStrategyOverlayFromResult(resultPath, 9000, overlay, error)) << error;

    ASSERT_EQ(overlay.orderSegments.size(), 1u);
    EXPECT_EQ(overlay.orderSegments[0].legIndex, 0u);
    ASSERT_EQ(overlay.fillMarkers.size(), 1u);
    EXPECT_EQ(overlay.fillMarkers[0].legIndex, 0u);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(StrategyOverlay, DoesNotStretchInstantFilledLimitToRunEnd) {
    const auto dir = makeTmpDir();
    const auto resultPath = makeRunResult(dir, "run-instant-fill",
        "[10,0,900,1000,1000,1,1,1,3,9900000000,100000000,0]\n",
        "[10,1000,1000,1,9900000000,100000000,0,0]\n");

    hftrec::gui::viewer::StrategyOverlayData overlay;
    std::string error;
    ASSERT_TRUE(hftrec::gui::viewer::loadStrategyOverlayFromResult(resultPath, 9000, overlay, error)) << error;

    EXPECT_TRUE(overlay.orderSegments.empty());
    ASSERT_EQ(overlay.fillMarkers.size(), 1u);
    EXPECT_EQ(overlay.fillMarkers[0].tsNs, 1000);
    EXPECT_EQ(overlay.fillMarkers[0].priceE8, e8(99));
    EXPECT_TRUE(overlay.fillMarkers[0].sideBuy);

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(StrategyOverlay, DoesNotTreatFilledLimitWithoutFillRowAsOpenEnded) {
    const auto dir = makeTmpDir();
    const auto resultPath = makeRunResult(dir, "run-filled-without-fill",
        "[20,0,900,1000,1000,1,0,1,3,10500000000,200000000,0]\n",
        "");

    hftrec::gui::viewer::StrategyOverlayData overlay;
    std::string error;
    ASSERT_TRUE(hftrec::gui::viewer::loadStrategyOverlayFromResult(resultPath, 9000, overlay, error)) << error;

    EXPECT_TRUE(overlay.orderSegments.empty());
    EXPECT_TRUE(overlay.fillMarkers.empty());

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(StrategyOverlay, DoesNotTreatCancelledLimitWithoutEndAsOpenEnded) {
    const auto dir = makeTmpDir();
    const auto resultPath = makeRunResult(dir, "run-cancelled-without-end",
        "[21,0,900,1000,1000,1,0,1,4,10500000000,200000000,0]\n",
        "");

    hftrec::gui::viewer::StrategyOverlayData overlay;
    std::string error;
    ASSERT_TRUE(hftrec::gui::viewer::loadStrategyOverlayFromResult(resultPath, 9000, overlay, error)) << error;

    EXPECT_TRUE(overlay.orderSegments.empty());
    EXPECT_TRUE(overlay.fillMarkers.empty());

    std::error_code ec;
    fs::remove_all(dir, ec);
}

TEST(StrategyOverlay, DoesNotStretchSameTimestampReplaceToRunEnd) {
    const auto dir = makeTmpDir();
    const auto resultPath = makeRunResult(dir, "run-same-timestamp-replace",
        "[30,0,900,1000,1000,1,0,1,2,10500000000,200000000,0]\n"
        "[31,30,950,1000,1000,3,0,0,2,0,0,0]\n"
        "[31,30,950,1000,1000,1,0,1,2,10600000000,200000000,0]\n",
        "");

    hftrec::gui::viewer::StrategyOverlayData overlay;
    std::string error;
    ASSERT_TRUE(hftrec::gui::viewer::loadStrategyOverlayFromResult(resultPath, 9000, overlay, error)) << error;

    ASSERT_EQ(overlay.orderSegments.size(), 1u);
    EXPECT_EQ(overlay.orderSegments[0].tsStartNs, 1000);
    EXPECT_EQ(overlay.orderSegments[0].priceE8, e8(106));
    EXPECT_TRUE(overlay.orderSegments[0].openEnded);

    std::error_code ec;
    fs::remove_all(dir, ec);
}
