#include "core/replay/SessionReplay.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "core/corpus/LoadReport.hpp"
#include "core/metrics/Metrics.hpp"

namespace hftrec::replay {

void SessionReplay::appendTradeRow(TradeRow row) {
    trades_.push_back(std::move(row));
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
    if (depths_.empty()) {
        return true;
    }

    auto& depthSummary = summaryFor_(IntegrityChannel::Depth);
    std::int64_t previousUpdateId = snapshot_.updateId;
    bool havePreviousUpdateId = snapshotLoaded_ && previousUpdateId > 0;

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
            depthSummary.state = ChannelHealthState::Corrupt;
            errorDetail_ = "depth integrity failure at row " + std::to_string(i)
                + ": updateId < firstUpdateId";
            noteIncident_(IntegrityIncident{
                IntegrityChannel::Depth,
                IntegrityIncidentKind::DepthNonMonotonic,
                IntegritySeverity::Error,
                "depth_non_monotonic",
                errorDetail_,
                row.tsNs,
                i,
                "updateId >= firstUpdateId",
                "updateId=" + std::to_string(row.updateId) + ", firstUpdateId=" + std::to_string(row.firstUpdateId),
                true
            });
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
            depthSummary.state = ChannelHealthState::Corrupt;
            errorDetail_ = "depth integrity failure at row " + std::to_string(i)
                + ": non-increasing updateId";
            noteIncident_(IntegrityIncident{
                IntegrityChannel::Depth,
                IntegrityIncidentKind::DepthNonMonotonic,
                IntegritySeverity::Error,
                "depth_non_monotonic",
                errorDetail_,
                row.tsNs,
                i,
                "updateId > previousUpdateId",
                "previous=" + std::to_string(previousUpdateId) + ", updateId=" + std::to_string(row.updateId),
                true
            });
            return false;
        }
        if (row.firstUpdateId > expectedNext || row.updateId < expectedNext) {
            gapDetected_ = true;
            ++integrityFailureCount_;
            depthSummary.state = ChannelHealthState::Corrupt;
            errorDetail_ = "depth integrity failure at row " + std::to_string(i)
                + ": expected update " + std::to_string(expectedNext)
                + " but saw range [" + std::to_string(row.firstUpdateId)
                + ", " + std::to_string(row.updateId) + "]";
            noteIncident_(IntegrityIncident{
                IntegrityChannel::Depth,
                IntegrityIncidentKind::DepthSequenceGap,
                IntegritySeverity::Error,
                "depth_sequence_gap",
                errorDetail_,
                row.tsNs,
                i,
                std::to_string(expectedNext),
                "[" + std::to_string(row.firstUpdateId) + "," + std::to_string(row.updateId) + "]",
                true
            });
            hftrec::corpus::appendLoadIssue(loadReport_, hftrec::corpus::LoadIssue{
                hftrec::corpus::LoadIssueCode::DepthGapDetected,
                hftrec::corpus::LoadIssueSeverity::Fatal,
                Status::CorruptData,
                "depth",
                "depth.jsonl",
                i + 1u,
                errorDetail_,
            });
            return false;
        }

        previousUpdateId = row.updateId;
    }

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

        if (!anyHaveCaptureSeq || !anyHaveIngestSeq) {
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

    if (snapshotLoaded_) {
        if (snapshot_.captureSeq <= 0 || snapshot_.ingestSeq <= 0) {
            auto& summary = summaryFor_(IntegrityChannel::Snapshot);
            summary.state = ChannelHealthState::Degraded;
            noteIncident_(IntegrityIncident{
                IntegrityChannel::Snapshot,
                IntegrityIncidentKind::ExactnessUnprovable,
                IntegritySeverity::Warning,
                "missing_snapshot_sequence",
                "snapshot sequence metadata is missing or non-positive; exact ordering is unprovable",
                snapshot_.tsNs,
                0,
                "positive captureSeq and ingestSeq",
                {},
                true
            });
        } else {
            sequenceValidationAvailable_ = true;
        }
    }

    const bool tradesOk = validateChannel(IntegrityChannel::Trades, trades_, "trades");
    const bool bookTickerOk = validateChannel(IntegrityChannel::BookTicker, bookTickers_, "bookticker");
    const bool depthOk = validateChannel(IntegrityChannel::Depth, depths_, "depth");
    if (!(tradesOk && bookTickerOk && depthOk)) {
        return false;
    }

    // EN: Snapshot is a replay anchor, not necessarily the first persisted
    // event in ingest order. Capture may write snapshot metadata after earlier
    // trades/bookticker rows already exist, so seeding merged ingest-sequence
    // validation from snapshot_.ingestSeq produces false "non-increasing"
    // failures for otherwise valid sessions.
    // RU: Snapshot — это anchor для replay, а не обязательно первое событие по
    // ingestSeq. Он может быть записан позже уже сохранённых trades/bookticker,
    // поэтому начинать merged-проверку с snapshot_.ingestSeq нельзя: это даёт
    // ложные ошибки non-increasing на валидных сессиях.
    std::int64_t previousIngestSeq = 0;
    for (std::size_t i = 0; i < events_.size(); ++i) {
        if (events_[i].ingestSeq <= 0) {
            return true;
        }
        if (previousIngestSeq <= 0) {
            previousIngestSeq = events_[i].ingestSeq;
            continue;
        }
        if (previousIngestSeq != 0 && events_[i].ingestSeq <= previousIngestSeq) {
            auto& tradesSummary = summaryFor_(IntegrityChannel::Trades);
            auto& bookTickerSummary = summaryFor_(IntegrityChannel::BookTicker);
            auto& depthSummary = summaryFor_(IntegrityChannel::Depth);
            if (tradesSummary.state != ChannelHealthState::Corrupt) tradesSummary.state = ChannelHealthState::Degraded;
            if (bookTickerSummary.state != ChannelHealthState::Corrupt) bookTickerSummary.state = ChannelHealthState::Degraded;
            if (depthSummary.state != ChannelHealthState::Corrupt) depthSummary.state = ChannelHealthState::Degraded;
            noteIncident_(IntegrityIncident{
                IntegrityChannel::Trades,
                IntegrityIncidentKind::ExactnessUnprovable,
                IntegritySeverity::Warning,
                "non_monotonic_merged_ingest_seq",
                "merged event ingest sequence is non-monotonic across channels; replay order falls back to timestamp ordering",
                events_[i].tsNs,
                i,
                "strictly increasing ingestSeq across all channels",
                std::to_string(events_[i].ingestSeq),
                true
            });
            return true;
        }
        previousIngestSeq = events_[i].ingestSeq;
    }

    return true;
}

void SessionReplay::rebuildEvents_() noexcept {
    events_.clear();
    events_.reserve(trades_.size() + bookTickers_.size() + depths_.size());
    for (std::size_t i = 0; i < depths_.size(); ++i) {
        events_.push_back(Event{depths_[i].tsNs, depths_[i].ingestSeq, static_cast<std::uint32_t>(i), EventKind::Depth});
    }
    for (std::size_t i = 0; i < trades_.size(); ++i) {
        events_.push_back(Event{trades_[i].tsNs, trades_[i].ingestSeq, static_cast<std::uint32_t>(i), EventKind::Trade});
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
