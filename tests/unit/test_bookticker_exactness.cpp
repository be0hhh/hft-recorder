#include <gtest/gtest.h>

#include "core/lab/BookFrameSampler.hpp"

namespace {

using hftrec::Status;
using hftrec::lab::BookFrame;
using hftrec::lab::sampleGroundTruthBookFrames;

TEST(BookTickerExactness, GroundTruthSamplerDoesNotCarryTickerAcrossDepthEvents) {
    hftrec::corpus::SessionCorpus corpus{};
    corpus.bookTickerLines.push_back("[30000,7,30100,8,2000,\"BTCUSDT\",\"binance\",\"futures_usd\",1,1]");
    corpus.depthLines.push_back("[[[30000,9,0,0]],[],3000,\"BTCUSDT\",\"binance\",\"futures_usd\",11,11,2,2]");
    corpus.depthLines.push_back("[[],[[30100,10,1,0]],4000,\"BTCUSDT\",\"binance\",\"futures_usd\",12,12,3,3]");

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
