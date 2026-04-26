#include <gtest/gtest.h>

#include <filesystem>

#include <QString>

#include "gui/viewer/ChartController.hpp"

namespace fs = std::filesystem;

namespace {

fs::path fixtureDir(const char* name) {
    return fs::path(HFTRREC_SOURCE_DIR) / "tests" / "fixtures" / "session_corpus" / name;
}

class CountingLiveDataProvider final : public hftrec::gui::viewer::ILiveDataProvider {
  public:
    void start(const hftrec::gui::viewer::LiveDataProviderConfig&) override { ++startCount; }
    void stop() noexcept override { ++stopCount; }
    hftrec::gui::viewer::LiveDataPollResult pollHot(std::uint64_t) override { return {}; }
    hftrec::gui::viewer::LiveDataBatch materializeRange(const hftrec::gui::viewer::LiveDataRangeRequest&,
                                                        std::uint64_t) const override {
        ++materializeCount;
        return {};
    }
    hftrec::gui::viewer::LiveDataStats stats() const noexcept override { return {}; }

    int startCount{0};
    int stopCount{0};
    mutable int materializeCount{0};
};

}  // namespace

TEST(RecordedRenderOnce, LoadSessionMaterializesRowsAndDoesNotStartLiveProvider) {
    auto provider = std::make_unique<CountingLiveDataProvider>();
    auto* providerPtr = provider.get();

    hftrec::gui::viewer::ChartController chart;
    chart.setLiveDataProvider(std::move(provider));

    ASSERT_TRUE(chart.loadSession(QString::fromStdString(fixtureDir("clean_full").string())));
    EXPECT_TRUE(chart.loaded());
    EXPECT_EQ(chart.replay().trades().size(), 1u);
    EXPECT_EQ(chart.replay().bookTickers().size(), 1u);
    EXPECT_EQ(chart.replay().depths().size(), 2u);
    EXPECT_EQ(providerPtr->startCount, 0);
    chart.refreshLiveDataWindow(0, 4000);
    EXPECT_EQ(providerPtr->materializeCount, 0);
}
