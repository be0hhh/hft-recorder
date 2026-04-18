#include "core/replay/SessionReplay.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>

#include "core/replay/JsonLineParser.hpp"

namespace hftrec::replay {

namespace {

bool readWholeFile(const std::filesystem::path& path, std::string& out) noexcept {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    out = ss.str();
    return true;
}

template <typename RowT, typename Parser>
Status loadJsonl(const std::filesystem::path& path,
                 std::vector<RowT>& out,
                 Parser&& parse) noexcept {
    std::ifstream in(path, std::ios::binary);
    if (!in) return Status::Ok;  // channel absent → simply no rows
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        if (line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        RowT row{};
        const auto st = parse(std::string_view{line}, row);
        if (!isOk(st)) return st;
        out.push_back(std::move(row));
    }
    return Status::Ok;
}

}  // namespace

void SessionReplay::reset() noexcept {
    trades_.clear();
    bookTickers_.clear();
    depths_.clear();
    events_.clear();
    snapshot_ = SnapshotDocument{};
    book_.reset();
    cursor_ = 0;
    firstTsNs_ = 0;
    lastTsNs_ = 0;
}

Status SessionReplay::addTradesFile(const std::filesystem::path& path) noexcept {
    if (path.empty()) return Status::InvalidArgument;
    return loadJsonl<TradeRow>(path, trades_, parseTradeLine);
}

Status SessionReplay::addBookTickerFile(const std::filesystem::path& path) noexcept {
    if (path.empty()) return Status::InvalidArgument;
    return loadJsonl<BookTickerRow>(path, bookTickers_, parseBookTickerLine);
}

Status SessionReplay::addDepthFile(const std::filesystem::path& path) noexcept {
    if (path.empty()) return Status::InvalidArgument;
    return loadJsonl<DepthRow>(path, depths_, parseDepthLine);
}

Status SessionReplay::addSnapshotFile(const std::filesystem::path& path) noexcept {
    if (path.empty()) return Status::InvalidArgument;
    if (!std::filesystem::exists(path)) return Status::InvalidArgument;
    std::string blob;
    if (!readWholeFile(path, blob)) return Status::IoError;
    SnapshotDocument snap{};
    const auto st = parseSnapshotDocument(blob, snap);
    if (!isOk(st)) return st;
    snapshot_ = snap;
    book_.applySnapshot(snapshot_);
    return Status::Ok;
}

void SessionReplay::finalize() noexcept {
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
        lastTsNs_  = events_.back().tsNs;
    } else if (snapshot_.tsNs != 0) {
        firstTsNs_ = snapshot_.tsNs;
        lastTsNs_  = snapshot_.tsNs;
    } else {
        firstTsNs_ = 0;
        lastTsNs_  = 0;
    }
}

Status SessionReplay::open(const std::filesystem::path& sessionDir) noexcept {
    reset();

    if (!std::filesystem::exists(sessionDir)) return Status::InvalidArgument;

    // Snapshot (optional but usually present). Prefer snapshot_000.json;
    // fall back to any snapshot_*.json file.
    const auto snapPath = sessionDir / "snapshot_000.json";
    if (std::filesystem::exists(snapPath)) {
        if (auto st = addSnapshotFile(snapPath); !isOk(st)) return st;
    } else {
        for (const auto& entry : std::filesystem::directory_iterator(sessionDir)) {
            if (!entry.is_regular_file()) continue;
            const auto name = entry.path().filename().string();
            if (name.rfind("snapshot_", 0) == 0 && entry.path().extension() == ".json") {
                if (auto st = addSnapshotFile(entry.path()); !isOk(st)) return st;
                break;
            }
        }
    }

    if (auto st = addTradesFile(sessionDir / "trades.jsonl");       !isOk(st)) return st;
    if (auto st = addBookTickerFile(sessionDir / "bookticker.jsonl"); !isOk(st)) return st;
    if (auto st = addDepthFile(sessionDir / "depth.jsonl");           !isOk(st)) return st;

    finalize();
    return Status::Ok;
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
    // Gather current event ts to decide whether to rewind.
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

}  // namespace hftrec::replay
