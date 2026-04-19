#include <gtest/gtest.h>

#include "core/replay/BookState.hpp"
#include "core/replay/EventRows.hpp"

namespace {

using hftrec::replay::BookState;
using hftrec::replay::DepthRow;
using hftrec::replay::PricePair;
using hftrec::replay::SnapshotDocument;

TEST(BookState, SnapshotDropsZeroQtyLevels) {
    SnapshotDocument snap{};
    snap.tsNs = 100;
    snap.bids = {{30000, 1'000}, {29900, 0}, {29800, 500}};
    snap.asks = {{30100, 2'000}, {30200, 0}};

    BookState b{};
    b.applySnapshot(snap);
    EXPECT_EQ(b.bids().size(), 2u);
    EXPECT_EQ(b.asks().size(), 1u);
    EXPECT_EQ(b.bestBidPrice(), 30000);
    EXPECT_EQ(b.bestAskPrice(), 30100);
    EXPECT_EQ(b.lastTsNs(), 100);
}

TEST(BookState, DeltaOverwritesModifiesAndRemoves) {
    SnapshotDocument snap{};
    snap.tsNs = 100;
    snap.bids = {{30000, 1'000}, {29900, 500}};
    snap.asks = {{30100, 2'000}, {30200, 1'000}};
    BookState b{};
    b.applySnapshot(snap);

    DepthRow d{};
    d.tsNs = 200;
    d.bids = {
        {30000, 1'500},   // modify top bid
        {29800, 300},     // add new bid level
        {29900, 0},       // remove 29900 bid level
    };
    d.asks = {
        {30100, 0},       // remove top ask
        {30300, 700},     // add new ask level
    };
    b.applyDelta(d);

    // Bids now: 30000→1500, 29800→300.  29900 removed.
    ASSERT_EQ(b.bids().size(), 2u);
    EXPECT_EQ(b.bids().at(30000), 1'500);
    EXPECT_EQ(b.bids().at(29800), 300);
    EXPECT_EQ(b.bids().count(29900), 0u);
    EXPECT_EQ(b.bestBidPrice(), 30000);

    // Asks now: 30200→1000, 30300→700.  30100 removed.
    ASSERT_EQ(b.asks().size(), 2u);
    EXPECT_EQ(b.asks().count(30100), 0u);
    EXPECT_EQ(b.asks().at(30200), 1'000);
    EXPECT_EQ(b.asks().at(30300), 700);
    EXPECT_EQ(b.bestAskPrice(), 30200);

    EXPECT_EQ(b.lastTsNs(), 200);
}

TEST(BookState, StickyLevelsPreservedBetweenDeltas) {
    SnapshotDocument snap{};
    snap.bids = {{100, 5}, {99, 5}, {98, 5}};
    snap.asks = {{101, 5}, {102, 5}, {103, 5}};
    BookState b{};
    b.applySnapshot(snap);

    DepthRow d{};
    d.tsNs = 1;
    d.bids = {{100, 7}};   // only touch the top bid
    d.asks = {{103, 0}};   // remove one specific ask
    b.applyDelta(d);

    // Untouched levels still there.
    EXPECT_EQ(b.bids().at(99), 5);
    EXPECT_EQ(b.bids().at(98), 5);
    EXPECT_EQ(b.asks().at(101), 5);
    EXPECT_EQ(b.asks().at(102), 5);
    // Touched levels changed.
    EXPECT_EQ(b.bids().at(100), 7);
    EXPECT_EQ(b.asks().count(103), 0u);
}

TEST(BookState, ResetClears) {
    BookState b{};
    SnapshotDocument snap{};
    snap.bids = {{1, 1}};
    snap.asks = {{2, 1}};
    b.applySnapshot(snap);
    b.reset();
    EXPECT_TRUE(b.bids().empty());
    EXPECT_TRUE(b.asks().empty());
    EXPECT_EQ(b.lastTsNs(), 0);
}

}  // namespace
