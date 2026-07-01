#include "core/capture/CaptureCoordinator.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/capture/Candles2BulkState.hpp"
#include "core/capture/CaptureCoordinatorInternal.hpp"
#include "core/capture/JsonSerializers.hpp"
#include "core/replay/EventRows.hpp"
#include "hft_trader/runtime/history/candles/CandleHistoryLoader.hpp"
#include "primitives/composite/Ohlcv.hpp"

namespace hftrec::capture {

namespace {

constexpr std::uint32_t kDetailedCandlesDefaultLimit = 5000u;
constexpr std::uint32_t kDetailedCandlesMaxLimit = 1'000'000u;

void copySymbolFromText(Symbol& out, std::string_view text) noexcept {
    char buffer[rawdata::SymbolMaxBytes]{};
    const std::size_t copyLen = std::min(text.size(), sizeof(buffer) - 1u);
    for (std::size_t i = 0u; i < copyLen; ++i) buffer[i] = text[i];
    out.copyFrom(buffer);
}

bool textEqualsAscii(std::string_view lhs, std::string_view rhs) noexcept {
    if (lhs.size() != rhs.size()) return false;
    for (std::size_t i = 0u; i < lhs.size(); ++i) {
        char a = lhs[i];
        char b = rhs[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + ('a' - 'A'));
        if (a != b) return false;
    }
    return true;
}

bool detailedCandlesNeedInstrumentMetadata(const CaptureConfig& config) noexcept {
    return (textEqualsAscii(config.exchange, "finam") || textEqualsAscii(config.exchange, "finam_arena"))
        && !textEqualsAscii(config.market, "spot")
        && !textEqualsAscii(config.market, "shares");
}

std::int64_t candleTierFromTimeframe(std::string_view timeframe) noexcept {
    if (timeframe == "1m") return 1;
    if (timeframe == "10m" || timeframe == "15m") return 2;
    if (timeframe == "1d") return 3;
    return 0;
}

std::string sanitizedTimeframeSuffix(std::string_view timeframe) {
    std::string out;
    out.reserve(timeframe.size());
    for (char c : timeframe) {
        if ((c >= '0' && c <= '9') ||
            (c >= 'a' && c <= 'z') ||
            (c >= 'A' && c <= 'Z')) {
            out.push_back(c);
        }
    }
    return out.empty() ? std::string{"unknown"} : out;
}

std::string detailedCandlesRelativePath(std::string_view timeframe, bool detailed) {
    const auto suffix = sanitizedTimeframeSuffix(timeframe);
    return std::string{"jsonl/"} + (detailed ? "candles2_" : "candles_") + suffix + ".jsonl";
}

bool validDetailedCandle(const cxet::composite::Ohlcv& candle) noexcept {
    return candle.ts.raw > 0 &&
           candle.open.raw > 0 &&
           candle.high.raw > 0 &&
           candle.low.raw > 0 &&
           candle.close.raw > 0 &&
           candle.high.raw >= candle.low.raw;
}

Status detailedCandlesFetchStatus(std::string_view errorText) noexcept {
    if (errorText == "candles2: missing symbol" ||
        errorText == "candles2: invalid symbol" ||
        errorText == "candles2: invalid timeframe") {
        return Status::InvalidArgument;
    }
    return Status::Unknown;
}

void pushUnique(std::vector<std::string>& values, const std::string& value) {
    if (value.empty()) return;
    if (std::find(values.begin(), values.end(), value) == values.end()) values.push_back(value);
}

replay::CandleRow makeDetailedCandleRow(const CaptureConfig& config,
                                        const cxet::composite::Ohlcv& candle,
                                        std::string_view timeframe,
                                        std::int64_t candleTier) {
    replay::CandleRow row{};
    row.tier = candleTier;
    row.exchange = config.exchange;
    row.market = config.market;
    row.symbol = std::string{internal::primaryIdentitySymbolText(config)};
    row.timeframe = std::string{timeframe};
    row.tsNs = static_cast<std::int64_t>(candle.ts.raw);
    row.openE8 = static_cast<std::int64_t>(candle.open.raw);
    row.highE8 = static_cast<std::int64_t>(candle.high.raw);
    row.lowE8 = static_cast<std::int64_t>(candle.low.raw);
    row.closeE8 = static_cast<std::int64_t>(candle.close.raw);
    row.volumeE8 = static_cast<std::int64_t>(candle.amount.raw);
    row.quoteAmountE8 = static_cast<std::int64_t>(candle.quoteAmount.raw);
    row.hasOhlc = true;
    return row;
}

replay::CandleRow makeLegacyCandleRow(const replay::CandleRow& row) noexcept {
    replay::CandleRow lite{};
    lite.tier = row.tier;
    lite.tsNs = row.tsNs;
    lite.highE8 = row.highE8;
    lite.lowE8 = row.lowE8;
    lite.quoteAmountE8 = row.quoteAmountE8;
    return lite;
}

struct BulkWriteContext {
    ChannelJsonWriter* candles2Writer{nullptr};
    ChannelJsonWriter* candlesWriter{nullptr};
    std::atomic<std::uint64_t>* candles2Count{nullptr};
    std::atomic<std::uint64_t>* candlesCount{nullptr};
    std::filesystem::path sessionDir{};
    const CaptureConfig* config{nullptr};
    Candles2BulkState* state{nullptr};
    std::string timeframe{};
    std::int64_t candleTier{0};
    bool writeLegacyCandles{false};
    Status status{Status::Ok};
    std::string error{};
    std::uint64_t legacyWritten{0u};
};

bool appendBulkCandlesPage(const hft_trader::runtime::candles::OhlcvHistoryPage& page,
                           void* userData) noexcept {
    auto* context = static_cast<BulkWriteContext*>(userData);
    if (!context || !context->candles2Writer || !context->candles2Count ||
        !context->config || !context->state || context->sessionDir.empty()) {
        return false;
    }

    std::uint64_t writtenThisPage = 0u;
    for (std::size_t i = 0u; i < page.count; ++i) {
        const auto& candle = page.rows[i];
        if (!validDetailedCandle(candle)) continue;
        replay::CandleRow row = makeDetailedCandleRow(*context->config, candle, context->timeframe, context->candleTier);
        if (row.volumeE8 < 0 || row.quoteAmountE8 < 0) continue;

        const auto writeStatus = context->candles2Writer->writeLineBuffered(renderCandleJsonLine(row));
        if (!isOk(writeStatus)) {
            context->status = writeStatus;
            context->error = "candles2_bulk: failed to write candles2 jsonl";
            return false;
        }
        context->candles2Count->fetch_add(1, std::memory_order_acq_rel);
        ++context->state->rowsWritten;
        ++writtenThisPage;

        if (context->writeLegacyCandles) {
            if (!context->candlesWriter || !context->candlesCount) {
                context->status = Status::InvalidArgument;
                context->error = "candles2_bulk: missing compatibility candles writer";
                return false;
            }
            const auto legacyStatus = context->candlesWriter->writeLineBuffered(
                renderCandleJsonLine(makeLegacyCandleRow(row)));
            if (!isOk(legacyStatus)) {
                context->status = legacyStatus;
                context->error = "candles2_bulk: failed to write compatibility candles jsonl";
                return false;
            }
            context->candlesCount->fetch_add(1, std::memory_order_acq_rel);
            ++context->legacyWritten;
        }
    }

    if (writtenThisPage != 0u) {
        const auto flushStatus = context->candles2Writer->flush();
        if (!isOk(flushStatus)) {
            context->status = flushStatus;
            context->error = "candles2_bulk: failed to flush candles2 jsonl";
            return false;
        }
        if (context->writeLegacyCandles) {
            const auto legacyFlushStatus = context->candlesWriter->flush();
            if (!isOk(legacyFlushStatus)) {
                context->status = legacyFlushStatus;
                context->error = "candles2_bulk: failed to flush compatibility candles jsonl";
                return false;
            }
        }
    }

    ++context->state->pagesOk;
    context->state->rowsRaw += static_cast<std::uint64_t>(page.count);
    context->state->cursorEndNs = page.nextCursorEndNs;
    if (page.oldestTsNs != 0u &&
        (context->state->oldestTsNs == 0u || page.oldestTsNs < context->state->oldestTsNs)) {
        context->state->oldestTsNs = page.oldestTsNs;
    }
    if (page.newestTsNs != 0u &&
        (context->state->newestTsNs == 0u || page.newestTsNs > context->state->newestTsNs)) {
        context->state->newestTsNs = page.newestTsNs;
    }
    context->state->status = "running";

    std::string stateError;
    const auto stateStatus = writeCandles2BulkStateFile(context->sessionDir, *context->state, &stateError);
    if (!isOk(stateStatus)) {
        context->status = stateStatus;
        context->error = stateError.empty() ? "candles2_bulk: failed to write state" : stateError;
        return false;
    }
    return true;
}

}  // namespace

Status CaptureCoordinator::captureDetailedCandlesBulk(const CaptureConfig& config) noexcept {
    const auto sessionStatus = ensureSession(config);
    if (!isOk(sessionStatus)) return sessionStatus;
    if (detailedCandlesNeedInstrumentMetadata(config) && !instrumentMetadataReady_) {
        const auto metadataStatus = refreshInstrumentMetadataFromExchangeInfo();
        if (!isOk(metadataStatus)) return metadataStatus;
    }
    if (detailedCandlesNeedInstrumentMetadata(config) && !instrumentMetadataReady_) {
        const std::string metadataDetail = lastError_;
        lastError_ = "candles2_bulk: finam/finam_arena futures instrument metadata is required for price-basis logic";
        if (!metadataDetail.empty()) {
            lastError_ += " metadata=";
            lastError_ += metadataDetail;
        }
        return Status::Unknown;
    }
    if (internal::primaryRouteSymbolText(config).empty()) {
        lastError_ = "candles2: missing symbol";
        return Status::InvalidArgument;
    }

    Symbol symbol{};
    copySymbolFromText(symbol, internal::primaryRouteSymbolText(config));
    if (symbol.data[0] == '\0') {
        lastError_ = "candles2: invalid symbol";
        return Status::InvalidArgument;
    }

    TimeframeBuf timeframe{};
    const std::string tfText = config.detailedCandlesTimeframe.empty() ? std::string{"15m"} : config.detailedCandlesTimeframe;
    timeframe.copyFrom(tfText.c_str());
    if (timeframe.data[0] == '\0') {
        lastError_ = "candles2: invalid timeframe";
        return Status::InvalidArgument;
    }

    CountVal limit{};
    const std::uint32_t requestedLimit = config.detailedCandlesLimit == 0u
        ? kDetailedCandlesDefaultLimit
        : config.detailedCandlesLimit;
    limit.raw = std::min<std::uint32_t>(requestedLimit, kDetailedCandlesMaxLimit);

    TimeNs endTime{};
    endTime.raw = config.detailedCandlesEndNs > 0
        ? static_cast<std::uint64_t>(config.detailedCandlesEndNs)
        : static_cast<std::uint64_t>(internal::nowNs());

    const std::string candles2Path = detailedCandlesRelativePath(tfText, true);
    const std::string candlesPath = detailedCandlesRelativePath(tfText, false);
    candles2Writer_.close();
    candlesWriter_.close();
    if (!isOk(candles2Writer_.openRelativePath(sessionDir_, candles2Path, false))) {
        lastError_ = "candles2_bulk: failed to create candles2 jsonl";
        return Status::IoError;
    }

    const std::int64_t candleTier = candleTierFromTimeframe(tfText);
    const bool writeLegacyCandles = candleTier >= 1 && candleTier <= 3;
    if (writeLegacyCandles && !isOk(candlesWriter_.openRelativePath(sessionDir_, candlesPath, false))) {
        lastError_ = "candles2_bulk: failed to create compatibility candles jsonl";
        return Status::IoError;
    }

    Candles2BulkState state{};
    state.status = "running";
    state.exchange = config.exchange;
    state.market = config.market;
    state.symbol = std::string{internal::primaryIdentitySymbolText(config)};
    state.timeframe = tfText;
    state.candles2Path = candles2Path;
    state.compatibilityCandlesPath = writeLegacyCandles ? candlesPath : std::string{};
    state.requestedLimit = limit.raw;
    state.requestedEndNs = static_cast<std::int64_t>(endTime.raw);
    state.cursorEndNs = endTime.raw;
    std::string stateError;
    if (!isOk(writeCandles2BulkStateFile(sessionDir_, state, &stateError))) {
        lastError_ = stateError.empty() ? "candles2_bulk: failed to write initial state" : stateError;
        return Status::IoError;
    }

    MessageBuffer requestBuf{};
    MessageBuffer recvBuf{};
    std::string fetchFailure;
    hft_trader::runtime::candles::OhlcvHistoryStreamStats streamStats{};
    hft_trader::runtime::candles::OhlcvHistoryStreamOptions options{};
    options.pageLimitOverride = config.detailedCandlesPageLimit;
    options.maxAttemptsPerPage = config.detailedCandlesMaxAttemptsPerPage;
    options.maxEmptyDateRangePages = config.detailedCandlesMaxEmptyWindows;

    BulkWriteContext context{};
    context.candles2Writer = &candles2Writer_;
    context.candlesWriter = &candlesWriter_;
    context.candles2Count = &candles2Count_;
    context.candlesCount = &candlesCount_;
    context.sessionDir = sessionDir_;
    context.config = &config;
    context.state = &state;
    context.timeframe = tfText;
    context.candleTier = candleTier;
    context.writeLegacyCandles = writeLegacyCandles;

    const bool fetched = hft_trader::runtime::candles::streamOhlcvHistoryForVenue(
        internal::makeTraderVenueConfig(config),
        symbol,
        timeframe,
        limit,
        endTime,
        appendBulkCandlesPage,
        &context,
        requestBuf,
        recvBuf,
        &streamStats,
        &fetchFailure,
        options);
    if (const auto authPersistStatus = internal::persistFinamAuthForConfig(config, lastError_);
        !isOk(authPersistStatus)) {
        state.status = "failed";
        state.lastError = lastError_;
        (void)writeCandles2BulkStateFile(sessionDir_, state, nullptr);
        return authPersistStatus;
    }

    state.pageLimit = streamStats.pageLimit;
    state.emptyWindowsSkipped = streamStats.emptyWindowsSkipped;
    state.callbackStops = streamStats.callbackStops;
    state.cursorEndNs = streamStats.cursorEndNs == 0u ? state.cursorEndNs : streamStats.cursorEndNs;
    if (streamStats.oldestTsNs != 0u) state.oldestTsNs = streamStats.oldestTsNs;
    if (streamStats.newestTsNs != 0u) state.newestTsNs = streamStats.newestTsNs;
    if (streamStats.rowsRaw != 0u) state.rowsRaw = streamStats.rowsRaw;
    if (streamStats.pagesOk != 0u) state.pagesOk = streamStats.pagesOk;
    if (!context.error.empty()) {
        state.status = "failed";
        state.lastError = context.error;
        (void)writeCandles2BulkStateFile(sessionDir_, state, nullptr);
        lastError_ = context.error;
        return context.status;
    }
    if (!fetched && state.rowsWritten == 0u) {
        lastError_ = "candles2_bulk: trader OHLCV fetch failed exchange=" + config.exchange +
                     " market=" + config.market +
                     " symbol=" + (config.symbols.empty() ? std::string{} : config.symbols.front()) +
                     " timeframe=" + tfText;
        if (!fetchFailure.empty()) {
            lastError_ += " reason=";
            lastError_ += fetchFailure;
        }
        lastError_ += " response_bytes=" + std::to_string(recvBuf.size());
        state.status = "failed";
        state.lastError = lastError_;
        (void)writeCandles2BulkStateFile(sessionDir_, state, nullptr);
        return detailedCandlesFetchStatus(lastError_);
    }

    state.lastError = fetchFailure;
    if (state.rowsWritten >= state.requestedLimit) {
        state.status = "complete";
    } else if (state.rowsRaw >= state.requestedLimit) {
        state.status = "complete_filtered";
    } else if (streamStats.stoppedOnFailure || !fetchFailure.empty()) {
        state.status = "partial";
    } else {
        state.status = "exhausted";
    }
    (void)writeCandles2BulkStateFile(sessionDir_, state, nullptr);

    if (state.rowsWritten == 0u) {
        lastError_ = "candles2_bulk: fetch returned no valid OHLCV rows exchange=" + config.exchange +
                     " market=" + config.market +
                     " symbol=" + (config.symbols.empty() ? std::string{} : config.symbols.front()) +
                     " timeframe=" + tfText +
                     " parsed_rows=" + std::to_string(streamStats.rowsEmitted);
        return Status::Unknown;
    }

    if (state.status == "complete" && lastError_.starts_with("candles2")) lastError_.clear();
    manifest_.candles2Enabled = true;
    manifest_.candles2Path = candles2Path;
    manifest_.candles2Count = candles2Count_.load(std::memory_order_relaxed);
    pushUnique(manifest_.canonicalArtifacts, manifest_.candles2Path);
    pushUnique(manifest_.supportArtifacts, kCandles2BulkStateRelativePath);
    if (writeLegacyCandles && context.legacyWritten != 0u) {
        manifest_.candlesEnabled = true;
        manifest_.candlesPath = candlesPath;
        manifest_.candlesCount = candlesCount_.load(std::memory_order_relaxed);
        pushUnique(manifest_.canonicalArtifacts, manifest_.candlesPath);
    }
    if (state.status != "complete") {
        std::string warning = "candles2_bulk " + state.status +
                              " rows=" + std::to_string(state.rowsWritten) +
                              "/" + std::to_string(state.requestedLimit);
        if (!state.lastError.empty()) {
            warning += " error=";
            warning += state.lastError;
        }
        manifest_.warningSummary = warning;
        lastError_ = std::move(warning);
    }
    return writeManifestFile_();
}

}  // namespace hftrec::capture
