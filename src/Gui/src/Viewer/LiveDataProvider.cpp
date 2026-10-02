#include "LiveDataProvider.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <chrono>

#include "../../../Runtime/src/Replay/JsonLineParser.hpp"
#include "../../../Runtime/src/Corpus/Storage/EventStorage.hpp"

namespace hftrec::gui::viewer {

namespace {

bool hasRows(const LiveDataBatch& batch) noexcept {
    return !batch.trades.empty()
        || !batch.bookTickers.empty()
        || !batch.depths.empty()
        || !batch.snapshots.empty();
}

constexpr std::size_t kMaxTradeHistoryRows = 200'000u;
constexpr std::size_t kMaxBookTickerHistoryRows = 200'000u;
constexpr std::size_t kMaxDepthHistoryRows = 20'000u;
constexpr std::uintmax_t kMaxTailReadBytes = 8u * 1024u * 1024u;

template <typename Row>
void keepRecentRows(std::vector<Row>& rows, std::size_t cap) {
    if (rows.size() <= cap) return;
    rows.erase(rows.begin(), rows.begin() + static_cast<std::ptrdiff_t>(rows.size() - cap));
}

void addObservedRows(LiveDataStats& stats, const LiveDataBatch& batch) noexcept {
    stats.tradesTotal += static_cast<std::uint64_t>(batch.trades.size());
    stats.bookTickersTotal += static_cast<std::uint64_t>(batch.bookTickers.size());
    stats.depthsTotal += static_cast<std::uint64_t>(batch.depths.size());
    stats.snapshotsTotal += static_cast<std::uint64_t>(batch.snapshots.size());
}


std::filesystem::path liveChannelPath(const std::filesystem::path& sessionDir, const char* fileName) {
    const auto nextPath = sessionDir / "jsonl" / fileName;
    std::error_code ec;
    if (std::filesystem::exists(nextPath, ec) && !ec) return nextPath;
    if (std::filesystem::exists(nextPath.parent_path(), ec) && !ec) return nextPath;
    return sessionDir / fileName;
}

std::filesystem::path liveDepthTapeChannelPath(const std::filesystem::path& sessionDir) {
    return liveChannelPath(sessionDir, "depth_tape.jsonl");
}

std::filesystem::path liveDepthSidecarChannelPath(const std::filesystem::path& sessionDir) {
    return liveChannelPath(sessionDir, "depth_sidecar.jsonl");
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
    const auto bytesToRead = std::min<std::uintmax_t>(fileSize - file.offset, kMaxTailReadBytes);

    std::ifstream in(file.path, std::ios::binary);
    if (!in) {
        result.failureStatus = Status::IoError;
        result.failureDetail = "live " + std::string{label} + " read failed";
        return;
    }

    in.seekg(static_cast<std::streamoff>(file.offset), std::ios::beg);
    std::string chunk(static_cast<std::size_t>(bytesToRead), '\0');
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
            const auto st = consumeLine(std::string_view{line});
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

void collectTailLines(JsonTailLiveDataProvider::TailFile& file,
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
    const auto bytesToRead = std::min<std::uintmax_t>(fileSize - file.offset, kMaxTailReadBytes);

    std::ifstream in(file.path, std::ios::binary);
    if (!in) {
        result.failureStatus = Status::IoError;
        result.failureDetail = "live " + std::string{label} + " read failed";
        return;
    }

    in.seekg(static_cast<std::streamoff>(file.offset), std::ios::beg);
    std::string chunk(static_cast<std::size_t>(bytesToRead), '\0');
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
        if (!line.empty()) file.ready.push_back(std::move(line));
        lineStart = lineEnd + 1;
    }

    nextPending.erase(0, lineStart);
    file.offset = nextOffset;
    file.pending = std::move(nextPending);
}

void tailDepthTapeSidecarRows(JsonTailLiveDataProvider::TailFile& tapeFile,
                              JsonTailLiveDataProvider::TailFile& sidecarFile,
                              LiveDataPollResult& result) {
    collectTailLines(tapeFile, "depth_tape", result);
    if (result.reloadRequired || !isOk(result.failureStatus)) return;
    collectTailLines(sidecarFile, "depth_sidecar", result);
    if (result.reloadRequired || !isOk(result.failureStatus)) return;

    const std::size_t pairCount = std::min(tapeFile.ready.size(), sidecarFile.ready.size());
    for (std::size_t i = 0; i < pairCount; ++i) {
        hftrec::replay::DepthRow row{};
        const auto st = hftrec::replay::parseDepthTapeSidecarLine(tapeFile.ready[i], sidecarFile.ready[i], row);
        if (!isOk(st)) {
            result.reloadRequired = true;
            result.failureStatus = st;
            result.failureDetail = "live depth_tape/depth_sidecar parse failed, scheduling reload";
            return;
        }
        result.batch.depths.push_back(std::move(row));
    }

    if (pairCount != 0u) {
        tapeFile.ready.erase(tapeFile.ready.begin(), tapeFile.ready.begin() + static_cast<std::ptrdiff_t>(pairCount));
        sidecarFile.ready.erase(sidecarFile.ready.begin(), sidecarFile.ready.begin() + static_cast<std::ptrdiff_t>(pairCount));
        result.appendedRows = true;
    }
}

}  // namespace

void JsonTailLiveDataProvider::start(const LiveDataProviderConfig& config) {
    sessionDir_ = config.sessionDir;
    tradesHistory_.clear();
    bookTickerHistory_.clear();
    depthHistory_.clear();
    observedStats_ = LiveDataStats{};
    ++version_;
    trades_ = TailFile{liveChannelPath(sessionDir_, "trades.jsonl"), 0, {}};
    bookTicker_ = TailFile{liveChannelPath(sessionDir_, "bookticker.jsonl"), 0, {}};
    const auto depthTapePath = liveDepthTapeChannelPath(sessionDir_);
    const auto depthSidecarPath = liveDepthSidecarChannelPath(sessionDir_);
    depthTape_ = TailFile{depthTapePath, 0, {}};
    depth_ = TailFile{depthSidecarPath, 0, {}};
    syncTailOffset_(trades_);
    syncTailOffset_(bookTicker_);
    syncTailOffset_(depthTape_);
    syncTailOffset_(depth_);
    observedStats_.version = version_;

}

void JsonTailLiveDataProvider::stop() noexcept {
    sessionDir_.clear();
    trades_ = TailFile{};
    bookTicker_ = TailFile{};
    depthTape_ = TailFile{};
    depth_ = TailFile{};
    tradesHistory_.clear();
    bookTickerHistory_.clear();
    depthHistory_.clear();
    observedStats_ = LiveDataStats{};
    ++version_;
    observedStats_.version = version_;
}

LiveDataPollResult JsonTailLiveDataProvider::pollHot(std::uint64_t nextBatchId) {
    LiveDataPollResult result{};
    result.batch.id = nextBatchId;
    if (sessionDir_.empty()) return result;

    std::error_code ec;
    if (!std::filesystem::exists(sessionDir_, ec) || ec) return result;

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









    tailDepthTapeSidecarRows(depthTape_, depth_, result);

    result.appendedRows = result.appendedRows || hasRows(result.batch);
    if (hasRows(result.batch)) {
        addObservedRows(observedStats_, result.batch);
        keepRecentRows(result.batch.trades, kMaxTradeHistoryRows);
        keepRecentRows(result.batch.bookTickers, kMaxBookTickerHistoryRows);
        keepRecentRows(result.batch.depths, kMaxDepthHistoryRows);
        tradesHistory_.insert(tradesHistory_.end(), result.batch.trades.begin(), result.batch.trades.end());
        bookTickerHistory_.insert(bookTickerHistory_.end(), result.batch.bookTickers.begin(), result.batch.bookTickers.end());
        depthHistory_.insert(depthHistory_.end(), result.batch.depths.begin(), result.batch.depths.end());
        keepRecentRows(tradesHistory_, kMaxTradeHistoryRows);
        keepRecentRows(bookTickerHistory_, kMaxBookTickerHistoryRows);
        keepRecentRows(depthHistory_, kMaxDepthHistoryRows);
        ++version_;
        observedStats_.version = version_;
    }
    return result;
}

LiveDataBatch JsonTailLiveDataProvider::materializeRange(const LiveDataRangeRequest& request,
                                                         std::uint64_t batchId) const {
    LiveDataBatch batch{};
    batch.id = batchId;
    if (request.tsMax <= request.tsMin) return batch;

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
        ? std::numeric_limits<std::int64_t>::min()
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
    auto stats = observedStats_;
    stats.version = version_;
    return stats;
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
        const auto currentRows = state.ref.source->readAll();
        if (const auto* hotCache = dynamic_cast<const hftrec::storage::IHotEventCache*>(state.ref.source)) {
            const auto stats = hotCache->stats();
            tradesTotal = static_cast<std::size_t>(stats.tradesTotal);
            bookTickersTotal = static_cast<std::size_t>(stats.bookTickersTotal);
            depthsTotal = static_cast<std::size_t>(stats.depthsTotal);
            snapshotsTotal = static_cast<std::size_t>(stats.snapshotsTotal);
        } else {
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
        hftrec::replay::SnapshotDocument snapshot{};
        std::int64_t depthTsMin = request.tsMin;
        if (state.ref.source->readSnapshotAtOrBefore(request.tsMax, snapshot)) {
            batch.snapshots.push_back(snapshot);
            depthTsMin = snapshot.tsNs;
        }

        const auto visibleRows = state.ref.source->readRange(request.tsMin, request.tsMax);
        batch.trades.insert(batch.trades.end(), visibleRows.trades.begin(), visibleRows.trades.end());
        batch.bookTickers.insert(batch.bookTickers.end(), visibleRows.bookTickers.begin(), visibleRows.bookTickers.end());

        auto depthRows = state.ref.source->readDepthRange(depthTsMin, request.tsMax);
        if (batch.snapshots.empty() && depthTsMin == request.tsMin) {
            auto bootstrapRows = state.ref.source->readDepthRange(0, request.tsMax);
            auto firstVisible = std::lower_bound(
                bootstrapRows.begin(),
                bootstrapRows.end(),
                request.tsMin,
                [](const hftrec::replay::DepthRow& row, std::int64_t ts) noexcept {
                    return row.tsNs < ts;
                });
            if (firstVisible != bootstrapRows.begin()) {
                batch.depths.push_back(*std::prev(firstVisible));
            }
        }
        batch.depths.insert(batch.depths.end(), depthRows.begin(), depthRows.end());
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
    file.ready.clear();
}

}  // namespace hftrec::gui::viewer
