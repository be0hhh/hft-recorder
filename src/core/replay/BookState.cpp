#include "core/replay/BookState.hpp"

namespace hftrec::replay {

void BookState::reset() noexcept {
    bids_.clear();
    asks_.clear();
    lastUpdateId_ = 0;
    lastTsNs_ = 0;
}

void BookState::applySnapshot(const SnapshotDocument& snap) {
    bids_.clear();
    asks_.clear();
    for (const auto& level : snap.bids) {
        if (level.qtyE8 > 0) bids_.emplace(level.priceE8, level.qtyE8);
    }
    for (const auto& level : snap.asks) {
        if (level.qtyE8 > 0) asks_.emplace(level.priceE8, level.qtyE8);
    }
    lastTsNs_ = snap.tsNs;
    lastUpdateId_ = 0;
}

void BookState::applyDelta(const DepthRow& delta) {
    for (const auto& level : delta.bids) {
        if (level.qtyE8 == 0) {
            bids_.erase(level.priceE8);
        } else {
            bids_[level.priceE8] = level.qtyE8;
        }
    }
    for (const auto& level : delta.asks) {
        if (level.qtyE8 == 0) {
            asks_.erase(level.priceE8);
        } else {
            asks_[level.priceE8] = level.qtyE8;
        }
    }
    lastTsNs_ = delta.tsNs;
    lastUpdateId_ = delta.finalUpdateId;
}

}  // namespace hftrec::replay
