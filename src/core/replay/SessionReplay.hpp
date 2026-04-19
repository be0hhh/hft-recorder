#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "core/common/Status.hpp"
#include "core/replay/BookState.hpp"
#include "core/replay/EventRows.hpp"

namespace hftrec::replay {

// Loads a captured session from disk, provides parsed event rows plus a
// BookState that can be advanced / rewound in time.
class SessionReplay {
  public:
    // Kinds of events kept in the merged timeline.
    enum class EventKind : std::uint8_t {
        Depth      = 0,
        Trade      = 1,
        BookTicker = 2,
    };

    struct Event {
        std::int64_t   tsNs{0};
        std::uint32_t  rowIndex{0};
        EventKind      kind{EventKind::Depth};
    };

    Status open(const std::filesystem::path& sessionDir) noexcept;

    // Individual-file loaders. Useful from the GUI when the user wants to
    // pick trades.jsonl, bookticker.jsonl, depth.jsonl or a snapshot file
    // from unrelated paths. Call reset() first, then any combination of
    // addXxx(), then finalize(). Each addXxx() may be called multiple times
    // to merge files.
    void   reset() noexcept;
    Status addTradesFile(const std::filesystem::path& path) noexcept;
    Status addBookTickerFile(const std::filesystem::path& path) noexcept;
    Status addDepthFile(const std::filesystem::path& path) noexcept;
    Status addSnapshotFile(const std::filesystem::path& path) noexcept;
    void   finalize() noexcept;
    Status status() const noexcept { return status_; }
    std::string_view errorDetail() const noexcept { return errorDetail_; }

    const std::vector<TradeRow>&      trades()      const noexcept { return trades_;      }
    const std::vector<BookTickerRow>& bookTickers() const noexcept { return bookTickers_; }
    const std::vector<DepthRow>&      depths()      const noexcept { return depths_;      }
    const std::vector<Event>&         events()      const noexcept { return events_;      }

    // Session-wide timestamp bounds (after open). 0 if no events.
    std::int64_t firstTsNs() const noexcept { return firstTsNs_; }
    std::int64_t lastTsNs()  const noexcept { return lastTsNs_;  }

    // Current book reflecting all events with ts <= cursor tsNs.
    const BookState& book() const noexcept { return book_; }

    // Advance the book forward so it contains every delta with tsNs <= targetTsNs.
    // If targetTsNs is earlier than the current cursor, the book is rewound to
    // the initial snapshot and then replayed forward to targetTsNs.
    void seek(std::int64_t targetTsNs);

    // How many events have already been applied (index into events()).
    std::size_t cursor() const noexcept { return cursor_; }

  private:
    void rewindToSnapshot_();
    void applyEvent_(const Event& ev);
    bool validateDepthSequence_(std::string& warningDetail) noexcept;
    void rebuildEvents_() noexcept;
    void setError_(Status status, std::string detail) noexcept;

    std::vector<TradeRow>      trades_{};
    std::vector<BookTickerRow> bookTickers_{};
    std::vector<DepthRow>      depths_{};
    std::vector<Event>         events_{};  // merged, sorted by tsNs

    SnapshotDocument snapshot_{};
    BookState        book_{};
    std::size_t      cursor_{0};
    std::int64_t     firstTsNs_{0};
    std::int64_t     lastTsNs_{0};
    Status           status_{Status::Ok};
    std::string      errorDetail_{};
};

}  // namespace hftrec::replay
