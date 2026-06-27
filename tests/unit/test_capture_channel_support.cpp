#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/capture/CaptureChannelSupport.hpp"

namespace {

using hftrec::capture::CaptureChannel;
using hftrec::capture::CaptureChannelDecision;
using hftrec::capture::CaptureChannelSkipReason;
using hftrec::capture::CaptureConfig;

bool rejectReferenceStreams(const CaptureConfig&, CaptureChannel channel, std::string& detail, void*) {
    if (channel == CaptureChannel::Funding || channel == CaptureChannel::PriceLimit) {
        detail = "missing route";
        return false;
    }
    return true;
}

bool rejectAllStreams(const CaptureConfig&, CaptureChannel, std::string& detail, void*) {
    detail = "not wired";
    return false;
}

std::vector<CaptureChannel> allRecorderChannels() {
    return {
        CaptureChannel::Trades,
        CaptureChannel::Liquidations,
        CaptureChannel::BookTicker,
        CaptureChannel::Orderbook,
        CaptureChannel::MarkPrice,
        CaptureChannel::IndexPrice,
        CaptureChannel::Funding,
        CaptureChannel::PriceLimit,
    };
}

const CaptureChannelDecision* findDecision(const hftrec::capture::CaptureLaunchPlan& plan,
                                           CaptureChannel channel) {
    for (const auto& decision : plan.decisions) {
        if (decision.channel == channel) return &decision;
    }
    return nullptr;
}

}  // namespace

TEST(CaptureChannelSupport, KeepsSupportedChannelsAndReportsSkippedOnes) {
    CaptureConfig config{};
    config.exchange = "gate";
    config.market = "futures";
    config.symbols = {"BTCUSDT"};

    const auto plan = hftrec::capture::buildCaptureLaunchPlan(
        config,
        allRecorderChannels(),
        rejectReferenceStreams,
        nullptr);

    EXPECT_TRUE(plan.anyEnabled());
    EXPECT_FALSE(plan.allRequestedEnabled());

    const auto* trades = findDecision(plan, CaptureChannel::Trades);
    ASSERT_NE(trades, nullptr);
    EXPECT_TRUE(trades->enabled);
    EXPECT_FALSE(trades->skipped);

    const auto* funding = findDecision(plan, CaptureChannel::Funding);
    ASSERT_NE(funding, nullptr);
    EXPECT_FALSE(funding->enabled);
    EXPECT_TRUE(funding->skipped);
    EXPECT_EQ(funding->reason, CaptureChannelSkipReason::UnsupportedRoute);
    EXPECT_NE(funding->detail.find("missing route"), std::string::npos);

    EXPECT_NE(plan.skippedSummary().find("funding"), std::string::npos);
    EXPECT_NE(plan.skippedSummary().find("price_limit"), std::string::npos);
}

TEST(CaptureChannelSupport, MarksPlanAsEmptyWhenNoChannelsRemain) {
    CaptureConfig config{};
    config.exchange = "toobit";
    config.market = "spot";
    config.symbols = {"MUSDT"};

    const auto plan = hftrec::capture::buildCaptureLaunchPlan(
        config,
        allRecorderChannels(),
        rejectAllStreams,
        nullptr);

    EXPECT_FALSE(plan.anyEnabled());
    EXPECT_FALSE(plan.allRequestedEnabled());
    EXPECT_NE(plan.skippedSummary().find("trades"), std::string::npos);
    EXPECT_NE(plan.skippedSummary().find("not wired"), std::string::npos);
}

#if HFTREC_WITH_CXET
TEST(CaptureChannelSupport, HyperliquidFuturesMarketDataChannelsAreRuntimeReady) {
    CaptureConfig config{};
    config.exchange = "hyperliquid";
    config.market = "futures";
    config.symbols = {"BTC"};

    std::string detail;
    EXPECT_TRUE(hftrec::capture::captureChannelRuntimeReady(config, CaptureChannel::Trades, detail)) << detail;
    EXPECT_TRUE(hftrec::capture::captureChannelRuntimeReady(config, CaptureChannel::BookTicker, detail)) << detail;
    EXPECT_TRUE(hftrec::capture::captureChannelRuntimeReady(config, CaptureChannel::Orderbook, detail)) << detail;
}
#endif

}  // namespace
