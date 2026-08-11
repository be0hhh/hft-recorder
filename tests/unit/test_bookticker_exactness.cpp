#include <gtest/gtest.h>

#include "CapturedArrivalTestData.hpp"
#include "core/capture/JsonSerializers.hpp"
#include "core/lab/BookFrameSampler.hpp"

namespace {

using hftrec::Status;
using hftrec::lab::BookFrame;
using hftrec::lab::sampleGroundTruthBookFrames;

TEST(BookTickerExactness, GroundTruthSamplerDoesNotCarryTickerAcrossDepthEvents) {
    hftrec::corpus::SessionCorpus corpus{};
    hftrec::replay::BookTickerRow ticker{};
    ticker.eventId = 1u;
    ticker.bidPriceE8 = 30000;
    ticker.bidQtyE8 = 7;
    ticker.askPriceE8 = 30100;
    ticker.askQtyE8 = 8;
    ticker.tsNs = 2000;
    ticker.captureSeq = 1;
    ticker.ingestSeq = 1;
    ticker.arrival = hftrec::test_support::applicationArrival(1, ticker.tsNs);
    corpus.bookTickerLines.push_back(
        hftrec::capture::renderBookTickerJsonLine(ticker));

    hftrec::replay::DepthRow bid{};
    bid.eventId = 2u;
    bid.tsNs = 3000;
    bid.captureSeq = 2;
    bid.ingestSeq = 2;
    bid.arrival = hftrec::test_support::applicationArrival(2, bid.tsNs);
    bid.levels.push_back({30000, 9, 0});
    corpus.depthRows.push_back(bid);

    hftrec::replay::DepthRow ask{};
    ask.eventId = 3u;
    ask.tsNs = 4000;
    ask.captureSeq = 3;
    ask.ingestSeq = 3;
    ask.arrival = hftrec::test_support::applicationArrival(3, ask.tsNs);
    ask.levels.push_back({30100, 10, 1});
    corpus.depthRows.push_back(ask);

    std::vector<BookFrame> frames;
    ASSERT_EQ(sampleGroundTruthBookFrames(corpus, 8, frames), Status::Ok);
    ASSERT_EQ(frames.size(), 3u);

    EXPECT_TRUE(frames[0].hasBookTicker);
    EXPECT_EQ(frames[0].tsNs, 2000);
    EXPECT_EQ(frames[0].tickerBidPriceE8, 30000);
    EXPECT_EQ(frames[0].tickerAskPriceE8, 30100);

    EXPECT_FALSE(frames[1].hasBookTicker);
    EXPECT_EQ(frames[1].tsNs, 3000);

    EXPECT_FALSE(frames[2].hasBookTicker);
    EXPECT_EQ(frames[2].tsNs, 4000);
}

}  // namespace
