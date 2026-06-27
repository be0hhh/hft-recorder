#include <gtest/gtest.h>

#include "app/capture_cli.cpp"

namespace hftrec::app {
namespace {

TEST(CaptureCli, DefaultsTradesWarmupToLiveOnly) {
    const auto config = makeDefaultConfig();

    EXPECT_EQ(config.tradesHistoryWarmupSec, 0);
}

}  // namespace
}  // namespace hftrec::app
