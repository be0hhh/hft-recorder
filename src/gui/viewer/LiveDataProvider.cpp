#include "gui/viewer/LiveDataProvider.hpp"

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

#include "core/metrics/Metrics.hpp"
#include "core/replay/JsonLineParser.hpp"
#include "core/storage/EventStorage.hpp"

namespace hftrec::gui::viewer {

namespace {

bool hasRows(const LiveDataBatch& batch) noexcept {
    return !batch.trades.empty()
        || !batch.liquidations.empty()
        || !batch.bookTickers.empty()
        || !batch.markPrices.empty()
        || !batch.indexPrices.empty()
        || !batch.fundings.empty()
        || !batch.priceLimits.empty()
        || !batch.depths.empty()
        || !batch.snapshots.empty();
}

constexpr std::size_t kMaxTradeHistoryRows = 200'000u;
constexpr std::size_t kMaxLiquidationHistoryRows = 50'000u;
constexpr std::size_t kMaxBookTickerHistoryRows = 200'000u;
constexpr std::size_t kMaxReferenceHistoryRows = 50'000u;
constexpr std::size_t kMaxDepthHistoryRows = 20'000u;
constexpr std::uintmax_t kMaxTailReadBytes = 8u * 1024u * 1024u;

template <typename Row>
void keepRecentRows(std::vector<Row>& rows, std::size_t cap) {
    if (rows.size() <= cap) return;
    rows.erase(rows.begin(), rows.begin() + static_cast<std::ptrdiff_t>(rows.size() - cap));
}

void addObservedRows(LiveDataStats& stats, const LiveDataBatch& batch) noexcept {
    stats.tradesTotal += static_cast<std::uint64_t>(batch.trades.size());
    stats.liquidationsTotal += static_cast<std::uint64_t>(batch.liquidations.size());
    stats.bookTickersTotal += static_cast<std::uint64_t>(batch.bookTickers.size());
    stats.markPricesTotal += static_cast<std::uint64_t>(batch.markPrices.size());
    stats.indexPricesTotal += static_cast<std::uint64_t>(batch.indexPrices.size());
    stats.fundingsTotal += static_cast<std::uint64_t>(batch.fundings.size());
    stats.priceLimitsTotal += static_cast<std::uint64_t>(batch.priceLimits.size());
    stats.depthsTotal += static_cast<std::uint64_t>(batch.depths.size());
    stats.snapshotsTotal += static_cast<std::uint64_t>(batch.snapshots.size());
}

template <typename Row>
void appendRowsSince(const std::vector<Row>& src, std::size_t offset, std::vector<Row>& out) {
    if (offset >= src.size()) return;
    out.insert(out.end(), src.begin() + static_cast<std::ptrdiff_t>(offset), src.end());
}

std::filesystem::path liveChannelPath(const std::filesystem::path& sessionDir, const char* fileName) {
    const auto nextPath = sessionDir / "jsonl" / fileName;
    std::error_code ec;
    if (std::filesystem::exists(nextPath, ec) && !ec) return nextPath;
    if (std::filesystem::exists(nextPath.parent_path(), ec) && !ec) return nextPath;
    return sessionDir / fileName;
}

bool regularFileExists(const std::filesystem::path& path) noexcept {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec) && !ec;
}

std::filesystem::path liveDepthTapeChannelPath(const std::filesystem::path& sessionDir) {
    return liveChannelPath(sessionDir, "depth_tape.jsonl");
}

std::filesystem::path liveDepthSidecarChannelPath(const std::filesystem::path& sessionDir) {
    return liveChannelPath(sessionDir, "depth_sidecar.jsonl");
}

std::filesystem::path liveDepthLegacyChannelPath(const std::filesystem::path& sessionDir) {
    const auto legacyPath = sessionDir / "jsonl" / "depth.jsonl";
    if (regularFileExists(legacyPath)) return legacyPath;
    return sessionDir / "depth.jsonl";
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
            const auto parseStart = std::chrono::steady_clock::now();
            const auto st = consumeLine(std::string_view{line});
            const auto parseNs = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - parseStart).count());
            hftrec::metrics::recordLiveJsonTailParse(parseNs, label);
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
        const auto parseStart = std::chrono::steady_clock::now();
        const auto st = hftrec::replay::parseDepthTapeSidecarLine(tapeFile.ready[i], sidecarFile.ready[i], row);
        const auto parseNs = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - parseStart).count());
        hftrec::metrics::recordLiveJsonTailParse(parseNs, "depth");
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
    liquidationHistory_.clear();
    bookTickerHistory_.clear();
    markPriceHistory_.clear();
    indexPriceHistory_.clear();
    fundingHistory_.clear();
    priceLimitHistory_.clear();
    depthHistory_.clear();
    observedStats_ = LiveDataStats{};
    ++version_;
    trades_ = TailFile{liveChannelPath(sessionDir_, "trades.jsonl"), 0, {}};
    liquidations_ = TailFile{liveChannelPath(sessionDir_, "liquidations.jsonl"), 0, {}};
    bookTicker_ = TailFile{liveChannelPath(sessionDir_, "bookticker.jsonl"), 0, {}};
    markPrice_ = TailFile{liveChannelPath(sessionDir_, "mark_price.jsonl"), 0, {}};
    indexPrice_ = TailFile{liveChannelPath(sessionDir_, "index_price.jsonl"), 0, {}};
    funding_ = TailFile{liveChannelPath(sessionDir_, "funding.jsonl"), 0, {}};
    priceLimit_ = TailFile{liveChannelPath(sessionDir_, "price_limit.jsonl"), 0, {}};
    const auto depthTapePath = liveDepthTapeChannelPath(sessionDir_);
    const auto depthSidecarPath = liveDepthSidecarChannelPath(sessionDir_);
    depthTapeSidecarMode_ = regularFileExists(depthTapePath) || regularFileExists(depthSidecarPath);
    depthTape_ = depthTapeSidecarMode_ ? TailFile{depthTapePath, 0, {}} : TailFile{};
    depth_ = depthTapeSidecarMode_ ? TailFile{depthSidecarPath, 0, {}} : TailFile{liveDepthLegacyChannelPath(sessionDir_), 0, {}};
    syncTailOffset_(trades_);
    syncTailOffset_(liquidations_);
    syncTailOffset_(bookTicker_);
    syncTailOffset_(markPrice_);
    syncTailOffset_(indexPrice_);
    syncTailOffset_(funding_);
    syncTailOffset_(priceLimit_);
    syncTailOffset_(depthTape_);
    syncTailOffset_(depth_);
    snapshotPath_.clear();
    snapshotDiscoveredPath_.clear();
    snapshotDirWriteTime_ = std::filesystem::file_time_type{};
    snapshotDirWriteTimeValid_ = false;
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
    observedStats_.snapshotsTotal = snapshotLoaded_ ? 1u : 0u;
    observedStats_.version = version_;

}

void JsonTailLiveDataProvider::stop() noexcept {
    sessionDir_.clear();
    trades_ = TailFile{};
    liquidations_ = TailFile{};
    bookTicker_ = TailFile{};
    markPrice_ = TailFile{};
    indexPrice_ = TailFile{};
    funding_ = TailFile{};
    priceLimit_ = TailFile{};
    depthTape_ = TailFile{};
    depth_ = TailFile{};
    depthTapeSidecarMode_ = false;
    snapshotPath_.clear();
    snapshotDiscoveredPath_.clear();
    snapshotDirWriteTime_ = std::filesystem::file_time_type{};
    snapshotDirWriteTimeValid_ = false;
    snapshotLoaded_ = false;
    snapshot_ = hftrec::replay::SnapshotDocument{};
    tradesHistory_.clear();
    liquidationHistory_.clear();
    bookTickerHistory_.clear();
    markPriceHistory_.clear();
    indexPriceHistory_.clear();
    fundingHistory_.clear();
    priceLimitHistory_.clear();
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

    tailRows(liquidations_,
             [&result](std::string_view line) {
                 hftrec::replay::LiquidationRow row{};
                 const auto st = hftrec::replay::parseLiquidationLine(line, row);
                 if (isOk(st)) result.batch.liquidations.push_back(std::move(row));
                 return st;
             },
             "liquidations",
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

    tailRows(markPrice_,
             [&result](std::string_view line) {
                 hftrec::replay::MarkPriceRow row{};
                 const auto st = hftrec::replay::parseMarkPriceLine(line, row);
                 if (isOk(st)) result.batch.markPrices.push_back(std::move(row));
                 return st;
             },
             "mark_price",
             result);
    if (result.reloadRequired || !isOk(result.failureStatus)) return result;

    tailRows(indexPrice_,
             [&result](std::string_view line) {
                 hftrec::replay::IndexPriceRow row{};
                 const auto st = hftrec::replay::parseIndexPriceLine(line, row);
                 if (isOk(st)) result.batch.indexPrices.push_back(std::move(row));
                 return st;
             },
             "index_price",
             result);
    if (result.reloadRequired || !isOk(result.failureStatus)) return result;

    tailRows(funding_,
             [&result](std::string_view line) {
                 hftrec::replay::FundingRow row{};
                 const auto st = hftrec::replay::parseFundingLine(line, row);
                 if (isOk(st)) result.batch.fundings.push_back(std::move(row));
                 return st;
             },
             "funding",
             result);
    if (result.reloadRequired || !isOk(result.failureStatus)) return result;

    tailRows(priceLimit_,
             [&result](std::string_view line) {
                 hftrec::replay::PriceLimitRow row{};
                 const auto st = hftrec::replay::parsePriceLimitLine(line, row);
                 if (isOk(st)) result.batch.priceLimits.push_back(std::move(row));
                 return st;
             },
             "price_limit",
             result);
    if (result.reloadRequired || !isOk(result.failureStatus)) return result;

    if (depthTapeSidecarMode_) {
        tailDepthTapeSidecarRows(depthTape_, depth_, result);
    } else {
        tailRows(depth_,
                 [&result](std::string_view line) {
                     hftrec::replay::DepthRow row{};
                     const auto st = hftrec::replay::parseDepthLine(line, row);
                     if (isOk(st)) result.batch.depths.push_back(std::move(row));
                     return st;
                 },
                 "depth",
                 result);
    }

    result.appendedRows = result.appendedRows || hasRows(result.batch);
    if (hasRows(result.batch)) {
        addObservedRows(observedStats_, result.batch);
        keepRecentRows(result.batch.trades, kMaxTradeHistoryRows);
        keepRecentRows(result.batch.liquidations, kMaxLiquidationHistoryRows);
        keepRecentRows(result.batch.bookTickers, kMaxBookTickerHistoryRows);
        keepRecentRows(result.batch.markPrices, kMaxReferenceHistoryRows);
        keepRecentRows(result.batch.indexPrices, kMaxReferenceHistoryRows);
        keepRecentRows(result.batch.fundings, kMaxReferenceHistoryRows);
        keepRecentRows(result.batch.priceLimits, kMaxReferenceHistoryRows);
        keepRecentRows(result.batch.depths, kMaxDepthHistoryRows);
        tradesHistory_.insert(tradesHistory_.end(), result.batch.trades.begin(), result.batch.trades.end());
        liquidationHistory_.insert(liquidationHistory_.end(), result.batch.liquidations.begin(), result.batch.liquidations.end());
        bookTickerHistory_.insert(bookTickerHistory_.end(), result.batch.bookTickers.begin(), result.batch.bookTickers.end());
        markPriceHistory_.insert(markPriceHistory_.end(), result.batch.markPrices.begin(), result.batch.markPrices.end());
        indexPriceHistory_.insert(indexPriceHistory_.end(), result.batch.indexPrices.begin(), result.batch.indexPrices.end());
        fundingHistory_.insert(fundingHistory_.end(), result.batch.fundings.begin(), result.batch.fundings.end());
        priceLimitHistory_.insert(priceLimitHistory_.end(), result.batch.priceLimits.begin(), result.batch.priceLimits.end());
        depthHistory_.insert(depthHistory_.end(), result.batch.depths.begin(), result.batch.depths.end());
        keepRecentRows(tradesHistory_, kMaxTradeHistoryRows);
        keepRecentRows(liquidationHistory_, kMaxLiquidationHistoryRows);
        keepRecentRows(bookTickerHistory_, kMaxBookTickerHistoryRows);
        keepRecentRows(markPriceHistory_, kMaxReferenceHistoryRows);
        keepRecentRows(indexPriceHistory_, kMaxReferenceHistoryRows);
        keepRecentRows(fundingHistory_, kMaxReferenceHistoryRows);
        keepRecentRows(priceLimitHistory_, kMaxReferenceHistoryRows);
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

    const auto liqBegin = std::lower_bound(
        liquidationHistory_.begin(),
        liquidationHistory_.end(),
        request.tsMin,
        [](const hftrec::replay::LiquidationRow& row, std::int64_t ts) noexcept { return row.tsNs < ts; });
    const auto liqEnd = std::upper_bound(
        liqBegin,
        liquidationHistory_.end(),
        request.tsMax,
        [](std::int64_t ts, const hftrec::replay::LiquidationRow& row) noexcept { return ts < row.tsNs; });
    batch.liquidations.insert(batch.liquidations.end(), liqBegin, liqEnd);

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

    const auto markBegin = std::lower_bound(
        markPriceHistory_.begin(),
        markPriceHistory_.end(),
        request.tsMin,
        [](const hftrec::replay::MarkPriceRow& row, std::int64_t ts) noexcept { return row.tsNs < ts; });
    const auto markEnd = std::upper_bound(
        markBegin,
        markPriceHistory_.end(),
        request.tsMax,
        [](std::int64_t ts, const hftrec::replay::MarkPriceRow& row) noexcept { return ts < row.tsNs; });
    batch.markPrices.insert(batch.markPrices.end(), markBegin, markEnd);

    const auto indexBegin = std::lower_bound(
        indexPriceHistory_.begin(),
        indexPriceHistory_.end(),
        request.tsMin,
        [](const hftrec::replay::IndexPriceRow& row, std::int64_t ts) noexcept { return row.tsNs < ts; });
    const auto indexEnd = std::upper_bound(
        indexBegin,
        indexPriceHistory_.end(),
        request.tsMax,
        [](std::int64_t ts, const hftrec::replay::IndexPriceRow& row) noexcept { return ts < row.tsNs; });
    batch.indexPrices.insert(batch.indexPrices.end(), indexBegin, indexEnd);

    const auto fundingBegin = std::lower_bound(
        fundingHistory_.begin(),
        fundingHistory_.end(),
        request.tsMin,
        [](const hftrec::replay::FundingRow& row, std::int64_t ts) noexcept { return row.tsNs < ts; });
    const auto fundingEnd = std::upper_bound(
        fundingBegin,
        fundingHistory_.end(),
        request.tsMax,
        [](std::int64_t ts, const hftrec::replay::FundingRow& row) noexcept { return ts < row.tsNs; });
    batch.fundings.insert(batch.fundings.end(), fundingBegin, fundingEnd);

    const auto limitBegin = std::lower_bound(
        priceLimitHistory_.begin(),
        priceLimitHistory_.end(),
        request.tsMin,
        [](const hftrec::replay::PriceLimitRow& row, std::int64_t ts) noexcept { return row.tsNs < ts; });
    const auto limitEnd = std::upper_bound(
        limitBegin,
        priceLimitHistory_.end(),
        request.tsMax,
        [](std::int64_t ts, const hftrec::replay::PriceLimitRow& row) noexcept { return ts < row.tsNs; });
    batch.priceLimits.insert(batch.priceLimits.end(), limitBegin, limitEnd);

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
        sources_.push_back(SourceState{std::move(source), 0u, 0u, 0u, 0u, 0u});
    }
}

void InMemoryLiveDataProvider::start(const LiveDataProviderConfig& config) {
    activeSourceId_ = config.sourceId;
    activeSymbol_ = config.symbol;
    for (auto& state : sources_) {
        state.seenTrades = 0u;
        state.seenLiquidations = 0u;
        state.seenBookTickers = 0u;
        state.seenMarkPrices = 0u;
        state.seenIndexPrices = 0u;
        state.seenFundings = 0u;
        state.seenPriceLimits = 0u;
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
        state.seenLiquidations = 0u;
        state.seenBookTickers = 0u;
        state.seenMarkPrices = 0u;
        state.seenIndexPrices = 0u;
        state.seenFundings = 0u;
        state.seenPriceLimits = 0u;
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
        std::size_t liquidationsTotal = 0u;
        std::size_t bookTickersTotal = 0u;
        std::size_t markPricesTotal = 0u;
        std::size_t indexPricesTotal = 0u;
        std::size_t fundingsTotal = 0u;
        std::size_t priceLimitsTotal = 0u;
        std::size_t depthsTotal = 0u;
        std::size_t snapshotsTotal = 0u;
        const auto currentRows = state.ref.source->readAll();
        if (const auto* hotCache = dynamic_cast<const hftrec::storage::IHotEventCache*>(state.ref.source)) {
            const auto stats = hotCache->stats();
            tradesTotal = static_cast<std::size_t>(stats.tradesTotal);
            liquidationsTotal = static_cast<std::size_t>(stats.liquidationsTotal);
            bookTickersTotal = static_cast<std::size_t>(stats.bookTickersTotal);
            depthsTotal = static_cast<std::size_t>(stats.depthsTotal);
            snapshotsTotal = static_cast<std::size_t>(stats.snapshotsTotal);
        } else {
            tradesTotal = currentRows.trades.size();
            liquidationsTotal = currentRows.liquidations.size();
            bookTickersTotal = currentRows.bookTickers.size();
            depthsTotal = currentRows.depths.size();
            snapshotsTotal = currentRows.snapshots.size();
        }
        markPricesTotal = currentRows.markPrices.size();
        indexPricesTotal = currentRows.indexPrices.size();
        fundingsTotal = currentRows.fundings.size();
        priceLimitsTotal = currentRows.priceLimits.size();

        nextStats.tradesTotal += static_cast<std::uint64_t>(tradesTotal);
        nextStats.liquidationsTotal += static_cast<std::uint64_t>(liquidationsTotal);
        nextStats.bookTickersTotal += static_cast<std::uint64_t>(bookTickersTotal);
        nextStats.markPricesTotal += static_cast<std::uint64_t>(markPricesTotal);
        nextStats.indexPricesTotal += static_cast<std::uint64_t>(indexPricesTotal);
        nextStats.fundingsTotal += static_cast<std::uint64_t>(fundingsTotal);
        nextStats.priceLimitsTotal += static_cast<std::uint64_t>(priceLimitsTotal);
        nextStats.depthsTotal += static_cast<std::uint64_t>(depthsTotal);
        nextStats.snapshotsTotal += static_cast<std::uint64_t>(snapshotsTotal);

        if (state.seenTrades > tradesTotal) state.seenTrades = 0u;
        if (state.seenLiquidations > liquidationsTotal) state.seenLiquidations = 0u;
        if (state.seenBookTickers > bookTickersTotal) state.seenBookTickers = 0u;
        if (state.seenMarkPrices > markPricesTotal) state.seenMarkPrices = 0u;
        if (state.seenIndexPrices > indexPricesTotal) state.seenIndexPrices = 0u;
        if (state.seenFundings > fundingsTotal) state.seenFundings = 0u;
        if (state.seenPriceLimits > priceLimitsTotal) state.seenPriceLimits = 0u;
        if (state.seenDepths > depthsTotal) state.seenDepths = 0u;
        if (state.seenSnapshots > snapshotsTotal) state.seenSnapshots = 0u;

        const auto delta = state.ref.source->readSince(state.seenTrades,
                                                       state.seenLiquidations,
                                                       state.seenBookTickers,
                                                       state.seenDepths,
                                                       state.seenSnapshots);
        result.batch.trades.insert(result.batch.trades.end(), delta.trades.begin(), delta.trades.end());
        result.batch.liquidations.insert(result.batch.liquidations.end(), delta.liquidations.begin(), delta.liquidations.end());
        result.batch.bookTickers.insert(result.batch.bookTickers.end(), delta.bookTickers.begin(), delta.bookTickers.end());
        appendRowsSince(currentRows.markPrices, state.seenMarkPrices, result.batch.markPrices);
        appendRowsSince(currentRows.indexPrices, state.seenIndexPrices, result.batch.indexPrices);
        appendRowsSince(currentRows.fundings, state.seenFundings, result.batch.fundings);
        appendRowsSince(currentRows.priceLimits, state.seenPriceLimits, result.batch.priceLimits);
        result.batch.depths.insert(result.batch.depths.end(), delta.depths.begin(), delta.depths.end());
        result.batch.snapshots.insert(result.batch.snapshots.end(), delta.snapshots.begin(), delta.snapshots.end());

        state.seenTrades = tradesTotal;
        state.seenLiquidations = liquidationsTotal;
        state.seenBookTickers = bookTickersTotal;
        state.seenMarkPrices = markPricesTotal;
        state.seenIndexPrices = indexPricesTotal;
        state.seenFundings = fundingsTotal;
        state.seenPriceLimits = priceLimitsTotal;
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
        batch.liquidations.insert(batch.liquidations.end(), visibleRows.liquidations.begin(), visibleRows.liquidations.end());
        batch.bookTickers.insert(batch.bookTickers.end(), visibleRows.bookTickers.begin(), visibleRows.bookTickers.end());
        batch.markPrices.insert(batch.markPrices.end(), visibleRows.markPrices.begin(), visibleRows.markPrices.end());
        batch.indexPrices.insert(batch.indexPrices.end(), visibleRows.indexPrices.begin(), visibleRows.indexPrices.end());
        batch.fundings.insert(batch.fundings.end(), visibleRows.fundings.begin(), visibleRows.fundings.end());
        batch.priceLimits.insert(batch.priceLimits.end(), visibleRows.priceLimits.begin(), visibleRows.priceLimits.end());

        auto depthRows = state.ref.source->readDepthRange(depthTsMin, request.tsMax);
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

std::filesystem::path JsonTailLiveDataProvider::findLatestSnapshotPath_() {
    std::filesystem::path latest{};
    if (sessionDir_.empty()) return latest;

    std::error_code ec;
    const auto dirWriteTime = std::filesystem::last_write_time(sessionDir_, ec);
    const bool dirWriteTimeOk = !ec;
    if (dirWriteTimeOk && snapshotDirWriteTimeValid_ && dirWriteTime == snapshotDirWriteTime_) return snapshotDiscoveredPath_;
    ec.clear();
    for (const auto& entry : std::filesystem::directory_iterator(sessionDir_, ec)) {
        if (ec) break;
        if (!entry.is_regular_file()) continue;
        const auto filename = entry.path().filename().string();
        if (!filename.starts_with("snapshot_") || entry.path().extension() != ".json") continue;
        if (latest.empty() || filename > latest.filename().string()) latest = entry.path();
    }
    if (!ec && dirWriteTimeOk) {
        snapshotDiscoveredPath_ = latest;
        snapshotDirWriteTime_ = dirWriteTime;
        snapshotDirWriteTimeValid_ = true;
    }
    return latest;
}

}  // namespace hftrec::gui::viewer
