#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <utility>
#include <vector>

#include <absl/container/flat_hash_map.h>

#include "core/replay/EventRows.hpp"

namespace hftrec::replay {

// Full L2 orderbook state, reconstructed event by event.
// Updates are stored in absl::flat_hash_map for fast price lookup. Public
// bid/ask views are sorted lazily: bids high -> low, asks low -> high.
// Delta semantics (match Binance depth): amount == 0 removes the level;
// non-zero amount overwrites it. Levels not mentioned by a delta keep their
// previous state — this is the "sticky" book the user asked for.
class BookState {
  public:
    using LevelMap = absl::flat_hash_map<std::int64_t, std::int64_t>;

    class BookSideView {
      public:
        using value_type = std::pair<std::int64_t, std::int64_t>;
        using const_iterator = std::vector<value_type>::const_iterator;

        bool empty() const noexcept { return levels_.empty(); }
        std::size_t size() const noexcept { return levels_.size(); }
        const_iterator begin() const noexcept { return levels_.begin(); }
        const_iterator end() const noexcept { return levels_.end(); }

        std::size_t count(std::int64_t priceE8) const noexcept;
        const std::int64_t& at(std::int64_t priceE8) const;

      private:
        friend class BookState;
        std::vector<value_type> levels_{};
    };

    void reset() noexcept;
    void applySnapshot(const SnapshotDocument& snap);
    void applyDelta(const DepthRow& delta);

    const BookSideView& bids() const;
    const BookSideView& asks() const;

    std::int64_t bestBidPrice() const;
    std::int64_t bestAskPrice() const;
    std::int64_t bestBidQty() const;
    std::int64_t bestAskQty() const;

    std::int64_t lastTsNs()     const noexcept { return lastTsNs_;     }

  private:
    void ensureBidsView_() const;
    void ensureAsksView_() const;

    LevelMap bids_{};
    LevelMap asks_{};
    mutable BookSideView bidView_{};
    mutable BookSideView askView_{};
    mutable bool bidsDirty_{true};
    mutable bool asksDirty_{true};
    std::int64_t lastTsNs_{0};
};

}  // namespace hftrec::replay
