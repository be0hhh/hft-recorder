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
    snap.levels = {{30000, 1'000, 0}, {29900, 0, 0}, {29800, 500, 0}, {30100, 2'000, 1}, {30200, 0, 1}};

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
    snap.levels = {{30000, 1'000, 0}, {29900, 500, 0}, {30100, 2'000, 1}, {30200, 1'000, 1}};
    BookState b{};
    b.applySnapshot(snap);

    DepthRow d{};
    d.tsNs = 200;
    d.levels = {
        {30000, 1'500, 0},   // modify top bid
        {29800, 300, 0},     // add new bid level
        {29900, 0, 0},       // remove 29900 bid level
        {30100, 0, 1},       // remove top ask
        {30300, 700, 1},     // add new ask level
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
    snap.levels = {{100, 5, 0}, {99, 5, 0}, {98, 5, 0}, {101, 5, 1}, {102, 5, 1}, {103, 5, 1}};
    BookState b{};
    b.applySnapshot(snap);

    DepthRow d{};
    d.tsNs = 1;
    d.levels = {{100, 7, 0}, {103, 0, 1}};
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
    snap.levels = {{1, 1, 0}, {2, 1, 1}};
    b.applySnapshot(snap);
    b.reset();
    EXPECT_TRUE(b.bids().empty());
    EXPECT_TRUE(b.asks().empty());
    EXPECT_EQ(b.lastTsNs(), 0);
}

}  // namespace
