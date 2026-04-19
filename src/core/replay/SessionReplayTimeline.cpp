#include "core/replay/SessionReplay.hpp"

#include <algorithm>
#include <string>

#include "core/metrics/Metrics.hpp"

namespace hftrec::replay {

void SessionReplay::finalize() noexcept {
    status_ = Status::Ok;
    errorDetail_.clear();
    gapDetected_ = false;
    sequenceValidationAvailable_ = false;
    rebuildEvents_();
    if (!validateDepthStream_()) {
        status_ = Status::CorruptData;
    }
}

void SessionReplay::rewindToSnapshot_() {
    book_.reset();
    book_.applySnapshot(snapshot_);
    cursor_ = 0;
}

void SessionReplay::applyEvent_(const Event& ev) {
    if (ev.kind == EventKind::Depth && ev.rowIndex < depths_.size()) {
        book_.applyDelta(depths_[ev.rowIndex]);
    }
    // Trades and book-ticker events do not mutate the L2 book.
}

void SessionReplay::seek(std::int64_t targetTsNs) {
    if (!isOk(status_)) return;
    metrics::recordReplaySeek();
    const std::int64_t currentTsNs = cursor_ == 0
        ? (snapshot_.tsNs != 0 ? snapshot_.tsNs : 0)
        : events_[cursor_ - 1].tsNs;
    if (targetTsNs < currentTsNs) {
        rewindToSnapshot_();
    }
    while (cursor_ < events_.size() && events_[cursor_].tsNs <= targetTsNs) {
        applyEvent_(events_[cursor_]);
        ++cursor_;
    }
}

bool SessionReplay::validateDepthStream_() noexcept {
    if (depths_.empty()) {
        return true;
    }

    std::int64_t previousUpdateId = snapshot_.updateId;
    bool havePreviousUpdateId = previousUpdateId > 0;

    for (std::size_t i = 0; i < depths_.size(); ++i) {
        const auto& row = depths_[i];
        const bool hasIds = row.updateId > 0 && row.firstUpdateId > 0;
        if (!hasIds) {
            continue;
        }

        sequenceValidationAvailable_ = true;
        if (row.updateId < row.firstUpdateId) {
            gapDetected_ = true;
            ++integrityFailureCount_;
            errorDetail_ = "depth integrity failure at row " + std::to_string(i)
                + ": updateId < firstUpdateId";
            return false;
        }

        if (!havePreviousUpdateId) {
            previousUpdateId = row.updateId;
            havePreviousUpdateId = true;
            continue;
        }

        const auto expectedNext = previousUpdateId + 1;
        if (row.updateId <= previousUpdateId) {
            gapDetected_ = true;
            ++integrityFailureCount_;
            errorDetail_ = "depth integrity failure at row " + std::to_string(i)
                + ": non-increasing updateId";
            return false;
        }
        if (row.firstUpdateId > expectedNext || row.updateId < expectedNext) {
            gapDetected_ = true;
            ++integrityFailureCount_;
            errorDetail_ = "depth integrity failure at row " + std::to_string(i)
                + ": expected update " + std::to_string(expectedNext)
                + " but saw range [" + std::to_string(row.firstUpdateId)
                + ", " + std::to_string(row.updateId) + "]";
            return false;
        }

        previousUpdateId = row.updateId;
    }

    return true;
}

void SessionReplay::rebuildEvents_() noexcept {
    events_.clear();
    events_.reserve(trades_.size() + bookTickers_.size() + depths_.size());
    for (std::size_t i = 0; i < depths_.size(); ++i) {
        events_.push_back(Event{depths_[i].tsNs, static_cast<std::uint32_t>(i), EventKind::Depth});
    }
    for (std::size_t i = 0; i < trades_.size(); ++i) {
        events_.push_back(Event{trades_[i].tsNs, static_cast<std::uint32_t>(i), EventKind::Trade});
    }
    for (std::size_t i = 0; i < bookTickers_.size(); ++i) {
        events_.push_back(Event{bookTickers_[i].tsNs, static_cast<std::uint32_t>(i), EventKind::BookTicker});
    }
    std::stable_sort(events_.begin(), events_.end(),
                     [](const Event& a, const Event& b) noexcept {
                         return a.tsNs < b.tsNs;
                     });
    cursor_ = 0;
    if (!events_.empty()) {
        firstTsNs_ = events_.front().tsNs;
        lastTsNs_ = events_.back().tsNs;
    } else if (snapshot_.tsNs != 0) {
        firstTsNs_ = snapshot_.tsNs;
        lastTsNs_ = snapshot_.tsNs;
    } else {
        firstTsNs_ = 0;
        lastTsNs_ = 0;
    }
}

}  // namespace hftrec::replay
