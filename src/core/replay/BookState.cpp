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

BookState::FilteredLevels filteredLevels(const BookState::LevelMap& side,
                                         std::int64_t minPriceE8,
                                         std::int64_t maxPriceE8,
                                         std::size_t maxCandidates,
                                         bool descending) {
    BookState::FilteredLevels out;
    out.reserve(maxCandidates > 0 ? std::min(maxCandidates, side.size()) : side.size());
    for (const auto& [price, qty] : side) {
        if (qty <= 0) continue;
        if (minPriceE8 > 0 && price < minPriceE8) continue;
        if (maxPriceE8 > 0 && price > maxPriceE8) continue;
        out.push_back({price, qty});
    }

    const auto less = [descending](const auto& a, const auto& b) noexcept {
        return descending ? a.first > b.first : a.first < b.first;
    };
    if (maxCandidates > 0 && out.size() > maxCandidates) {
        std::partial_sort(out.begin(), out.begin() + static_cast<std::ptrdiff_t>(maxCandidates), out.end(), less);
        out.resize(maxCandidates);
    } else {
        std::sort(out.begin(), out.end(), less);
    }
    return out;
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

BookState::FilteredLevels BookState::filteredBids(std::int64_t minPriceE8,
                                                  std::int64_t maxPriceE8,
                                                  std::size_t maxCandidates) const {
    return filteredLevels(bids_, minPriceE8, maxPriceE8, maxCandidates, true);
}

BookState::FilteredLevels BookState::filteredAsks(std::int64_t minPriceE8,
                                                  std::int64_t maxPriceE8,
                                                  std::size_t maxCandidates) const {
    return filteredLevels(asks_, minPriceE8, maxPriceE8, maxCandidates, false);
}

std::int64_t BookState::bestBidPrice() const {
    std::int64_t best = 0;
    for (const auto& [price, qty] : bids_) {
        if (qty > 0 && price > best) best = price;
    }
    return best;
}

std::int64_t BookState::bestAskPrice() const {
    std::int64_t best = 0;
    for (const auto& [price, qty] : asks_) {
        if (qty <= 0) continue;
        if (best == 0 || price < best) best = price;
    }
    return best;
}

std::int64_t BookState::bestBidQty() const {
    std::int64_t bestPrice = 0;
    std::int64_t bestQty = 0;
    for (const auto& [price, qty] : bids_) {
        if (qty > 0 && price > bestPrice) {
            bestPrice = price;
            bestQty = qty;
        }
    }
    return bestQty;
}

std::int64_t BookState::bestAskQty() const {
    std::int64_t bestPrice = 0;
    std::int64_t bestQty = 0;
    for (const auto& [price, qty] : asks_) {
        if (qty <= 0) continue;
        if (bestPrice == 0 || price < bestPrice) {
            bestPrice = price;
            bestQty = qty;
        }
    }
    return bestQty;
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
