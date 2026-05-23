#include <gtest/gtest.h>

#include <filesystem>

#include "hftrec/recorder_api.hpp"

namespace fs = std::filesystem;

namespace {

fs::path fixtureSession(const char* name) {
    return fs::path(HFTRREC_SOURCE_DIR) / "tests" / "fixtures" / "session_corpus" / name;
}

}  // namespace

TEST(RecorderApi, LoadsSingleSessionThroughPublicHeader) {
    hftrec::RecorderSessionSet sessions;
    hftrec::RecorderLoadRequest request{};
    request.primarySessionPath = fixtureSession("clean_full");
    request.channels = hftrec::RecorderChannel_AllMarketData;

    const auto status = hftrec::loadRecorderSessions(request, sessions);

    ASSERT_EQ(status, hftrec::Status::Ok);
    EXPECT_FALSE(sessions.hasSecondary());
    EXPECT_EQ(sessions.primary().summary().status, hftrec::Status::Ok);
    EXPECT_GT(sessions.primary().summary().trades, 0u);
    EXPECT_GT(sessions.primary().summary().bookTickers, 0u);
    EXPECT_GT(sessions.primary().summary().depths, 0u);
    EXPECT_GT(sessions.primary().summary().timelineEvents, 0u);
    EXPECT_EQ(sessions.primary().summary().trades, sessions.primary().trades().size());
}

TEST(RecorderApi, LoadsTwoSessionsThroughOneRequest) {
    hftrec::RecorderSessionSet sessions;
    hftrec::RecorderLoadRequest request{};
    request.primarySessionPath = fixtureSession("clean_full");
    request.secondarySessionPath = fixtureSession("clean_full");
    request.channels = hftrec::RecorderChannel_BookTicker;

    const auto status = hftrec::loadRecorderSessions(request, sessions);

    ASSERT_EQ(status, hftrec::Status::Ok);
    ASSERT_TRUE(sessions.hasSecondary());
    EXPECT_GT(sessions.primary().bookTickers().size(), 0u);
    EXPECT_GT(sessions.secondary().bookTickers().size(), 0u);
    EXPECT_TRUE(sessions.primary().trades().empty());
    EXPECT_TRUE(sessions.secondary().trades().empty());
    EXPECT_EQ(sessions.primary().summary().bookTickers, sessions.secondary().summary().bookTickers);
}

TEST(RecorderApi, ReportsMissingSessionAsStructuredError) {
    hftrec::RecorderSessionSet sessions;
    hftrec::RecorderLoadRequest request{};
    request.primarySessionPath = fixtureSession("missing_session_for_public_api_test");

    const auto status = hftrec::loadRecorderSessions(request, sessions);

    EXPECT_EQ(status, hftrec::Status::InvalidArgument);
    EXPECT_EQ(sessions.primary().summary().status, hftrec::Status::InvalidArgument);
    EXPECT_FALSE(sessions.primary().summary().error.empty());
}