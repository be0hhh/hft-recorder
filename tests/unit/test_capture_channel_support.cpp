#include <gtest/gtest.h>

#include <string>
#include <utility>
#include <vector>

#include "core/capture/CaptureChannelSupport.hpp"
#if HFTREC_WITH_CXET
#include "core/capture/CaptureCoordinatorRuntimeHelpers.hpp"
#endif

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

TEST(CaptureChannelSupport, SkippedSummaryCarriesPreflightTransportReason) {
    hftrec::capture::CaptureLaunchPlan plan{};
    CaptureChannelDecision decision{};
    decision.channel = CaptureChannel::Trades;
    decision.requested = true;
    decision.skipped = true;
    decision.reason = CaptureChannelSkipReason::ConnectFailed;
    decision.detail = "trades: route status=connect_failed connect_stage=tcp_connect";
    plan.decisions.push_back(std::move(decision));

    const std::string summary = plan.skippedSummary();
    EXPECT_NE(summary.find("trades:connect_failed"), std::string::npos);
    EXPECT_NE(summary.find("tcp_connect"), std::string::npos);
}

#if HFTREC_WITH_CXET
TEST(CaptureChannelSupport, ClassifiesOnlyTerminalStartupStatusesAsFailFast) {
    using cxet::api::market::PublicMarketDataStatus;

    EXPECT_TRUE(hftrec::capture::runtime::marketDataStatusIsTerminalStartupFailure(PublicMarketDataStatus::ConnectFailed));
    EXPECT_TRUE(hftrec::capture::runtime::marketDataStatusIsTerminalStartupFailure(PublicMarketDataStatus::BadConfig));
    EXPECT_TRUE(hftrec::capture::runtime::marketDataStatusIsTerminalStartupFailure(PublicMarketDataStatus::UnsupportedRoute));
    EXPECT_TRUE(hftrec::capture::runtime::marketDataStatusIsTerminalStartupFailure(PublicMarketDataStatus::SubscribeFailed));

    EXPECT_FALSE(hftrec::capture::runtime::marketDataStatusIsTerminalStartupFailure(PublicMarketDataStatus::NoFrame));
    EXPECT_FALSE(hftrec::capture::runtime::marketDataStatusIsTerminalStartupFailure(PublicMarketDataStatus::Disconnected));
    EXPECT_FALSE(hftrec::capture::runtime::marketDataStatusIsTerminalStartupFailure(PublicMarketDataStatus::Parsed));
}

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

TEST(CaptureChannelSupport, FinamArenaUsesRecorderMarketDataRoutes) {
    CaptureConfig config{};
    config.exchange = "finam_arena";
    config.market = "spot";
    config.symbols = {"SBER@MISX"};

    std::string detail;
    EXPECT_TRUE(hftrec::capture::captureChannelRuntimeReady(config, CaptureChannel::BookTicker, detail)) << detail;
    EXPECT_TRUE(hftrec::capture::captureChannelRuntimeReady(config, CaptureChannel::Orderbook, detail)) << detail;
}
#endif
