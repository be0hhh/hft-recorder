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

}  // namespace

TEST(StrategyOverlay, MaterializesLimitLifetimesAndFillMarkers) {
    const auto dir = makeTmpDir();
    const auto resultPath = dir / "run-a.json";
    writeFile(resultPath, R"json({
      "type":"run.result",
      "run_id":"run-a",
      "strategy":"spread_maker1and2",
      "session_path":"/tmp/session-a",
      "orders":[
        {"id":1,"target_id":0,"submit_ts_ns":900,"active_ts_ns":1000,"action":"order","side":"buy","type":"limit","status":"active","price_e8":9900000000,"qty_e8":100000000,"reduce_only":false},
        {"id":2,"target_id":1,"submit_ts_ns":1900,"active_ts_ns":2000,"action":"cancel","side":"","type":"","status":"acked","price_e8":0,"qty_e8":0,"reduce_only":false},
        {"id":3,"target_id":0,"submit_ts_ns":2900,"active_ts_ns":3000,"action":"order","side":"sell","type":"limit","status":"filled","price_e8":10500000000,"qty_e8":200000000,"reduce_only":false},
        {"id":4,"target_id":0,"submit_ts_ns":3900,"active_ts_ns":4000,"action":"order","side":"buy","type":"market","status":"filled","price_e8":0,"qty_e8":300000000,"reduce_only":false},
        {"id":5,"target_id":0,"submit_ts_ns":4900,"active_ts_ns":5000,"action":"order","side":"sell","type":"limit","status":"active","price_e8":11000000000,"qty_e8":100000000,"reduce_only":false},
        {"id":6,"target_id":5,"submit_ts_ns":5400,"active_ts_ns":5500,"action":"amend","side":"sell","type":"limit","status":"missing","price_e8":11100000000,"qty_e8":100000000,"reduce_only":false},
        {"id":7,"target_id":5,"submit_ts_ns":5900,"active_ts_ns":6000,"action":"amend","side":"sell","type":"limit","status":"acked","price_e8":11200000000,"qty_e8":100000000,"reduce_only":false}
      ],
      "fills":[
        {"order_id":3,"entry_ts_ns":3000,"exit_ts_ns":3500,"side":"sell","price_e8":10500000000,"qty_e8":200000000,"realized_pnl_e8":0,"reduce_only":false},
        {"order_id":4,"entry_ts_ns":4000,"exit_ts_ns":4100,"side":"buy","price_e8":10100000000,"qty_e8":300000000,"realized_pnl_e8":0,"reduce_only":false},
        {"order_id":7,"entry_ts_ns":6000,"exit_ts_ns":6500,"side":"sell","price_e8":11200000000,"qty_e8":100000000,"realized_pnl_e8":0,"reduce_only":false}
      ],
      "summary":{},
      "errors":[]
    })json");

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

