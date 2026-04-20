#include <gtest/gtest.h>

#include "core/lab/BookFrameSampler.hpp"

namespace {

using hftrec::Status;
using hftrec::lab::BookFrame;
using hftrec::lab::sampleGroundTruthBookFrames;

TEST(BookTickerExactness, GroundTruthSamplerDoesNotCarryTickerAcrossDepthEvents) {
    hftrec::corpus::SessionCorpus corpus{};
    corpus.bookTickerLines.push_back(
        "{\"tsNs\":2000,\"captureSeq\":1,\"ingestSeq\":1,"
        "\"bidPriceE8\":30000,\"bidQtyE8\":7,\"askPriceE8\":30100,\"askQtyE8\":8}");
    corpus.depthLines.push_back(
        "{\"tsNs\":3000,\"captureSeq\":2,\"ingestSeq\":2,\"updateId\":11,\"firstUpdateId\":11,"
        "\"bids\":[{\"price_i64\":30000,\"qty_i64\":9}],\"asks\":[]}");
    corpus.depthLines.push_back(
        "{\"tsNs\":4000,\"captureSeq\":3,\"ingestSeq\":3,\"updateId\":12,\"firstUpdateId\":12,"
        "\"bids\":[],\"asks\":[{\"price_i64\":30100,\"qty_i64\":10}]}");

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
