#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "gui/viewer/StrategyIndicator.hpp"

namespace fs = std::filesystem;

namespace {

fs::path makeTmpDir() {
    const auto dir = fs::temp_directory_path() / ("hftrec_strategy_indicator_" + std::to_string(std::rand()));
    fs::create_directories(dir);
    return dir;
}

void writeFile(const fs::path& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << data;
}

}  // namespace

TEST(StrategyIndicator, LoadsManifestMetadataAndCompactRows) {
    const fs::path dir = makeTmpDir();
    writeFile(dir / "manifest.json",
              "{\"type\":\"run.result.v2\",\"streams\":{\"strategy_indicator\":{"
              "\"path\":\"strategy_indicator.jsonl\",\"profile\":\"trend_score\","
              "\"title\":\"Trend score\",\"value_label\":\"Score\",\"aux_label\":\"Raw\","
              "\"unit\":\"score\",\"rows\":2}}}\n");
    writeFile(dir / "strategy_indicator.jsonl", "[1000,40,44,0,0,1,0]\n[2000,42,45,1,7,3,1]\n");

    hftrec::gui::viewer::StrategyIndicatorData data;
    std::string error;
    ASSERT_TRUE(hftrec::gui::viewer::loadStrategyIndicatorFromResult(dir, data, error)) << error;

    EXPECT_EQ(data.profile, QStringLiteral("trend_score"));
    EXPECT_EQ(data.title, QStringLiteral("Trend score"));
    EXPECT_EQ(data.valueLabel, QStringLiteral("Score"));
    EXPECT_EQ(data.auxLabel, QStringLiteral("Raw"));
    EXPECT_EQ(data.unit, QStringLiteral("score"));
    ASSERT_EQ(data.points.size(), 2u);
    EXPECT_EQ(data.points[0].tsNs, 1000);
    EXPECT_EQ(data.points[0].valueRaw, 40);
    EXPECT_EQ(data.points[1].auxRaw, 45);
    EXPECT_EQ(data.points[1].eventCode, 1u);
    EXPECT_EQ(data.points[1].reasonRaw, 7u);
    EXPECT_EQ(data.points[1].decisionKind, 3u);
    EXPECT_EQ(data.points[1].sideRaw, 1u);
}

TEST(StrategyIndicator, MissingStreamIsEmptyNotFailure) {
    const fs::path dir = makeTmpDir();
    writeFile(dir / "manifest.json", "{\"type\":\"run.result.v2\",\"streams\":{}}\n");

    hftrec::gui::viewer::StrategyIndicatorData data;
    std::string error;
    ASSERT_TRUE(hftrec::gui::viewer::loadStrategyIndicatorFromResult(dir, data, error)) << error;
    EXPECT_TRUE(data.empty());
}
