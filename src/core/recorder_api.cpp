#include "hftrec/recorder_api.hpp"

#include <fstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

#include "core/capture/SessionManifest.hpp"
#include "core/replay/SessionReplay.hpp"

namespace hftrec {
namespace {

bool wants(RecorderChannelMask channels, RecorderChannelMask channel) noexcept {
    return (channels & channel) != 0u;
}

bool fileExists(const std::filesystem::path& path) noexcept {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

bool directoryExists(const std::filesystem::path& path) noexcept {
    std::error_code ec;
    return std::filesystem::is_directory(path, ec);
}

bool readWholeFile(const std::filesystem::path& path, std::string& out) noexcept {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) return false;
    out.resize(static_cast<std::size_t>(size));
    if (out.empty()) return true;
    in.read(out.data(), static_cast<std::streamsize>(out.size()));
    return in.good() || in.eof();
}

Status loadManifest(const std::filesystem::path& sessionPath,
                    capture::SessionManifest& manifest,
                    std::string& error) noexcept {
    const auto manifestPath = sessionPath / "manifest.json";
    if (!fileExists(manifestPath)) {
        error = "current captured-arrival manifest.json is required";
        return Status::CorruptData;
    }

    std::string document;
    if (!readWholeFile(manifestPath, document)) {
        error = "failed to read manifest.json";
        return Status::IoError;
    }

    const Status status = capture::parseManifestJson(document, manifest);
    if (!isOk(status)) {
        error = "failed to parse manifest.json";
        return status;
    }
    if (!manifest.structurallyLoadable ||
        manifest.manifestSchemaVersion != capture::kManifestSchemaVersionCurrent ||
        manifest.corpusSchemaVersion != capture::kCorpusSchemaVersionCurrent ||
        manifest.captureContractVersion != capture::kCaptureContractVersionCurrent) {
        error = "manifest.json does not declare the current captured-arrival contract";
        return Status::CorruptData;
    }
    return Status::Ok;
}

std::filesystem::path declaredChannelPath(
    const std::filesystem::path& sessionPath,
    const std::string& manifestPath) {
    const std::filesystem::path relative{manifestPath};
    if (relative.empty() || relative.is_absolute()) return {};
    for (const auto& component : relative) {
        if (component == "..") return {};
    }
    return sessionPath / relative;
}

Status openSelectedReplay(const std::filesystem::path& sessionPath,
                          RecorderChannelMask channels,
                          replay::SessionReplay& replay,
                          std::string& error) noexcept {
    replay.reset();
    if (sessionPath.empty()) {
        error = "session path is empty";
        return Status::InvalidArgument;
    }
    if (!directoryExists(sessionPath)) {
        error = "session directory does not exist";
        return Status::InvalidArgument;
    }

    capture::SessionManifest manifest{};
    const Status manifestStatus = loadManifest(sessionPath, manifest, error);
    if (!isOk(manifestStatus)) return manifestStatus;

    const auto addChannel = [&](RecorderChannelMask channel,
                                bool enabled,
                                std::string_view label,
                                const std::filesystem::path& path,
                                auto addFile) noexcept -> Status {
        if (!wants(channels, channel) || !enabled) return Status::Ok;
        if (path.empty() || !fileExists(path)) {
            error = "manifest-declared ";
            error += label;
            error += " artifact is missing or unsafe";
            return Status::CorruptData;
        }
        return (replay.*addFile)(path, 0u);
    };

    Status status = addChannel(RecorderChannel_Trades,
                               manifest.tradesEnabled,
                               "trades",
                               declaredChannelPath(sessionPath, manifest.tradesPath),
                               &replay::SessionReplay::addTradesFile);
    if (!isOk(status)) return status;

    status = addChannel(RecorderChannel_Liquidations,
                        manifest.liquidationsEnabled,
                        "liquidations",
                        declaredChannelPath(sessionPath, manifest.liquidationsPath),
                        &replay::SessionReplay::addLiquidationsFile);
    if (!isOk(status)) return status;

    status = addChannel(RecorderChannel_BookTicker,
                        manifest.bookTickerEnabled,
                        "bookticker",
                        declaredChannelPath(sessionPath, manifest.bookTickerPath),
                        &replay::SessionReplay::addBookTickerFile);
    if (!isOk(status)) return status;

    if (wants(channels, RecorderChannel_Candles)) {
        const auto candlesPath = declaredChannelPath(sessionPath, manifest.candlesPath);
        const auto candles2Path = declaredChannelPath(sessionPath, manifest.candles2Path);
        if (manifest.candlesEnabled) {
            if (candlesPath.empty() || !fileExists(candlesPath)) {
                error = "manifest-declared candles artifact is missing or unsafe";
                return Status::CorruptData;
            }
            status = replay.addCandlesFile(candlesPath);
            if (!isOk(status)) return status;
        }
        if (manifest.candles2Enabled) {
            if (candles2Path.empty() || !fileExists(candles2Path)) {
                error = "manifest-declared candles2 artifact is missing or unsafe";
                return Status::CorruptData;
            }
            status = replay.addCandles2File(candles2Path);
            if (!isOk(status)) return status;
        }
    }

    status = addChannel(RecorderChannel_Depth,
                        manifest.orderbookEnabled,
                        "depth",
                        declaredChannelPath(sessionPath, manifest.depthPath),
                        &replay::SessionReplay::addDepthFile);
    if (!isOk(status)) return status;

    const auto countMatches = [&](RecorderChannelMask channel,
                                  bool enabled,
                                  std::uint64_t declared,
                                  std::size_t actual,
                                  std::string_view label) noexcept {
        if (!wants(channels, channel) || !enabled ||
            declared == static_cast<std::uint64_t>(actual)) return true;
        error = "manifest declared_event_count mismatch for ";
        error += label;
        return false;
    };
    if (!countMatches(RecorderChannel_Trades, manifest.tradesEnabled,
                      manifest.tradesCount, replay.trades().size(), "trades") ||
        !countMatches(RecorderChannel_Liquidations, manifest.liquidationsEnabled,
                      manifest.liquidationsCount, replay.liquidations().size(), "liquidations") ||
        !countMatches(RecorderChannel_BookTicker, manifest.bookTickerEnabled,
                      manifest.bookTickerCount, replay.bookTickers().size(), "bookticker") ||
        !countMatches(RecorderChannel_Depth, manifest.orderbookEnabled,
                      manifest.depthCount, replay.depths().size(), "depth")) {
        return Status::CorruptData;
    }
    if (wants(channels, RecorderChannel_Candles) &&
        ((manifest.candlesEnabled &&
          manifest.candlesCount != static_cast<std::uint64_t>(replay.candles().size())) ||
         (manifest.candles2Enabled &&
          manifest.candles2Count != static_cast<std::uint64_t>(replay.candles2().size())))) {
        error = "manifest declared_event_count mismatch for candles";
        return Status::CorruptData;
    }

    replay.finalize();
    return replay.status();
}

RecorderPriceLevel convert(const replay::PricePair& row) {
    return RecorderPriceLevel{row.priceE8, row.qtyE8, row.side};
}

std::vector<RecorderPriceLevel> convertLevels(const std::vector<replay::PricePair>& rows) {
    std::vector<RecorderPriceLevel> out;
    out.reserve(rows.size());
    for (const auto& row : rows) out.push_back(convert(row));
    return out;
}

RecorderTradeRow convert(const replay::TradeRow& row) {
    RecorderTradeRow out{};
    out.tradeId = row.tradeId;
    out.firstTradeId = row.firstTradeId;
    out.lastTradeId = row.lastTradeId;
    out.symbol = row.symbol;
    out.exchange = row.exchange;
    out.market = row.market;
    out.tsNs = row.tsNs;
    out.captureSeq = row.captureSeq;
    out.ingestSeq = row.ingestSeq;
    out.priceE8 = row.priceE8;
    out.qtyE8 = row.qtyE8;
    out.quoteQtyE8 = row.quoteQtyE8;
    out.side = row.side;
    out.isBuyerMaker = row.isBuyerMaker;
    out.sideBuy = row.sideBuy;
    return out;
}

RecorderLiquidationRow convert(const replay::LiquidationRow& row) {
    RecorderLiquidationRow out{};
    out.symbol = row.symbol;
    out.exchange = row.exchange;
    out.market = row.market;
    out.tsNs = row.tsNs;
    out.captureSeq = row.captureSeq;
    out.ingestSeq = row.ingestSeq;
    out.priceE8 = row.priceE8;
    out.qtyE8 = row.qtyE8;
    out.avgPriceE8 = row.avgPriceE8;
    out.filledQtyE8 = row.filledQtyE8;
    out.side = row.side;
    out.sideBuy = row.sideBuy;
    out.orderType = row.orderType;
    out.timeInForce = row.timeInForce;
    out.status = row.status;
    out.sourceMode = row.sourceMode;
    return out;
}

RecorderBookTickerRow convert(const replay::BookTickerRow& row) {
    RecorderBookTickerRow out{};
    out.eventId = row.eventId;
    out.symbol = row.symbol;
    out.exchange = row.exchange;
    out.market = row.market;
    out.tsNs = row.tsNs;
    out.captureSeq = row.captureSeq;
    out.ingestSeq = row.ingestSeq;
    out.bidPriceE8 = row.bidPriceE8;
    out.bidQtyE8 = row.bidQtyE8;
    out.askPriceE8 = row.askPriceE8;
    out.askQtyE8 = row.askQtyE8;
    return out;
}

RecorderCandleRow convert(const replay::CandleRow& row) {
    RecorderCandleRow out{};
    out.tier = row.tier;
    out.tsNs = row.tsNs;
    out.exchange = row.exchange;
    out.market = row.market;
    out.symbol = row.symbol;
    out.timeframe = row.timeframe;
    out.durationNs = row.durationNs;
    out.openE8 = row.openE8;
    out.highE8 = row.highE8;
    out.lowE8 = row.lowE8;
    out.closeE8 = row.closeE8;
    out.volumeE8 = row.volumeE8;
    out.quoteAmountE8 = row.quoteAmountE8;
    out.hasOhlc = row.hasOhlc;
    return out;
}

RecorderDepthRow convert(const replay::DepthRow& row) {
    RecorderDepthRow out{};
    out.eventId = row.eventId;
    out.tsNs = row.tsNs;
    out.levels = convertLevels(row.levels);
    return out;
}

RecorderSnapshotDocument convert(const replay::SnapshotDocument& row) {
    RecorderSnapshotDocument out{};
    out.tsNs = row.tsNs;
    out.levels = convertLevels(row.levels);
    return out;
}

RecorderEventKind convert(replay::SessionReplay::EventKind kind) noexcept {
    switch (kind) {
        case replay::SessionReplay::EventKind::Depth: return RecorderEventKind::Depth;
        case replay::SessionReplay::EventKind::Trade: return RecorderEventKind::Trade;
        case replay::SessionReplay::EventKind::Liquidation: return RecorderEventKind::Liquidation;
        case replay::SessionReplay::EventKind::BookTicker: return RecorderEventKind::BookTicker;
        case replay::SessionReplay::EventKind::MarkPrice:
        case replay::SessionReplay::EventKind::IndexPrice:
        case replay::SessionReplay::EventKind::Funding:
        case replay::SessionReplay::EventKind::PriceLimit:
            return RecorderEventKind::Depth;
    }
    return RecorderEventKind::Depth;
}

bool eventWanted(replay::SessionReplay::EventKind kind, RecorderChannelMask channels) noexcept {
    switch (kind) {
        case replay::SessionReplay::EventKind::Depth: return wants(channels, RecorderChannel_Depth);
        case replay::SessionReplay::EventKind::Trade: return wants(channels, RecorderChannel_Trades);
        case replay::SessionReplay::EventKind::Liquidation: return wants(channels, RecorderChannel_Liquidations);
        case replay::SessionReplay::EventKind::BookTicker: return wants(channels, RecorderChannel_BookTicker);
        case replay::SessionReplay::EventKind::MarkPrice:
        case replay::SessionReplay::EventKind::IndexPrice:
        case replay::SessionReplay::EventKind::Funding:
        case replay::SessionReplay::EventKind::PriceLimit:
            return false;
    }
    return false;
}

void fillSummary(RecorderSession& out, const replay::SessionReplay& replay, Status status) {
    out.info.status = status;
    out.info.trades = out.tradeRows.size();
    out.info.liquidations = out.liquidationRows.size();
    out.info.bookTickers = out.bookTickerRows.size();
    out.info.depths = out.depthRows.size();
    out.info.candles = out.candleRows.size();
    out.info.snapshots = out.hasSnapshot ? 1u : 0u;
    out.info.timelineEvents = out.timelineRows.size();
    out.info.buckets = replay.buckets().size();
    out.info.firstTsNs = replay.firstTsNs();
    out.info.lastTsNs = replay.lastTsNs();
    out.info.error = std::string{replay.errorDetail()};
}

}  // namespace

void RecorderSession::clear() noexcept {
    info = RecorderSessionSummary{};
    tradeRows.clear();
    liquidationRows.clear();
    bookTickerRows.clear();
    depthRows.clear();
    candleRows.clear();
    snapshot = RecorderSnapshotDocument{};
    hasSnapshot = false;
    timelineRows.clear();
}

void RecorderSessionSet::clear() noexcept {
    primarySession.clear();
    secondarySession.clear();
    secondaryPresent = false;
}

Status loadRecorderSession(const std::filesystem::path& sessionPath,
                           RecorderChannelMask channels,
                           bool buildTimeline,
                           RecorderSession& out) noexcept {
    out.clear();
    out.info.sessionPath = sessionPath;
    if (sessionPath.empty()) {
        out.info.status = Status::InvalidArgument;
        out.info.error = "session path is empty";
        return out.info.status;
    }

    replay::SessionReplay replay;
    std::string loadError;
    const Status status = openSelectedReplay(sessionPath, channels, replay, loadError);
    if (!isOk(status)) {
        fillSummary(out, replay, status);
        if (!loadError.empty()) out.info.error = std::move(loadError);
        if (out.info.error.empty()) out.info.error = "failed to load recorder session";
        return status;
    }

    if (wants(channels, RecorderChannel_Trades)) {
        out.tradeRows.reserve(replay.trades().size());
        for (const auto& row : replay.trades()) out.tradeRows.push_back(convert(row));
    }
    if (wants(channels, RecorderChannel_Liquidations)) {
        out.liquidationRows.reserve(replay.liquidations().size());
        for (const auto& row : replay.liquidations()) out.liquidationRows.push_back(convert(row));
    }
    if (wants(channels, RecorderChannel_BookTicker)) {
        out.bookTickerRows.reserve(replay.bookTickers().size());
        for (const auto& row : replay.bookTickers()) out.bookTickerRows.push_back(convert(row));
    }
    if (wants(channels, RecorderChannel_Depth)) {
        out.depthRows.reserve(replay.depths().size());
        for (const auto& row : replay.depths()) out.depthRows.push_back(convert(row));
    }
    if (wants(channels, RecorderChannel_Candles)) {
        out.candleRows.reserve(replay.candles().size() + replay.candles2().size());
        for (const auto& row : replay.candles()) out.candleRows.push_back(convert(row));
        for (const auto& row : replay.candles2()) out.candleRows.push_back(convert(row));
    }
    if (wants(channels, RecorderChannel_Snapshot) && replay.hasSnapshot()) {
        out.snapshot = convert(replay.snapshot());
        out.hasSnapshot = true;
    }
    if (buildTimeline) {
        out.timelineRows.reserve(replay.events().size());
        for (const auto& event : replay.events()) {
            if (!eventWanted(event.kind, channels)) continue;
            out.timelineRows.push_back(RecorderTimelineEvent{
                event.tsNs,
                event.ingestSeq,
                event.rowIndex,
                convert(event.kind),
            });
        }
    }

    fillSummary(out, replay, Status::Ok);
    return Status::Ok;
}

Status loadRecorderSessions(const RecorderLoadRequest& request, RecorderSessionSet& out) noexcept {
    out.clear();
    const Status primaryStatus = loadRecorderSession(request.primarySessionPath,
                                                     request.channels,
                                                     request.buildTimeline,
                                                     out.primarySession);
    if (!isOk(primaryStatus)) return primaryStatus;

    if (!request.secondarySessionPath.empty()) {
        out.secondaryPresent = true;
        const Status secondaryStatus = loadRecorderSession(request.secondarySessionPath,
                                                           request.channels,
                                                           request.buildTimeline,
                                                           out.secondarySession);
        if (!isOk(secondaryStatus)) return secondaryStatus;
    }
    return Status::Ok;
}

}  // namespace hftrec
