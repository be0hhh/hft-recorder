#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "core/common/Integrity.hpp"
#include "core/common/Status.hpp"
#include "core/corpus/LoadReport.hpp"
#include "core/replay/BookState.hpp"
#include "core/replay/EventRows.hpp"

namespace hftrec::replay {

// Loads a captured session from disk, provides parsed event rows plus a
// BookState that can be advanced / rewound in time.
class SessionReplay {
  public:
    // Kinds of rows kept in the merged timeline.
    enum class EventKind : std::uint8_t {
        Depth      = 0,
        Trade      = 1,
        Liquidation = 2,
        BookTicker = 3,
    };

    struct Event {
        std::int64_t   tsNs{0};
        std::int64_t   ingestSeq{0};
        std::uint32_t  rowIndex{0};
        EventKind      kind{EventKind::Depth};
    };

    struct BucketItem {
        std::int64_t   ingestSeq{0};
        std::uint32_t  rowIndex{0};
        EventKind      kind{EventKind::Depth};
    };

    struct ReplayBucket {
        std::int64_t             tsNs{0};
        std::vector<BucketItem>  items{};
    };

    Status open(const std::filesystem::path& sessionDir) noexcept;

    // Individual-file loaders. Useful from the GUI when the user wants to
    // pick trades.jsonl, bookticker.jsonl, depth.jsonl or a snapshot file
    // from unrelated paths. Call reset() first, then any combination of
    // addXxx(), then finalize(). Each addXxx() may be called multiple times
    // to merge files.
    void   reset() noexcept;
    Status addTradesFile(const std::filesystem::path& path) noexcept;
    Status addLiquidationsFile(const std::filesystem::path& path) noexcept;
    Status addBookTickerFile(const std::filesystem::path& path) noexcept;
    Status addDepthFile(const std::filesystem::path& path) noexcept;
    Status addSnapshotFile(const std::filesystem::path& path) noexcept;
    void   finalize() noexcept;
    Status status() const noexcept { return status_; }
    std::string_view errorDetail() const noexcept { return errorDetail_; }
    bool gapDetected() const noexcept { return gapDetected_; }
    bool sequenceValidationAvailable() const noexcept { return sequenceValidationAvailable_; }
    std::size_t parseFailureCount() const noexcept { return parseFailureCount_; }
    std::size_t integrityFailureCount() const noexcept { return integrityFailureCount_; }
    const SessionIntegritySummary& integritySummary() const noexcept { return integritySummary_; }
    bool exactReplayEligible() const noexcept { return integritySummary_.exactReplayEligible; }
    const hftrec::corpus::LoadReport& loadReport() const noexcept { return loadReport_; }

    const std::vector<TradeRow>&      trades()      const noexcept { return trades_;      }
    const std::vector<LiquidationRow>& liquidations() const noexcept { return liquidations_; }
    const std::vector<BookTickerRow>& bookTickers() const noexcept { return bookTickers_; }
    const std::vector<DepthRow>&      depths()      const noexcept { return depths_;      }
    const std::vector<Event>&         events()      const noexcept { return events_;      }
    const std::vector<ReplayBucket>&  buckets()     const noexcept { return buckets_;     }

    void appendTradeRow(TradeRow row);
    void appendLiquidationRow(LiquidationRow row);
    void appendBookTickerRow(BookTickerRow row);
    void appendDepthRow(DepthRow row);
    void appendSnapshotDocument(SnapshotDocument snapshot);
    void refreshLiveTimeline() noexcept;

    // Session-wide timestamp bounds (after open). 0 if no events.
    std::int64_t firstTsNs() const noexcept { return firstTsNs_; }
    std::int64_t lastTsNs()  const noexcept { return lastTsNs_;  }

    // Current book reflecting all events with ts <= cursor tsNs.
    const BookState& book() const noexcept { return book_; }

    // Advance the book forward so it contains every delta with tsNs <= targetTsNs.
    // If targetTsNs is earlier than the current cursor, the book is rewound to
    // the initial snapshot and then replayed forward to targetTsNs.
    void seek(std::int64_t targetTsNs);

    // How many replay buckets have already been applied.
    std::size_t cursor() const noexcept { return cursor_; }

  private:
    struct ManifestHints {
        bool present{false};
        bool tradesEnabled{true};
        bool liquidationsEnabled{true};
        bool bookTickerEnabled{true};
        bool orderbookEnabled{true};
    };

    void rewindToSnapshot_();
    void applyBucket_(const ReplayBucket& bucket);
    void applyDepthRowsUntil_(std::size_t depthRowExclusive);
    bool validateDepthStream_() noexcept;
    bool validateSequenceMetadata_() noexcept;
    void rebuildEvents_() noexcept;
    void rebuildBuckets_() noexcept;
    void resetIntegrity_() noexcept;
    void finalizeChannelStates_() noexcept;
    void refreshHealthSummary_() noexcept;
    void noteIncident_(const IntegrityIncident& incident) noexcept;
    ChannelIntegritySummary& summaryFor_(IntegrityChannel channel) noexcept;
    bool loadManifestHints_(const std::filesystem::path& sessionDir) noexcept;
    void maybeWriteIntegrityReport_() noexcept;

    std::vector<TradeRow>      trades_{};
    std::vector<LiquidationRow> liquidations_{};
    std::vector<BookTickerRow> bookTickers_{};
    std::vector<DepthRow>      depths_{};
    std::vector<Event>         events_{};   // transitional flat timeline
    std::vector<ReplayBucket>  buckets_{};  // canonical bucketed replay timeline

    SnapshotDocument snapshot_{};
    bool             snapshotLoaded_{false};
    BookState        book_{};
    std::size_t      cursor_{0};
    std::int64_t     firstTsNs_{0};
    std::int64_t     lastTsNs_{0};
    Status           status_{Status::Ok};
    std::string      errorDetail_{};
    bool             gapDetected_{false};
    bool             sequenceValidationAvailable_{false};
    std::size_t      parseFailureCount_{0};
    std::size_t      integrityFailureCount_{0};
    SessionIntegritySummary integritySummary_{};
    ManifestHints           manifestHints_{};
    std::filesystem::path   sessionDir_{};
    hftrec::corpus::LoadReport loadReport_{};
};

}  // namespace hftrec::replay
