#include "core/replay/SessionReplay.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

#include "core/corpus/LoadReport.hpp"
#include "core/metrics/Metrics.hpp"

namespace hftrec::replay {

void SessionReplay::appendTradeRow(TradeRow row) {
    trades_.push_back(std::move(row));
}

void SessionReplay::appendLiquidationRow(LiquidationRow row) {
    liquidations_.push_back(std::move(row));
}

void SessionReplay::appendBookTickerRow(BookTickerRow row) {
    bookTickers_.push_back(std::move(row));
}

void SessionReplay::appendDepthRow(DepthRow row) {
    depths_.push_back(std::move(row));
}

void SessionReplay::appendSnapshotDocument(SnapshotDocument snapshot) {
    snapshot_ = std::move(snapshot);
    snapshotLoaded_ = true;
    book_.reset();
    book_.applySnapshot(snapshot_);
}

void SessionReplay::refreshLiveTimeline() noexcept {
    status_ = Status::Ok;
    errorDetail_.clear();
    gapDetected_ = false;
    sequenceValidationAvailable_ = false;
    integrityFailureCount_ = 0;
    resetIntegrity_();
    rebuildEvents_();
    rebuildBuckets_();
    const bool depthValid = validateDepthStream_();
    const bool sequenceMetadataValid = validateSequenceMetadata_();
    refreshHealthSummary_();
    if (!depthValid || !sequenceMetadataValid || integritySummary_.sessionHealth == SessionHealth::Corrupt) {
        status_ = Status::CorruptData;
    }
}

void SessionReplay::finalize() noexcept {
    status_ = Status::Ok;
    errorDetail_.clear();
    gapDetected_ = false;
    sequenceValidationAvailable_ = false;
    integrityFailureCount_ = 0;
    resetIntegrity_();
    rebuildEvents_();
    rebuildBuckets_();
    const bool depthValid = validateDepthStream_();
    const bool sequenceMetadataValid = validateSequenceMetadata_();
    refreshHealthSummary_();
    if (!depthValid || !sequenceMetadataValid || integritySummary_.sessionHealth == SessionHealth::Corrupt) {
        status_ = Status::CorruptData;
    }
}

void SessionReplay::rewindToSnapshot_() {
    book_.reset();
    if (snapshotLoaded_) {
        book_.applySnapshot(snapshot_);
    }
    cursor_ = 0;
}

void SessionReplay::applyDepthRowsUntil_(std::size_t depthRowExclusive) {
    const auto limit = std::min(depthRowExclusive, depths_.size());
    for (std::size_t i = 0; i < limit; ++i) {
        book_.applyDelta(depths_[i]);
    }
}

void SessionReplay::applyBucket_(const ReplayBucket& bucket) {
    for (const auto& item : bucket.items) {
        if (item.kind == EventKind::Depth && item.rowIndex < depths_.size()) {
            book_.applyDelta(depths_[item.rowIndex]);
        }
    }
}

void SessionReplay::seek(std::int64_t targetTsNs) {
    if (!isOk(status_)) return;
    metrics::recordReplaySeek();
    const std::int64_t currentTsNs = cursor_ == 0
        ? (snapshot_.tsNs != 0 ? snapshot_.tsNs : 0)
        : buckets_[cursor_ - 1].tsNs;
    if (targetTsNs < currentTsNs) {
        rewindToSnapshot_();
    }

    if (cursor_ == 0 && !loadReport_.seekBuckets.empty()) {
        const auto it = std::upper_bound(
            loadReport_.seekBuckets.begin(),
            loadReport_.seekBuckets.end(),
            targetTsNs,
            [](std::int64_t ts, const hftrec::corpus::SeekIndexBucket& bucket) noexcept {
                return ts < bucket.tsNs;
            });
        if (it != loadReport_.seekBuckets.begin()) {
            const auto& bucket = *std::prev(it);
            applyDepthRowsUntil_(static_cast<std::size_t>(bucket.depthRowIndex));
            std::size_t eventBase = 0;
            std::size_t bucketCursor = 0;
            const auto targetEventIndex = static_cast<std::size_t>(bucket.eventIndexStart);
            while (bucketCursor < buckets_.size() && eventBase < targetEventIndex) {
                eventBase += buckets_[bucketCursor].items.size();
                ++bucketCursor;
            }
            cursor_ = std::min(bucketCursor, buckets_.size());
        }
    }

    while (cursor_ < buckets_.size() && buckets_[cursor_].tsNs <= targetTsNs) {
        applyBucket_(buckets_[cursor_]);
        ++cursor_;
    }
}

bool SessionReplay::validateDepthStream_() noexcept {
    return true;
}

bool SessionReplay::validateSequenceMetadata_() noexcept {
    auto validateChannel = [&](IntegrityChannel channel,
                               const auto& rows,
                               std::string_view expectedName) noexcept -> bool {
        if (rows.empty()) return true;

        bool allHaveIngestSeq = true;
        bool allHaveCaptureSeq = true;
        bool anyHaveIngestSeq = false;
        bool anyHaveCaptureSeq = false;
        std::int64_t previousCaptureSeq = 0;
        for (std::size_t i = 0; i < rows.size(); ++i) {
            const bool hasIngestSeq = rows[i].ingestSeq > 0;
            const bool hasCaptureSeq = rows[i].captureSeq > 0;
            allHaveIngestSeq = allHaveIngestSeq && hasIngestSeq;
            allHaveCaptureSeq = allHaveCaptureSeq && hasCaptureSeq;
            anyHaveIngestSeq = anyHaveIngestSeq || hasIngestSeq;
            anyHaveCaptureSeq = anyHaveCaptureSeq || hasCaptureSeq;

            if (hasCaptureSeq) {
                sequenceValidationAvailable_ = true;
                if (previousCaptureSeq != 0 && rows[i].captureSeq <= previousCaptureSeq) {
                    ++integrityFailureCount_;
                    errorDetail_ = std::string{expectedName} + " capture sequence failure at row "
                        + std::to_string(i) + ": non-increasing captureSeq";
                    auto& summary = summaryFor_(channel);
                    summary.state = ChannelHealthState::Corrupt;
                    noteIncident_(IntegrityIncident{
                        channel,
                        IntegrityIncidentKind::DepthNonMonotonic,
                        IntegritySeverity::Error,
                        "non_increasing_capture_seq",
                        errorDetail_,
                        rows[i].tsNs,
                        i,
                        "strictly increasing captureSeq",
                        std::to_string(rows[i].captureSeq),
                        true
                    });
                    return false;
                }
                previousCaptureSeq = rows[i].captureSeq;
            }

            if (!hasIngestSeq) continue;
            if (i != 0 && rows[i - 1].ingestSeq > 0 && rows[i].ingestSeq <= rows[i - 1].ingestSeq) {
                ++integrityFailureCount_;
                errorDetail_ = std::string{expectedName} + " ingest sequence failure at row "
                    + std::to_string(i) + ": non-increasing ingestSeq";
                auto& summary = summaryFor_(channel);
                summary.state = ChannelHealthState::Corrupt;
                noteIncident_(IntegrityIncident{
                    channel,
                    IntegrityIncidentKind::DepthNonMonotonic,
                    IntegritySeverity::Error,
                    "non_increasing_ingest_seq",
                    errorDetail_,
                    rows[i].tsNs,
                    i,
                    "strictly increasing ingestSeq",
                    std::to_string(rows[i].ingestSeq),
                    true
                });
                return false;
            }
        }

        if (anyHaveCaptureSeq && !allHaveCaptureSeq) {
            auto& summary = summaryFor_(channel);
            summary.state = ChannelHealthState::Corrupt;
            ++integrityFailureCount_;
            errorDetail_ = std::string{expectedName} + " capture sequence metadata is missing or inconsistent";
            noteIncident_(IntegrityIncident{
                channel,
                IntegrityIncidentKind::UnknownIntegrity,
                IntegritySeverity::Error,
                "missing_capture_seq",
                errorDetail_,
                rows.front().tsNs,
                0,
                "all rows have positive captureSeq",
                {},
                true
            });
            return false;
        }

        if (anyHaveIngestSeq && !allHaveIngestSeq) {
            auto& summary = summaryFor_(channel);
            summary.state = ChannelHealthState::Corrupt;
            ++integrityFailureCount_;
            errorDetail_ = std::string{expectedName} + " ingest sequence metadata is missing or inconsistent";
            noteIncident_(IntegrityIncident{
                channel,
                IntegrityIncidentKind::UnknownIntegrity,
                IntegritySeverity::Error,
                "inconsistent_ingest_seq",
                errorDetail_,
                rows.front().tsNs,
                0,
                "all rows either have ingestSeq or do not",
                {},
                true
            });
            return false;
        }

        if (anyHaveCaptureSeq != anyHaveIngestSeq) {
            auto& summary = summaryFor_(channel);
            if (summary.state != ChannelHealthState::Corrupt) {
                summary.state = ChannelHealthState::Degraded;
            }
            const auto reasonCode = !anyHaveCaptureSeq
                ? "missing_capture_seq"
                : "missing_ingest_seq";
            const auto detail = !anyHaveCaptureSeq
                ? (std::string{expectedName} + " capture sequence metadata is absent; exact ordering is unprovable")
                : (std::string{expectedName} + " ingest sequence metadata is absent; exact ordering is unprovable");
            noteIncident_(IntegrityIncident{
                channel,
                IntegrityIncidentKind::ExactnessUnprovable,
                IntegritySeverity::Warning,
                reasonCode,
                detail,
                rows.front().tsNs,
                0,
                "positive captureSeq and ingestSeq on every row",
                {},
                true
            });
        }

        return true;
    };

    const bool tradesOk = validateChannel(IntegrityChannel::Trades, trades_, "trades");
    const bool liquidationsOk = validateChannel(IntegrityChannel::Liquidations, liquidations_, "liquidations");
    const bool bookTickerOk = validateChannel(IntegrityChannel::BookTicker, bookTickers_, "bookticker");
    if (!(tradesOk && liquidationsOk && bookTickerOk)) {
        return false;
    }

    std::vector<std::int64_t> mergedIngestSeqs;
    mergedIngestSeqs.reserve(events_.size());
    for (const auto& event : events_) {
        if (event.ingestSeq <= 0) {
            continue;
        }
        mergedIngestSeqs.push_back(event.ingestSeq);
    }

    std::sort(mergedIngestSeqs.begin(), mergedIngestSeqs.end());
    for (std::size_t i = 1; i < mergedIngestSeqs.size(); ++i) {
        if (mergedIngestSeqs[i] <= mergedIngestSeqs[i - 1]) {
            auto& tradesSummary = summaryFor_(IntegrityChannel::Trades);
            auto& liquidationsSummary = summaryFor_(IntegrityChannel::Liquidations);
            auto& bookTickerSummary = summaryFor_(IntegrityChannel::BookTicker);
            auto& depthSummary = summaryFor_(IntegrityChannel::Depth);
            if (tradesSummary.state != ChannelHealthState::Corrupt) tradesSummary.state = ChannelHealthState::Degraded;
            if (liquidationsSummary.state != ChannelHealthState::Corrupt) liquidationsSummary.state = ChannelHealthState::Degraded;
            if (bookTickerSummary.state != ChannelHealthState::Corrupt) bookTickerSummary.state = ChannelHealthState::Degraded;
            if (depthSummary.state != ChannelHealthState::Corrupt) depthSummary.state = ChannelHealthState::Degraded;
            noteIncident_(IntegrityIncident{
                IntegrityChannel::Trades,
                IntegrityIncidentKind::ExactnessUnprovable,
                IntegritySeverity::Warning,
                "duplicate_merged_ingest_seq",
                "merged event ingest sequence contains duplicates across channels; exact ordering is unprovable",
                0,
                i,
                "unique ingestSeq across all channels",
                std::to_string(mergedIngestSeqs[i]),
                true
            });
            return true;
        }
    }

    return true;
}

void SessionReplay::rebuildEvents_() noexcept {
    events_.clear();
    events_.reserve(trades_.size() + liquidations_.size() + bookTickers_.size() + depths_.size());
    for (std::size_t i = 0; i < depths_.size(); ++i) {
        events_.push_back(Event{depths_[i].tsNs, 0, static_cast<std::uint32_t>(i), EventKind::Depth});
    }
    for (std::size_t i = 0; i < trades_.size(); ++i) {
        events_.push_back(Event{trades_[i].tsNs, trades_[i].ingestSeq, static_cast<std::uint32_t>(i), EventKind::Trade});
    }
    for (std::size_t i = 0; i < liquidations_.size(); ++i) {
        events_.push_back(Event{liquidations_[i].tsNs, liquidations_[i].ingestSeq, static_cast<std::uint32_t>(i), EventKind::Liquidation});
    }
    for (std::size_t i = 0; i < bookTickers_.size(); ++i) {
        events_.push_back(Event{bookTickers_[i].tsNs, bookTickers_[i].ingestSeq, static_cast<std::uint32_t>(i), EventKind::BookTicker});
    }
    std::stable_sort(events_.begin(), events_.end(),
                     [](const Event& a, const Event& b) noexcept {
                         if (a.tsNs != b.tsNs) return a.tsNs < b.tsNs;
                         const bool aHasIngestSeq = a.ingestSeq > 0;
                         const bool bHasIngestSeq = b.ingestSeq > 0;
                         if (aHasIngestSeq && bHasIngestSeq && a.ingestSeq != b.ingestSeq) {
                             return a.ingestSeq < b.ingestSeq;
                         }
                         if (a.kind != b.kind) return a.kind < b.kind;
                         return a.rowIndex < b.rowIndex;
                     });
}

void SessionReplay::rebuildBuckets_() noexcept {
    buckets_.clear();
    buckets_.reserve(events_.size());
    for (const auto& ev : events_) {
        if (buckets_.empty() || buckets_.back().tsNs != ev.tsNs) {
            buckets_.push_back(ReplayBucket{ev.tsNs, {}});
        }
        buckets_.back().items.push_back(BucketItem{ev.ingestSeq, ev.rowIndex, ev.kind});
    }

    cursor_ = 0;
    if (!events_.empty()) {
        firstTsNs_ = buckets_.front().tsNs;
        lastTsNs_ = buckets_.back().tsNs;
    } else if (snapshot_.tsNs != 0) {
        firstTsNs_ = snapshot_.tsNs;
        lastTsNs_ = snapshot_.tsNs;
    } else {
        firstTsNs_ = 0;
        lastTsNs_ = 0;
    }
}

}  // namespace hftrec::replay
