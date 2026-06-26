#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#include "gui/viewer/RateLimitUsage.hpp"

namespace fs = std::filesystem;

namespace {

fs::path makeTmpDir() {
    const auto dir = fs::temp_directory_path() / ("hftrec_rate_limit_usage_" + std::to_string(std::rand()));
    fs::create_directories(dir);
    return dir;
}

void writeFile(const fs::path& path, const std::string& data) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << data;
}

}  // namespace

TEST(RateLimitUsage, LoadsManifestStreamAndSortsRows) {
    const fs::path dir = makeTmpDir();
    writeFile(dir / "manifest.json",
              "{\"type\":\"run.result.v2\",\"streams\":{\"rate_limit_usage\":{"
              "\"path\":\"rate_limit_usage.jsonl\",\"rows\":3}}}\n");
    writeFile(dir / "rate_limit_usage.jsonl",
              "[2000,1,500000,5,10,2,10000000000,0]\n"
              "[1000,0,900000,9,10,2,10000000000,0]\n"
              "[2000,0,0,0,10,2,10000000000,1]\n");

    hftrec::gui::viewer::RateLimitUsageData data;
    std::string error;
    ASSERT_TRUE(hftrec::gui::viewer::loadRateLimitUsageFromResult(dir, data, error)) << error;

    ASSERT_EQ(data.points.size(), 3u);
    EXPECT_EQ(data.legCount, 2u);
    EXPECT_EQ(data.points[0].tsNs, 1000);
    EXPECT_EQ(data.points[0].legIndex, 0u);
    EXPECT_EQ(data.points[1].tsNs, 2000);
    EXPECT_EQ(data.points[1].legIndex, 0u);
    EXPECT_TRUE(data.points[1].exhausted);
    EXPECT_EQ(data.points[2].legIndex, 1u);
}

TEST(RateLimitUsage, MissingStreamIsEmptyNotFailure) {
    const fs::path dir = makeTmpDir();
    writeFile(dir / "manifest.json", "{\"type\":\"run.result.v2\",\"streams\":{}}\n");

    hftrec::gui::viewer::RateLimitUsageData data;
    std::string error;
    ASSERT_TRUE(hftrec::gui::viewer::loadRateLimitUsageFromResult(dir, data, error)) << error;
    EXPECT_TRUE(data.empty());
}

TEST(RateLimitUsage, MinRemainingPctUsesWorstCurrentLeg) {
    hftrec::gui::viewer::RateLimitUsageData data;
    data.legCount = 2u;
    data.points = {
        hftrec::gui::viewer::RateLimitUsagePoint{.tsNs = 1000, .legIndex = 0u, .minRemainingPctE4 = 900000},
        hftrec::gui::viewer::RateLimitUsagePoint{.tsNs = 2000, .legIndex = 1u, .minRemainingPctE4 = 500000},
        hftrec::gui::viewer::RateLimitUsagePoint{.tsNs = 3000, .legIndex = 0u, .minRemainingPctE4 = 800000},
        hftrec::gui::viewer::RateLimitUsagePoint{.tsNs = 4000, .legIndex = 1u, .minRemainingPctE4 = 950000},
    };

    EXPECT_EQ(hftrec::gui::viewer::rateLimitMinRemainingPctE4At(data, 500), 1000000);
    EXPECT_EQ(hftrec::gui::viewer::rateLimitMinRemainingPctE4At(data, 1500), 900000);
    EXPECT_EQ(hftrec::gui::viewer::rateLimitMinRemainingPctE4At(data, 2500), 500000);
    EXPECT_EQ(hftrec::gui::viewer::rateLimitMinRemainingPctE4At(data, 3500), 500000);
    EXPECT_EQ(hftrec::gui::viewer::rateLimitMinRemainingPctE4At(data, 4500), 800000);
}
