#include "gui/viewer/LiveDataProvider.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <chrono>

#include "core/metrics/Metrics.hpp"
#include "core/replay/JsonLineParser.hpp"
#include "core/storage/EventStorage.hpp"

namespace hftrec::gui::viewer {

namespace {

bool hasRows(const LiveDataBatch& batch) noexcept {
    return !batch.trades.empty() || !batch.bookTickers.empty() || !batch.depths.empty() || !batch.snapshots.empty();
}

template <typename Row>
void appendSortedRange(const std::vector<Row>& rows,
                       std::int64_t tsMin,
                       std::int64_t tsMax,
                       std::vector<Row>& out) {
    if (rows.empty() || tsMax < tsMin) return;
    const auto begin = std::lower_bound(
        rows.begin(),
        rows.end(),
        tsMin,
        [](const Row& row, std::int64_t ts) noexcept { return row.tsNs < ts; });
    const auto end = std::upper_bound(
        begin,
        rows.end(),
        tsMax,
        [](std::int64_t ts, const Row& row) noexcept { return ts < row.tsNs; });
    out.insert(out.end(), begin, end);
}


template <typename ConsumeLine>
void tailRows(JsonTailLiveDataProvider::TailFile& file,
              ConsumeLine&& consumeLine,
              std::string_view label,
              LiveDataPollResult& result) {
    std::error_code fileEc;
    if (file.path.empty() || !std::filesystem::exists(file.path, fileEc) || fileEc) return;

    const auto fileSize = std::filesystem::file_size(file.path, fileEc);
    if (fileEc) return;
    if (fileSize < file.offset) {
        result.reloadRequired = true;
        return;
    }
    if (fileSize == file.offset) return;

    std::ifstream in(file.path, std::ios::binary);
    if (!in) {
        result.failureStatus = Status::IoError;
        result.failureDetail = "live " + std::string{label} + " read failed";
        return;
    }

    in.seekg(static_cast<std::streamoff>(file.offset), std::ios::beg);
    std::string chunk(static_cast<std::size_t>(fileSize - file.offset), '\0');
    in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
    const auto bytesRead = static_cast<std::size_t>(in.gcount());
    chunk.resize(bytesRead);
    if (bytesRead == 0u) return;

    const std::uintmax_t nextOffset = file.offset + bytesRead;
    std::string nextPending = file.pending;
    nextPending += chunk;

    std::size_t lineStart = 0;
    while (true) {
        const auto lineEnd = nextPending.find('\n', lineStart);
        if (lineEnd == std::string::npos) break;

        std::string line = nextPending.substr(lineStart, lineEnd - lineStart);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.empty()) {
            const auto parseStart = std::chrono::steady_clock::now();
            const auto st = consumeLine(std::string_view{line});
            const auto parseNs = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - parseStart).count());
            hftrec::metrics::recordLiveJsonTailParse(parseNs);
            if (!isOk(st)) {
                result.reloadRequired = true;
                result.failureStatus = st;
                result.failureDetail = "live " + std::string{label} + " parse failed, scheduling reload";
                return;
            }
            result.appendedRows = true;
        }
        lineStart = lineEnd + 1;
    }

    nextPending.erase(0, lineStart);
    file.offset = nextOffset;
    file.pending = std::move(nextPending);
}

}  // namespace

void JsonTailLiveDataProvider::start(const LiveDataProviderConfig& config) {
    sessionDir_ = config.sessionDir;
    tradesHistory_.clear();
    bookTickerHistory_.clear();
    depthHistory_.clear();
    ++version_;
    trades_ = TailFile{sessionDir_ / "trades.jsonl", 0, {}};
    bookTicker_ = TailFile{sessionDir_ / "bookticker.jsonl", 0, {}};
    depth_ = TailFile{sessionDir_ / "depth.jsonl", 0, {}};
    snapshotPath_ = findLatestSnapshotPath_();
    snapshot_ = hftrec::replay::SnapshotDocument{};
    snapshotLoaded_ = false;
    if (!snapshotPath_.empty()) {
        std::ifstream snapshotIn(snapshotPath_, std::ios::binary);
        if (snapshotIn) {
            std::string blob{std::istreambuf_iterator<char>{snapshotIn}, std::istreambuf_iterator<char>{}};
            snapshotLoaded_ = isOk(hftrec::replay::parseSnapshotDocument(std::string_view{blob}, snapshot_));
        }
    }
    syncTailOffset_(trades_);
    syncTailOffset_(bookTicker_);
    syncTailOffset_(depth_);
}

void JsonTailLiveDataProvider::stop() noexcept {
    sessionDir_.clear();
    trades_ = TailFile{};
    bookTicker_ = TailFile{};
    depth_ = TailFile{};
    snapshotPath_.clear();
    snapshotLoaded_ = false;
    snapshot_ = hftrec::replay::SnapshotDocument{};
    tradesHistory_.clear();
    bookTickerHistory_.clear();
    depthHistory_.clear();
    ++version_;
}

LiveDataPollResult JsonTailLiveDataProvider::pollHot(std::uint64_t nextBatchId) {
    LiveDataPollResult result{};
    result.batch.id = nextBatchId;
    if (sessionDir_.empty()) return result;

    std::error_code ec;
    if (!std::filesystem::exists(sessionDir_, ec) || ec) return result;

    const auto latestSnapshotPath = findLatestSnapshotPath_();
    if (!latestSnapshotPath.empty() && latestSnapshotPath != snapshotPath_) {
        hftrec::replay::SnapshotDocument nextSnapshot{};
        std::ifstream snapshotIn(latestSnapshotPath, std::ios::binary);
        if (!snapshotIn) {
            result.failureStatus = Status::IoError;
            result.failureDetail = "live snapshot read failed";
            return result;
        }

        std::string blob{std::istreambuf_iterator<char>{snapshotIn}, std::istreambuf_iterator<char>{}};
        const auto st = hftrec::replay::parseSnapshotDocument(std::string_view{blob}, nextSnapshot);
        if (!isOk(st)) {
            result.failureStatus = st;
            result.failureDetail = "live snapshot parse failed";
            return result;
        }

        snapshotPath_ = latestSnapshotPath;
        snapshotLoaded_ = true;
        snapshot_ = nextSnapshot;
        result.batch.snapshots.push_back(snapshot_);
        result.appendedRows = true;
    } else if (!snapshotLoaded_ && !latestSnapshotPath.empty()) {
        result.reloadRequired = true;
        return result;
    }

    tailRows(trades_,
             [&result](std::string_view line) {
                 hftrec::replay::TradeRow row{};
                 const auto st = hftrec::replay::parseTradeLine(line, row);
                 if (isOk(st)) result.batch.trades.push_back(std::move(row));
                 return st;
             },
             "trades",
             result);
    if (result.reloadRequired || !isOk(result.failureStatus)) return result;

    tailRows(bookTicker_,
             [&result](std::string_view line) {
                 hftrec::replay::BookTickerRow row{};
                 const auto st = hftrec::replay::parseBookTickerLine(line, row);
                 if (isOk(st)) result.batch.bookTickers.push_back(std::move(row));
                 return st;
             },
             "bookticker",
             result);
    if (result.reloadRequired || !isOk(result.failureStatus)) return result;

    tailRows(depth_,
             [&result](std::string_view line) {
                 hftrec::replay::DepthRow row{};
                 const auto st = hftrec::replay::parseDepthLine(line, row);
                 if (isOk(st)) result.batch.depths.push_back(std::move(row));
                 return st;
             },
             "depth",
             result);

    result.appendedRows = result.appendedRows || hasRows(result.batch);
    if (hasRows(result.batch)) {
        tradesHistory_.insert(tradesHistory_.end(), result.batch.trades.begin(), result.batch.trades.end());
        bookTickerHistory_.insert(bookTickerHistory_.end(), result.batch.bookTickers.begin(), result.batch.bookTickers.end());
        depthHistory_.insert(depthHistory_.end(), result.batch.depths.begin(), result.batch.depths.end());
        ++version_;
    }
    return result;
}

LiveDataBatch JsonTailLiveDataProvider::materializeRange(const LiveDataRangeRequest& request,
                                                         std::uint64_t batchId) const {
    LiveDataBatch batch{};
    batch.id = batchId;
    if (request.tsMax <= request.tsMin) return batch;

    if (snapshotLoaded_ && snapshot_.tsNs <= request.tsMax) {
        batch.snapshots.push_back(snapshot_);
    }

    const auto tradesBegin = std::lower_bound(
        tradesHistory_.begin(),
        tradesHistory_.end(),
        request.tsMin,
        [](const hftrec::replay::TradeRow& row, std::int64_t ts) noexcept { return row.tsNs < ts; });
    const auto tradesEnd = std::upper_bound(
        tradesBegin,
        tradesHistory_.end(),
        request.tsMax,
        [](std::int64_t ts, const hftrec::replay::TradeRow& row) noexcept { return ts < row.tsNs; });
    batch.trades.insert(batch.trades.end(), tradesBegin, tradesEnd);

    const auto tickerBegin = std::lower_bound(
        bookTickerHistory_.begin(),
        bookTickerHistory_.end(),
        request.tsMin,
        [](const hftrec::replay::BookTickerRow& row, std::int64_t ts) noexcept { return row.tsNs < ts; });
    const auto tickerEnd = std::upper_bound(
        tickerBegin,
        bookTickerHistory_.end(),
        request.tsMax,
        [](std::int64_t ts, const hftrec::replay::BookTickerRow& row) noexcept { return ts < row.tsNs; });
    batch.bookTickers.insert(batch.bookTickers.end(), tickerBegin, tickerEnd);

    const std::int64_t depthTsMin = batch.snapshots.empty()
        ? request.tsMin
        : batch.snapshots.back().tsNs;
    const auto depthBegin = std::lower_bound(
        depthHistory_.begin(),
        depthHistory_.end(),
        depthTsMin,
        [](const hftrec::replay::DepthRow& row, std::int64_t ts) noexcept { return row.tsNs < ts; });
    const auto depthEnd = std::upper_bound(
        depthBegin,
        depthHistory_.end(),
        request.tsMax,
        [](std::int64_t ts, const hftrec::replay::DepthRow& row) noexcept { return ts < row.tsNs; });
    batch.depths.insert(batch.depths.end(), depthBegin, depthEnd);
    return batch;
}

LiveDataStats JsonTailLiveDataProvider::stats() const noexcept {
    return LiveDataStats{
        static_cast<std::uint64_t>(tradesHistory_.size()),
        static_cast<std::uint64_t>(bookTickerHistory_.size()),
        static_cast<std::uint64_t>(depthHistory_.size()),
        snapshotLoaded_ ? 1u : 0u,
        version_,
    };
}

InMemoryLiveDataProvider::InMemoryLiveDataProvider(std::vector<SourceRef> sources) {
    sources_.reserve(sources.size());
    for (auto& source : sources) {
        if (source.source == nullptr) continue;
        sources_.push_back(SourceState{std::move(source), 0u, 0u, 0u, 0u});
    }
}

void InMemoryLiveDataProvider::start(const LiveDataProviderConfig& config) {
    activeSourceId_ = config.sourceId;
    activeSymbol_ = config.symbol;
    for (auto& state : sources_) {
        state.seenTrades = 0u;
        state.seenBookTickers = 0u;
        state.seenDepths = 0u;
        state.seenSnapshots = 0u;
    }
    cachedStats_ = LiveDataStats{};
    ++version_;
}

void InMemoryLiveDataProvider::stop() noexcept {
    activeSourceId_.clear();
    activeSymbol_.clear();
    for (auto& state : sources_) {
        state.seenTrades = 0u;
        state.seenBookTickers = 0u;
        state.seenDepths = 0u;
        state.seenSnapshots = 0u;
    }
    cachedStats_ = LiveDataStats{};
    ++version_;
}

LiveDataPollResult InMemoryLiveDataProvider::pollHot(std::uint64_t nextBatchId) {
    LiveDataPollResult result{};
    result.batch.id = nextBatchId;

    LiveDataStats nextStats{};
    for (auto& state : sources_) {
        if (!sourceMatches_(state, activeSourceId_, activeSymbol_)) continue;
        std::size_t tradesTotal = 0u;
        std::size_t bookTickersTotal = 0u;
        std::size_t depthsTotal = 0u;
        std::size_t snapshotsTotal = 0u;
        if (const auto* hotCache = dynamic_cast<const hftrec::storage::IHotEventCache*>(state.ref.source)) {
            const auto stats = hotCache->stats();
            tradesTotal = static_cast<std::size_t>(stats.tradesTotal);
            bookTickersTotal = static_cast<std::size_t>(stats.bookTickersTotal);
            depthsTotal = static_cast<std::size_t>(stats.depthsTotal);
            snapshotsTotal = static_cast<std::size_t>(stats.snapshotsTotal);
        } else {
            const auto currentRows = state.ref.source->readSince(0u, 0u, 0u, 0u);
            tradesTotal = currentRows.trades.size();
            bookTickersTotal = currentRows.bookTickers.size();
            depthsTotal = currentRows.depths.size();
            snapshotsTotal = currentRows.snapshots.size();
        }

        nextStats.tradesTotal += static_cast<std::uint64_t>(tradesTotal);
        nextStats.bookTickersTotal += static_cast<std::uint64_t>(bookTickersTotal);
        nextStats.depthsTotal += static_cast<std::uint64_t>(depthsTotal);
        nextStats.snapshotsTotal += static_cast<std::uint64_t>(snapshotsTotal);

        if (state.seenTrades > tradesTotal) state.seenTrades = 0u;
        if (state.seenBookTickers > bookTickersTotal) state.seenBookTickers = 0u;
        if (state.seenDepths > depthsTotal) state.seenDepths = 0u;
        if (state.seenSnapshots > snapshotsTotal) state.seenSnapshots = 0u;

        const auto delta = state.ref.source->readSince(state.seenTrades,
                                                       state.seenBookTickers,
                                                       state.seenDepths,
                                                       state.seenSnapshots);
        result.batch.trades.insert(result.batch.trades.end(), delta.trades.begin(), delta.trades.end());
        result.batch.bookTickers.insert(result.batch.bookTickers.end(), delta.bookTickers.begin(), delta.bookTickers.end());
        result.batch.depths.insert(result.batch.depths.end(), delta.depths.begin(), delta.depths.end());
        result.batch.snapshots.insert(result.batch.snapshots.end(), delta.snapshots.begin(), delta.snapshots.end());

        state.seenTrades = tradesTotal;
        state.seenBookTickers = bookTickersTotal;
        state.seenDepths = depthsTotal;
        state.seenSnapshots = snapshotsTotal;
    }

    result.appendedRows = hasRows(result.batch);
    if (result.appendedRows) ++version_;
    nextStats.version = version_;
    cachedStats_ = nextStats;
    return result;
}

LiveDataBatch InMemoryLiveDataProvider::materializeRange(const LiveDataRangeRequest& request,
                                                         std::uint64_t batchId) const {
    LiveDataBatch batch{};
    batch.id = batchId;
    if (request.tsMax <= request.tsMin) return batch;

    const std::string_view requestedSymbol = request.symbol.empty()
        ? std::string_view{activeSymbol_}
        : std::string_view{request.symbol};
    for (const auto& state : sources_) {
        if (!sourceMatches_(state, activeSourceId_, requestedSymbol)) continue;
        const auto allRows = state.ref.source->readAll();
        const auto snapshotIt = std::upper_bound(
            allRows.snapshots.begin(),
            allRows.snapshots.end(),
            request.tsMax,
            [](std::int64_t ts, const hftrec::replay::SnapshotDocument& row) noexcept { return ts < row.tsNs; });
        std::int64_t depthTsMin = request.tsMin;
        if (snapshotIt != allRows.snapshots.begin()) {
            const auto& snapshot = *std::prev(snapshotIt);
            batch.snapshots.push_back(snapshot);
            depthTsMin = snapshot.tsNs;
        }

        appendSortedRange(allRows.trades, request.tsMin, request.tsMax, batch.trades);
        appendSortedRange(allRows.bookTickers, request.tsMin, request.tsMax, batch.bookTickers);
        appendSortedRange(allRows.depths, depthTsMin, request.tsMax, batch.depths);
    }
    return batch;
}

LiveDataStats InMemoryLiveDataProvider::stats() const noexcept {
    return cachedStats_;
}

bool InMemoryLiveDataProvider::sourceMatches_(const SourceState& state,
                                              std::string_view sourceId,
                                              std::string_view symbol) const noexcept {
    return state.ref.source != nullptr
        && (sourceId.empty() || state.ref.sourceId == sourceId)
        && (symbol.empty() || state.ref.symbol == symbol);
}

LiveDataRegistry& LiveDataRegistry::instance() noexcept {
    static LiveDataRegistry registry{};
    return registry;
}

void LiveDataRegistry::setSources(std::vector<RegisteredSource> sources) {
    std::lock_guard<std::mutex> lock(mutex_);
    sources_.clear();
    sources_.reserve(sources.size());
    for (auto& source : sources) {
        if (source.ingress == nullptr || source.ingress->eventSource() == nullptr) continue;
        sources_.push_back(std::move(source));
    }
}

void LiveDataRegistry::clear() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    sources_.clear();
}

std::unique_ptr<ILiveDataProvider> LiveDataRegistry::makeProvider(std::string_view sourceId) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<InMemoryLiveDataProvider::SourceRef> refs;
    refs.reserve(sources_.size());
    for (const auto& source : sources_) {
        if (!sourceId.empty() && source.viewerSourceId != sourceId) continue;
        refs.push_back(InMemoryLiveDataProvider::SourceRef{
            source.viewerSourceId,
            source.exchange,
            source.market,
            source.symbol,
            source.ingress->eventSource()});
    }

    if (refs.empty()) return nullptr;
    return std::make_unique<InMemoryLiveDataProvider>(std::move(refs));
}

bool LiveDataRegistry::hasSource(std::string_view sourceId) const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& source : sources_) {
        if (source.viewerSourceId == sourceId) return true;
    }
    return false;
}

bool LiveDataRegistry::hasSources() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return !sources_.empty();
}

std::vector<LiveDataRegistry::RegisteredSource> LiveDataRegistry::snapshotSources() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return sources_;
}

void JsonTailLiveDataProvider::syncTailOffset_(TailFile& file) noexcept {
    std::error_code ec;
    if (std::filesystem::exists(file.path, ec) && !ec) {
        file.offset = std::filesystem::file_size(file.path, ec);
        if (ec) file.offset = 0;
    } else {
        file.offset = 0;
    }
    file.pending.clear();
}

std::filesystem::path JsonTailLiveDataProvider::findLatestSnapshotPath_() const {
    std::filesystem::path latest{};
    if (sessionDir_.empty()) return latest;

    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(sessionDir_, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        const auto filename = entry.path().filename().string();
        if (!filename.starts_with("snapshot_") || entry.path().extension() != ".json") continue;
        if (latest.empty() || filename > latest.filename().string()) latest = entry.path();
    }
    return latest;
}

}  // namespace hftrec::gui::viewer








