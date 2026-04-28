#include "core/replay/BookState.hpp"

namespace hftrec::replay {
namespace {

void applyLevel(BookState::LevelMap& side, const PricePair& level, bool& dirty) {
    if (level.qtyE8 == 0) {
        side.erase(level.priceE8);
    } else {
        side[level.priceE8] = level.qtyE8;
    }
    dirty = true;
}

}  // namespace

std::size_t BookState::BookSideView::count(std::int64_t priceE8) const noexcept {
    return std::find_if(levels_.begin(), levels_.end(), [priceE8](const auto& level) {
        return level.first == priceE8;
    }) == levels_.end() ? 0u : 1u;
}

const std::int64_t& BookState::BookSideView::at(std::int64_t priceE8) const {
    const auto it = std::find_if(levels_.begin(), levels_.end(), [priceE8](const auto& level) {
        return level.first == priceE8;
    });
    if (it == levels_.end()) throw std::out_of_range{"book price not found"};
    return it->second;
}

void BookState::reset() noexcept {
    bids_.clear();
    asks_.clear();
    bidView_.levels_.clear();
    askView_.levels_.clear();
    bidsDirty_ = false;
    asksDirty_ = false;
    lastTsNs_ = 0;
}

void BookState::applySnapshot(const SnapshotDocument& snap) {
    bids_.clear();
    asks_.clear();
    bidsDirty_ = true;
    asksDirty_ = true;
    for (const auto& level : snap.levels) {
        if (level.qtyE8 <= 0) continue;
        if (level.side == 0) applyLevel(bids_, level, bidsDirty_);
        else applyLevel(asks_, level, asksDirty_);
    }
    lastTsNs_ = snap.tsNs;
}

void BookState::applyDelta(const DepthRow& delta) {
    for (const auto& level : delta.levels) {
        if (level.side == 0) applyLevel(bids_, level, bidsDirty_);
        else applyLevel(asks_, level, asksDirty_);
    }
    lastTsNs_ = delta.tsNs;
}

const BookState::BookSideView& BookState::bids() const {
    ensureBidsView_();
    return bidView_;
}

const BookState::BookSideView& BookState::asks() const {
    ensureAsksView_();
    return askView_;
}

std::int64_t BookState::bestBidPrice() const {
    const auto& view = bids();
    return view.empty() ? 0 : view.begin()->first;
}

std::int64_t BookState::bestAskPrice() const {
    const auto& view = asks();
    return view.empty() ? 0 : view.begin()->first;
}

std::int64_t BookState::bestBidQty() const {
    const auto& view = bids();
    return view.empty() ? 0 : view.begin()->second;
}

std::int64_t BookState::bestAskQty() const {
    const auto& view = asks();
    return view.empty() ? 0 : view.begin()->second;
}

void BookState::ensureBidsView_() const {
    if (!bidsDirty_) return;
    bidView_.levels_.clear();
    bidView_.levels_.reserve(bids_.size());
    for (const auto& [price, qty] : bids_) bidView_.levels_.push_back({price, qty});
    std::sort(bidView_.levels_.begin(), bidView_.levels_.end(), [](const auto& a, const auto& b) {
        return a.first > b.first;
    });
    bidsDirty_ = false;
}

void BookState::ensureAsksView_() const {
    if (!asksDirty_) return;
    askView_.levels_.clear();
    askView_.levels_.reserve(asks_.size());
    for (const auto& [price, qty] : asks_) askView_.levels_.push_back({price, qty});
    std::sort(askView_.levels_.begin(), askView_.levels_.end(), [](const auto& a, const auto& b) {
        return a.first < b.first;
    });
    asksDirty_ = false;
}

}  // namespace hftrec::replay
