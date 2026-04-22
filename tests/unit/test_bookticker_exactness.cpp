#include <gtest/gtest.h>

#include "core/lab/BookFrameSampler.hpp"

namespace {

using hftrec::Status;
using hftrec::lab::BookFrame;
using hftrec::lab::sampleGroundTruthBookFrames;

TEST(BookTickerExactness, GroundTruthSamplerDoesNotCarryTickerAcrossDepthEvents) {
    hftrec::corpus::SessionCorpus corpus{};
    corpus.bookTickerLines.push_back("[7,30000,8,30100,2000,0,1,1]");
    corpus.depthLines.push_back("[11,11,3000,1,0,2,2,[[9,30000,0,0]],[],0]");
    corpus.depthLines.push_back("[12,12,4000,0,1,3,3,[],[[10,30100,1,0]],0]");

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
