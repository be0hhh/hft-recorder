#include "core/storage/EventStorage.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>

namespace hftrec::storage {

namespace {

template <typename Row>
void appendRowsSince(const std::vector<Row>& src, std::size_t offset, std::vector<Row>& out) {
    if (offset >= src.size()) return;
    out.insert(out.end(),
               src.begin() + static_cast<std::ptrdiff_t>(offset),
               src.end());
}

template <typename Row>
bool inRange(const Row& row, std::int64_t fromTsNs, std::int64_t toTsNs) noexcept {
    return row.tsNs >= fromTsNs && row.tsNs <= toTsNs;
}

template <typename Row>
bool eventLess(const Row& lhs, const Row& rhs) noexcept {
    if (lhs.tsNs != rhs.tsNs) return lhs.tsNs < rhs.tsNs;
    if constexpr (requires { lhs.captureSeq; lhs.ingestSeq; }) {
        if (lhs.captureSeq != rhs.captureSeq) return lhs.captureSeq < rhs.captureSeq;
        return lhs.ingestSeq < rhs.ingestSeq;
    } else {
        return false;
    }
}

template <typename Row>
void appendRowsInSortedRange(const std::vector<Row>& src,
                             std::int64_t fromTsNs,
                             std::int64_t toTsNs,
                             std::vector<Row>& out) {
    if (src.empty() || toTsNs < fromTsNs) return;
    const auto begin = std::lower_bound(
        src.begin(),
        src.end(),
        fromTsNs,
        [](const Row& row, std::int64_t ts) noexcept { return row.tsNs < ts; });
    const auto end = std::upper_bound(
        begin,
        src.end(),
        toTsNs,
        [](std::int64_t ts, const Row& row) noexcept { return ts < row.tsNs; });
    out.insert(out.end(), begin, end);
}

template <typename Row>
void appendRowsInAnyOrderRange(const std::vector<Row>& src,
                               std::int64_t fromTsNs,
                               std::int64_t toTsNs,
                               std::vector<Row>& out) {
    if (src.empty() || toTsNs < fromTsNs) return;
    for (const auto& row : src) {
        if (inRange(row, fromTsNs, toTsNs)) out.push_back(row);
    }
    std::stable_sort(out.begin(), out.end(), [](const Row& lhs, const Row& rhs) noexcept { return eventLess(lhs, rhs); });
}

Status mergeStatus(Status current, Status next) noexcept {
    return isOk(current) ? next : current;
}

}  // namespace

EventBatch IEventSource::readSince(std::size_t tradeOffset,
                                   std::size_t liquidationOffset,
                                   std::size_t bookTickerOffset,
                                   std::size_t depthOffset,
                                   std::size_t snapshotOffset,
                                   std::size_t markPriceOffset,
                                   std::size_t indexPriceOffset,
                                   std::size_t fundingOffset,
                                   std::size_t priceLimitOffset) const {
    auto batch = readAll();
    if (tradeOffset >= batch.trades.size()) {
        batch.trades.clear();
    } else {
        batch.trades.erase(batch.trades.begin(),
                           batch.trades.begin() + static_cast<std::ptrdiff_t>(tradeOffset));
    }
    if (liquidationOffset >= batch.liquidations.size()) {
        batch.liquidations.clear();
    } else {
        batch.liquidations.erase(batch.liquidations.begin(),
                                 batch.liquidations.begin() + static_cast<std::ptrdiff_t>(liquidationOffset));
    }
    if (bookTickerOffset >= batch.bookTickers.size()) {
        batch.bookTickers.clear();
    } else {
        batch.bookTickers.erase(batch.bookTickers.begin(),
                                batch.bookTickers.begin() + static_cast<std::ptrdiff_t>(bookTickerOffset));
    }
    if (depthOffset >= batch.depths.size()) {
        batch.depths.clear();
    } else {
        batch.depths.erase(batch.depths.begin(),
                           batch.depths.begin() + static_cast<std::ptrdiff_t>(depthOffset));
    }
    if (snapshotOffset >= batch.snapshots.size()) {
        batch.snapshots.clear();
    } else {
        batch.snapshots.erase(batch.snapshots.begin(),
                              batch.snapshots.begin() + static_cast<std::ptrdiff_t>(snapshotOffset));
    }
    if (markPriceOffset >= batch.markPrices.size()) {
        batch.markPrices.clear();
    } else {
        batch.markPrices.erase(batch.markPrices.begin(),
                               batch.markPrices.begin() + static_cast<std::ptrdiff_t>(markPriceOffset));
    }
    if (indexPriceOffset >= batch.indexPrices.size()) {
        batch.indexPrices.clear();
    } else {
        batch.indexPrices.erase(batch.indexPrices.begin(),
                                batch.indexPrices.begin() + static_cast<std::ptrdiff_t>(indexPriceOffset));
    }
    if (fundingOffset >= batch.fundings.size()) {
        batch.fundings.clear();
    } else {
        batch.fundings.erase(batch.fundings.begin(),
                             batch.fundings.begin() + static_cast<std::ptrdiff_t>(fundingOffset));
    }
    if (priceLimitOffset >= batch.priceLimits.size()) {
        batch.priceLimits.clear();
    } else {
        batch.priceLimits.erase(batch.priceLimits.begin(),
                                batch.priceLimits.begin() + static_cast<std::ptrdiff_t>(priceLimitOffset));
    }
    return batch;
}

bool IEventSource::readSnapshotAtOrBefore(std::int64_t tsNs,
                                          replay::SnapshotDocument& out) const {
    const auto batch = readAll();
    const auto it = std::upper_bound(
        batch.snapshots.begin(),
        batch.snapshots.end(),
        tsNs,
        [](std::int64_t ts, const replay::SnapshotDocument& row) noexcept { return ts < row.tsNs; });
    if (it == batch.snapshots.begin()) return false;
    out = *std::prev(it);
    return true;
}

std::vector<replay::DepthRow> IEventSource::readDepthRange(std::int64_t fromTsNs,
                                                           std::int64_t toTsNs) const {
    return readRange(fromTsNs, toTsNs).depths;
}

Status LiveEventStore::appendTrade(const replay::TradeRow& row) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.trades.push_back(row);
    ++version_;
    return Status::Ok;
}

Status LiveEventStore::appendLiquidation(const replay::LiquidationRow& row) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.liquidations.push_back(row);
    ++version_;
    return Status::Ok;
}

Status LiveEventStore::appendBookTicker(const replay::BookTickerRow& row) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.bookTickers.push_back(row);
    ++version_;
    return Status::Ok;
}

Status LiveEventStore::appendMarkPrice(const replay::MarkPriceRow& row) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.markPrices.push_back(row);
    ++version_;
    return Status::Ok;
}

Status LiveEventStore::appendIndexPrice(const replay::IndexPriceRow& row) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.indexPrices.push_back(row);
    ++version_;
    return Status::Ok;
}

Status LiveEventStore::appendFunding(const replay::FundingRow& row) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.fundings.push_back(row);
    ++version_;
    return Status::Ok;
}

Status LiveEventStore::appendPriceLimit(const replay::PriceLimitRow& row) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.priceLimits.push_back(row);
    ++version_;
    return Status::Ok;
}

Status LiveEventStore::appendDepth(const replay::DepthRow& row) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.depths.push_back(row);
    ++version_;
    return Status::Ok;
}

Status LiveEventStore::appendSnapshot(const replay::SnapshotDocument& snapshot,
                                      std::uint64_t) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    events_.snapshots.push_back(snapshot);
    ++version_;
    return Status::Ok;
}

Status LiveEventStore::flush() noexcept {
    return Status::Ok;
}

Status LiveEventStore::close() noexcept {
    return Status::Ok;
}

EventBatch LiveEventStore::readAll() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return events_;
}

EventBatch LiveEventStore::readRange(std::int64_t fromTsNs, std::int64_t toTsNs) const {
    EventBatch out{};
    std::lock_guard<std::mutex> lock(mutex_);
    appendRowsInAnyOrderRange(events_.trades, fromTsNs, toTsNs, out.trades);
    appendRowsInAnyOrderRange(events_.liquidations, fromTsNs, toTsNs, out.liquidations);
    appendRowsInAnyOrderRange(events_.bookTickers, fromTsNs, toTsNs, out.bookTickers);
    appendRowsInAnyOrderRange(events_.markPrices, fromTsNs, toTsNs, out.markPrices);
    appendRowsInAnyOrderRange(events_.indexPrices, fromTsNs, toTsNs, out.indexPrices);
    appendRowsInAnyOrderRange(events_.fundings, fromTsNs, toTsNs, out.fundings);
    appendRowsInAnyOrderRange(events_.priceLimits, fromTsNs, toTsNs, out.priceLimits);
    appendRowsInAnyOrderRange(events_.depths, fromTsNs, toTsNs, out.depths);
    appendRowsInAnyOrderRange(events_.snapshots, fromTsNs, toTsNs, out.snapshots);
    return out;
}

bool LiveEventStore::readSnapshotAtOrBefore(std::int64_t tsNs,
                                            replay::SnapshotDocument& out) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto it = std::upper_bound(
        events_.snapshots.begin(),
        events_.snapshots.end(),
        tsNs,
        [](std::int64_t ts, const replay::SnapshotDocument& row) noexcept { return ts < row.tsNs; });
    if (it == events_.snapshots.begin()) return false;
    out = *std::prev(it);
    return true;
}

std::vector<replay::DepthRow> LiveEventStore::readDepthRange(std::int64_t fromTsNs,
                                                             std::int64_t toTsNs) const {
    std::vector<replay::DepthRow> out;
    std::lock_guard<std::mutex> lock(mutex_);
    appendRowsInSortedRange(events_.depths, fromTsNs, toTsNs, out);
    return out;
}

EventBatch LiveEventStore::readSince(std::size_t tradeOffset,
                                     std::size_t liquidationOffset,
                                     std::size_t bookTickerOffset,
                                     std::size_t depthOffset,
                                     std::size_t snapshotOffset,
                                     std::size_t markPriceOffset,
                                     std::size_t indexPriceOffset,
                                     std::size_t fundingOffset,
                                     std::size_t priceLimitOffset) const {
    EventBatch out{};
    std::lock_guard<std::mutex> lock(mutex_);
    appendRowsSince(events_.trades, tradeOffset, out.trades);
    appendRowsSince(events_.liquidations, liquidationOffset, out.liquidations);
    appendRowsSince(events_.bookTickers, bookTickerOffset, out.bookTickers);
    appendRowsSince(events_.depths, depthOffset, out.depths);
    appendRowsSince(events_.snapshots, snapshotOffset, out.snapshots);
    appendRowsSince(events_.markPrices, markPriceOffset, out.markPrices);
    appendRowsSince(events_.indexPrices, indexPriceOffset, out.indexPrices);
    appendRowsSince(events_.fundings, fundingOffset, out.fundings);
    appendRowsSince(events_.priceLimits, priceLimitOffset, out.priceLimits);
    return out;
}

EventStoreStats LiveEventStore::stats() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return EventStoreStats{
        static_cast<std::uint64_t>(events_.trades.size()),
        static_cast<std::uint64_t>(events_.liquidations.size()),
        static_cast<std::uint64_t>(events_.bookTickers.size()),
        static_cast<std::uint64_t>(events_.markPrices.size()),
        static_cast<std::uint64_t>(events_.indexPrices.size()),
        static_cast<std::uint64_t>(events_.fundings.size()),
        static_cast<std::uint64_t>(events_.priceLimits.size()),
        static_cast<std::uint64_t>(events_.depths.size()),
        static_cast<std::uint64_t>(events_.snapshots.size()),
        version_,
    };
}

void LiveEventStore::clear() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    events_ = EventBatch{};
    ++version_;
}

void CompositeEventSink::clearSinks() noexcept {
    sinks_.clear();
}

void CompositeEventSink::addSink(IEventSink* sink) noexcept {
    if (sink != nullptr) sinks_.push_back(sink);
}

Status CompositeEventSink::appendTrade(const replay::TradeRow& row) noexcept {
    Status status = Status::Ok;
    for (auto* sink : sinks_) status = mergeStatus(status, sink->appendTrade(row));
    return status;
}

Status CompositeEventSink::appendLiquidation(const replay::LiquidationRow& row) noexcept {
    Status status = Status::Ok;
    for (auto* sink : sinks_) status = mergeStatus(status, sink->appendLiquidation(row));
    return status;
}

Status CompositeEventSink::appendBookTicker(const replay::BookTickerRow& row) noexcept {
    Status status = Status::Ok;
    for (auto* sink : sinks_) status = mergeStatus(status, sink->appendBookTicker(row));
    return status;
}

Status CompositeEventSink::appendMarkPrice(const replay::MarkPriceRow& row) noexcept {
    Status status = Status::Ok;
    for (auto* sink : sinks_) status = mergeStatus(status, sink->appendMarkPrice(row));
    return status;
}

Status CompositeEventSink::appendIndexPrice(const replay::IndexPriceRow& row) noexcept {
    Status status = Status::Ok;
    for (auto* sink : sinks_) status = mergeStatus(status, sink->appendIndexPrice(row));
    return status;
}

Status CompositeEventSink::appendFunding(const replay::FundingRow& row) noexcept {
    Status status = Status::Ok;
    for (auto* sink : sinks_) status = mergeStatus(status, sink->appendFunding(row));
    return status;
}

Status CompositeEventSink::appendPriceLimit(const replay::PriceLimitRow& row) noexcept {
    Status status = Status::Ok;
    for (auto* sink : sinks_) status = mergeStatus(status, sink->appendPriceLimit(row));
    return status;
}

Status CompositeEventSink::appendDepth(const replay::DepthRow& row) noexcept {
    Status status = Status::Ok;
    for (auto* sink : sinks_) status = mergeStatus(status, sink->appendDepth(row));
    return status;
}

Status CompositeEventSink::appendSnapshot(const replay::SnapshotDocument& snapshot,
                                          std::uint64_t snapshotIndex) noexcept {
    Status status = Status::Ok;
    for (auto* sink : sinks_) status = mergeStatus(status, sink->appendSnapshot(snapshot, snapshotIndex));
    return status;
}

Status CompositeEventSink::flush() noexcept {
    Status status = Status::Ok;
    for (auto* sink : sinks_) status = mergeStatus(status, sink->flush());
    return status;
}

Status CompositeEventSink::close() noexcept {
    Status status = Status::Ok;
    for (auto* sink : sinks_) status = mergeStatus(status, sink->close());
    return status;
}

}  // namespace hftrec::storage
