#pragma once

#include <cstdint>
#include <map>

#include "core/replay/EventRows.hpp"

namespace hftrec::replay {

// Full L2 orderbook state, reconstructed event by event.
// Bids are sorted descending (highest price first) via std::greater.
// Asks are sorted ascending (lowest price first).
// Delta semantics (match Binance depth): amount == 0 removes the level;
// non-zero amount overwrites it. Levels not mentioned by a delta keep their
// previous state — this is the "sticky" book the user asked for.
class BookState {
  public:
    using BidMap = std::map<std::int64_t, std::int64_t, std::greater<std::int64_t>>;
    using AskMap = std::map<std::int64_t, std::int64_t>;

    void reset() noexcept;
    void applySnapshot(const SnapshotDocument& snap);
    void applyDelta(const DepthRow& delta);

    const BidMap& bids() const noexcept { return bids_; }
    const AskMap& asks() const noexcept { return asks_; }

    std::int64_t bestBidPrice() const noexcept { return bids_.empty() ? 0 : bids_.begin()->first;  }
    std::int64_t bestAskPrice() const noexcept { return asks_.empty() ? 0 : asks_.begin()->first;  }
    std::int64_t bestBidQty()   const noexcept { return bids_.empty() ? 0 : bids_.begin()->second; }
    std::int64_t bestAskQty()   const noexcept { return asks_.empty() ? 0 : asks_.begin()->second; }

    std::int64_t lastUpdateId() const noexcept { return lastUpdateId_; }
    std::int64_t lastTsNs()     const noexcept { return lastTsNs_;     }

  private:
    BidMap       bids_{};
    AskMap       asks_{};
    std::int64_t lastUpdateId_{0};
    std::int64_t lastTsNs_{0};
};

}  // namespace hftrec::replay
