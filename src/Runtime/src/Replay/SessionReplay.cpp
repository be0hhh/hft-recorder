#include "SessionReplay.hpp"

#include <algorithm>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

#include "../Capture/Session/SessionManifest.hpp"
#include "../Common/JsonString.hpp"
#include "../Corpus/CorpusLoader.hpp"
#include "../Corpus/SessionCorpus.hpp"
#include "JsonLineParser.hpp"

namespace hftrec::replay {

namespace {

Status parseFinamCandleArchiveLine(std::string_view line, CandleRow& row) noexcept {
    const Status status = parseCandleLine(line, row);
    return isOk(status) && row.exchange == "finam" && isHistoricalBackfill(row.arrival)
        ? Status::Ok : Status::CorruptData;
}


Status parseTradeCanonicalLine(std::string_view line, TradeRow& row) noexcept {
    return parseTradeLine(line, row);
}

Status parseBookTickerCanonicalLine(std::string_view line, BookTickerRow& row) noexcept {
    return parseBookTickerLine(line, row);
}

bool sameLevelKey(const PricePair& lhs, const PricePair& rhs) noexcept {
    return lhs.priceE8 == rhs.priceE8 && lhs.side == rhs.side;
}

bool containsLevelKey(const std::vector<PricePair>& levels, const PricePair& needle) noexcept {
    return std::find_if(levels.begin(), levels.end(), [&](const PricePair& level) noexcept {
        return sameLevelKey(level, needle);
    }) != levels.end();
}

bool hasDeleteLevel(const std::vector<DepthRow>& rows) noexcept {
    for (const auto& row : rows) {
        for (const auto& level : row.levels) {
            if (level.qtyE8 == 0) return true;
        }
    }
    return false;
}

void normalizeFixedDepthSnapshotDeltas(std::vector<DepthRow>& rows) {
    if (hasDeleteLevel(rows)) return;
    std::vector<PricePair> previousLevels;
    for (auto& row : rows) {
        std::vector<PricePair> currentLevels;
        currentLevels.reserve(row.levels.size());
        for (const auto& level : row.levels) {
            if (level.qtyE8 > 0) currentLevels.push_back(level);
        }
        for (const auto& previous : previousLevels) {
            if (!containsLevelKey(currentLevels, previous)) {
                row.levels.push_back(PricePair{previous.priceE8, 0, previous.side});
            }
        }
        previousLevels = std::move(currentLevels);
    }
}

void applyLoadIssueToIntegritySummary(const hftrec::corpus::LoadIssue& issue,
                                      SessionIntegritySummary& summary) {
    auto mark = [&](ChannelIntegritySummary& channelSummary, IntegrityChannel channel) {
        channelSummary.state = ChannelHealthState::Corrupt;
        channelSummary.exactReplayEligible = false;
        ++channelSummary.incidentCount;
        if (issue.code == hftrec::corpus::LoadIssueCode::InvalidJsonLine
            || issue.code == hftrec::corpus::LoadIssueCode::InvalidJsonDocument
            || issue.code == hftrec::corpus::LoadIssueCode::InvalidManifest) {
            ++channelSummary.parseErrorCount;
        }
        channelSummary.highestSeverity = IntegritySeverity::Error;
        channelSummary.reasonCode = std::string{hftrec::corpus::toString(issue.code)};
        channelSummary.reasonText = issue.detail;
        summary.incidents.push_back(IntegrityIncident{
            channel,
            IntegrityIncidentKind::ParseError,
            IntegritySeverity::Error,
            channelSummary.reasonCode,
            issue.detail,
            0,
            issue.lineOrRow,
            {},
            issue.artifact,
            true
        });
        ++summary.totalIncidents;
        summary.highestSeverity = IntegritySeverity::Error;
        summary.sessionHealth = SessionHealth::Corrupt;
        summary.exactReplayEligible = false;
    };

    if (issue.channel == "trades") mark(summary.trades, IntegrityChannel::Trades);
    else if (issue.channel == "liquidations") mark(summary.liquidations, IntegrityChannel::Liquidations);
    else if (issue.channel == "bookticker") mark(summary.bookTicker, IntegrityChannel::BookTicker);
    else if (issue.channel == "depth") mark(summary.depth, IntegrityChannel::Depth);
    else if (issue.channel == "snapshot") mark(summary.snapshot, IntegrityChannel::Snapshot);
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

bool regularFileExists(const std::filesystem::path& path) noexcept {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec) && !ec;
}

void sortCandles(std::vector<CandleRow>& rows) {
    std::stable_sort(rows.begin(), rows.end(), [](const CandleRow& lhs, const CandleRow& rhs) noexcept {
        if (lhs.tsNs != rhs.tsNs) return lhs.tsNs < rhs.tsNs;
        return lhs.tier < rhs.tier;
    });
}

template <typename RowT, typename Parser>
Status loadJsonl(const std::filesystem::path& path,
                 std::vector<RowT>& out,
                 std::string& errorDetail,
                 Parser&& parse,
                 std::size_t& lineNumberOut,
                 std::size_t reserveHint = 0) noexcept {
    std::ifstream in(path, std::ios::binary);
    if (!in) return Status::Ok;
    if (reserveHint > 0u) out.reserve(out.size() + reserveHint);
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(in, line)) {
        ++lineNumber;
        if (line.empty()) continue;
        if (line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        RowT row{};
        const auto st = parse(std::string_view{line}, row);
        if (!isOk(st)) {
            errorDetail = "failed to parse " + path.filename().string()
                + " line " + std::to_string(lineNumber)
                + ": " + std::string{statusToString(st)};
            lineNumberOut = lineNumber;
            return st;
        }
        out.push_back(std::move(row));
    }
    lineNumberOut = lineNumber;
    return Status::Ok;
}

Status loadDepthTapeSidecarJsonl(const std::filesystem::path& tapePath,
                                 const std::filesystem::path& sidecarPath,
                                 std::vector<DepthRow>& out,
                                 std::string& errorDetail,
                                 std::size_t& lineNumberOut,
                                 bool allowPartial,
                                 std::size_t reserveHint = 0) noexcept {
    const bool tapeExists = regularFileExists(tapePath);
    const bool sidecarExists = regularFileExists(sidecarPath);
    if (!tapeExists && !sidecarExists) return Status::Ok;
    if (!tapeExists || !sidecarExists) {
        errorDetail = "depth_tape/depth_sidecar package is incomplete";
        lineNumberOut = 0;
        return Status::CorruptData;
    }

    std::ifstream tape(tapePath, std::ios::binary);
    std::ifstream sidecar(sidecarPath, std::ios::binary);
    if (!tape || !sidecar) {
        errorDetail = "failed to open depth_tape/depth_sidecar package";
        lineNumberOut = 0;
        return Status::IoError;
    }
    std::string tapeLine;
    std::string sidecarLine;
    std::size_t lineNumber = 0;
    const std::size_t initialSize = out.size();
    if (reserveHint > 0u) out.reserve(out.size() + reserveHint);
    while (true) {
        const bool haveTape = static_cast<bool>(std::getline(tape, tapeLine));
        const bool haveSidecar = static_cast<bool>(std::getline(sidecar, sidecarLine));
        if (!haveTape && !haveSidecar) break;
        ++lineNumber;
        if (haveTape != haveSidecar) {
            errorDetail = "depth_tape/depth_sidecar line count mismatch at line " + std::to_string(lineNumber);
            lineNumberOut = lineNumber;
            if (!allowPartial) out.resize(initialSize);
            return Status::CorruptData;
        }
        if (!tapeLine.empty() && tapeLine.back() == '\r') tapeLine.pop_back();
        if (!sidecarLine.empty() && sidecarLine.back() == '\r') sidecarLine.pop_back();
        if (tapeLine.empty() && sidecarLine.empty()) continue;
        DepthRow row{};
        const auto st = parseDepthTapeSidecarLine(std::string_view{tapeLine}, std::string_view{sidecarLine}, row);
        if (!isOk(st)) {
            errorDetail = "failed to parse depth_tape/depth_sidecar line " + std::to_string(lineNumber)
                + ": " + std::string{statusToString(st)};
            lineNumberOut = lineNumber;
            if (!allowPartial) out.resize(initialSize);
            return st;
        }
        out.push_back(std::move(row));
    }
    lineNumberOut = lineNumber;
    return Status::Ok;
}

}  // namespace

void SessionReplay::reset() noexcept {
    trades_.clear();
    bookTickers_.clear();
    candles_.clear();
    candles2_.clear();
    depths_.clear();
    events_.clear();
    buckets_.clear();
    snapshot_ = SnapshotDocument{};
    snapshotLoaded_ = false;
    book_.reset();
    cursor_ = 0;
    firstTsNs_ = 0;
    lastTsNs_ = 0;
    status_ = Status::Ok;
    errorDetail_.clear();
    gapDetected_ = false;
    sequenceValidationAvailable_ = false;
    parseFailureCount_ = 0;
    integrityFailureCount_ = 0;
    partialDepthCorrupt_ = false;
    partialDepthError_.clear();
    partialDepthLine_ = 0;
    loadReport_ = hftrec::corpus::LoadReport{};
    manifestHints_ = ManifestHints{};
    sessionDir_.clear();
    resetIntegrity_();
}

Status SessionReplay::addTradesFile(const std::filesystem::path& path, std::size_t reserveHint) noexcept {
    if (path.empty()) {
        errorDetail_ = "trades path is empty";
        ++parseFailureCount_;
        noteIncident_(IntegrityIncident{
            IntegrityChannel::Trades,
            IntegrityIncidentKind::ParseError,
            IntegritySeverity::Error,
            "invalid_argument",
            errorDetail_,
            0,
            0,
            {},
            {},
            true
        });
        return Status::InvalidArgument;
    }

    std::size_t lineNumber = 0;
    const auto st = loadJsonl<TradeRow>(path, trades_, errorDetail_, parseTradeCanonicalLine, lineNumber, reserveHint);
    if (!isOk(st)) {
        ++parseFailureCount_;
        auto& summary = summaryFor_(IntegrityChannel::Trades);
        summary.state = ChannelHealthState::Corrupt;
        summary.exactReplayEligible = false;
        noteIncident_(IntegrityIncident{
            IntegrityChannel::Trades,
            IntegrityIncidentKind::ParseError,
            IntegritySeverity::Error,
            "parse_error",
            errorDetail_,
            0,
            lineNumber,
            {},
            path.filename().string(),
            true
        });
    }
    return st;
}

Status SessionReplay::addBookTickerFile(const std::filesystem::path& path, std::size_t reserveHint) noexcept {
    if (path.empty()) {
        errorDetail_ = "bookticker path is empty";
        ++parseFailureCount_;
        noteIncident_(IntegrityIncident{
            IntegrityChannel::BookTicker,
            IntegrityIncidentKind::ParseError,
            IntegritySeverity::Error,
            "invalid_argument",
            errorDetail_,
            0,
            0,
            {},
            {},
            true
        });
        return Status::InvalidArgument;
    }

    std::size_t lineNumber = 0;
    const auto st = loadJsonl<BookTickerRow>(path, bookTickers_, errorDetail_, parseBookTickerCanonicalLine, lineNumber, reserveHint);
    if (!isOk(st)) {
        ++parseFailureCount_;
        auto& summary = summaryFor_(IntegrityChannel::BookTicker);
        summary.state = ChannelHealthState::Corrupt;
        summary.exactReplayEligible = false;
        noteIncident_(IntegrityIncident{
            IntegrityChannel::BookTicker,
            IntegrityIncidentKind::ParseError,
            IntegritySeverity::Error,
            "parse_error",
            errorDetail_,
            0,
            lineNumber,
            {},
            path.filename().string(),
            true
        });
    }
    return st;
}

Status SessionReplay::addCandlesFile(const std::filesystem::path& path,
                                     std::size_t reserveHint,
                                     bool rebuildTimeline) noexcept {
    if (path.empty()) {
        errorDetail_ = "candles path is empty";
        ++parseFailureCount_;
        return Status::InvalidArgument;
    }
    std::size_t lineNumber = 0;
    const auto st = loadJsonl<CandleRow>(path, candles_, errorDetail_, parseFinamCandleArchiveLine, lineNumber, reserveHint);
    if (!isOk(st)) {
        ++parseFailureCount_;
        status_ = st;
        return st;
    }
    sortCandles(candles_);
    if (rebuildTimeline) rebuildBuckets_();
    return Status::Ok;
}

Status SessionReplay::addCandles2File(const std::filesystem::path& path,
                                      std::size_t reserveHint,
                                      bool rebuildTimeline) noexcept {
    if (path.empty()) {
        errorDetail_ = "candles2 path is empty";
        ++parseFailureCount_;
        return Status::InvalidArgument;
    }

    std::size_t lineNumber = 0;
    const auto st = loadJsonl<CandleRow>(path, candles2_, errorDetail_, parseFinamCandleArchiveLine, lineNumber, reserveHint);
    if (!isOk(st)) {
        ++parseFailureCount_;
        status_ = st;
        return st;
    }
    sortCandles(candles2_);
    if (rebuildTimeline) rebuildBuckets_();
    return Status::Ok;
}

Status SessionReplay::addDepthFile(const std::filesystem::path& path, std::size_t reserveHint) noexcept {
    return addDepthFile_(path, false, reserveHint);
}

Status SessionReplay::addDepthFileAllowPartial(const std::filesystem::path& path, std::size_t reserveHint) noexcept {
    return addDepthFile_(path, true, reserveHint);
}

Status SessionReplay::addDepthFile_(const std::filesystem::path& path, bool allowPartial, std::size_t reserveHint) noexcept {
    if (path.empty()) {
        errorDetail_ = "depth path is empty";
        ++parseFailureCount_;
        noteIncident_(IntegrityIncident{
            IntegrityChannel::Depth,
            IntegrityIncidentKind::ParseError,
            IntegritySeverity::Error,
            "invalid_argument",
            errorDetail_,
            0,
            0,
            {},
            {},
            true
        });
        return Status::InvalidArgument;
    }

    std::size_t lineNumber = 0;
    auto st = Status::Ok;
    if (path.filename() != "depth_tape.jsonl" &&
        path.filename() != "depth_sidecar.jsonl") {
        errorDetail_ = "current depth input must be depth_tape.jsonl or depth_sidecar.jsonl";
        st = Status::InvalidArgument;
    } else {
        const std::filesystem::path tapePath = path.filename() == "depth_tape.jsonl"
            ? path
            : path.parent_path() / "depth_tape.jsonl";
        const std::filesystem::path sidecarPath = path.filename() == "depth_sidecar.jsonl"
            ? path
            : path.parent_path() / "depth_sidecar.jsonl";
        st = loadDepthTapeSidecarJsonl(tapePath, sidecarPath, depths_,
                                       errorDetail_, lineNumber, allowPartial,
                                       reserveHint);
    }
    if (!isOk(st)) {
        ++parseFailureCount_;
        auto& summary = summaryFor_(IntegrityChannel::Depth);
        summary.state = ChannelHealthState::Corrupt;
        summary.exactReplayEligible = false;
        noteIncident_(IntegrityIncident{
            IntegrityChannel::Depth,
            IntegrityIncidentKind::ParseError,
            IntegritySeverity::Error,
            "parse_error",
            errorDetail_,
            0,
            lineNumber,
            {},
            path.filename().string(),
            true
        });
        if (allowPartial && !depths_.empty()) {
            partialDepthCorrupt_ = true;
            partialDepthError_ = errorDetail_;
            partialDepthLine_ = lineNumber;
        }
        refreshHealthSummary_();
    }
    return st;
}

Status SessionReplay::open(const std::filesystem::path& sessionDir) noexcept {
    reset();
    sessionDir_ = sessionDir;

    hftrec::corpus::CorpusLoader loader{};
    hftrec::corpus::SessionCorpus corpus{};
    loadReport_ = hftrec::corpus::LoadReport{};
    status_ = loader.loadDetailed(sessionDir, corpus, loadReport_);

    if (loadReport_.manifestPresent) {
        manifestHints_.present = true;
        manifestHints_.exchange = corpus.manifest.exchange;
        manifestHints_.tradesEnabled = corpus.manifest.tradesEnabled;
        manifestHints_.bookTickerEnabled = corpus.manifest.bookTickerEnabled;
        manifestHints_.orderbookEnabled = corpus.manifest.orderbookEnabled;
        manifestHints_.endedAtNs = corpus.manifest.endedAtNs;
    } else {
        (void)loadManifestHints_(sessionDir);
    }

    if (!isOk(status_)) {
        ++parseFailureCount_;
        if (!loadReport_.issues.empty()) {
            const auto& issue = loadReport_.issues.front();
            applyLoadIssueToIntegritySummary(issue, integritySummary_);
            if (!issue.artifact.empty() && issue.lineOrRow != 0u) {
                errorDetail_ = issue.artifact + " line " + std::to_string(issue.lineOrRow)
                    + ": " + issue.detail;
            } else if (!issue.artifact.empty()) {
                errorDetail_ = issue.artifact + ": " + issue.detail;
            } else {
                errorDetail_ = issue.detail;
            }
        } else if (errorDetail_.empty()) {
            errorDetail_ = "failed to load session corpus";
        }
        refreshHealthSummary_();
        maybeWriteIntegrityReport_();
        return status_;
    }

    trades_.reserve(corpus.tradeLines.size());
    for (const auto& line : corpus.tradeLines) {
        if (line.empty()) continue;
        TradeRow row{};
        const auto st = parseTradeLine(std::string_view{line}, row);
        if (!isOk(st)) {
            errorDetail_ = "failed to parse trades.jsonl line from loaded corpus";
            ++parseFailureCount_;
            status_ = st;
            refreshHealthSummary_();
            maybeWriteIntegrityReport_();
            return status_;
        }
        trades_.push_back(std::move(row));
    }



    bookTickers_.reserve(corpus.bookTickerLines.size());
    for (const auto& line : corpus.bookTickerLines) {
        if (line.empty()) continue;
        BookTickerRow row{};
        const auto st = parseBookTickerLine(std::string_view{line}, row);
        if (!isOk(st)) {
            errorDetail_ = "failed to parse bookticker.jsonl line from loaded corpus";
            ++parseFailureCount_;
            status_ = st;
            refreshHealthSummary_();
            maybeWriteIntegrityReport_();
            return status_;
        }
        bookTickers_.push_back(std::move(row));
    }









    candles_.reserve(corpus.candleLines.size());
    for (const auto& line : corpus.candleLines) {
        if (line.empty()) continue;
        CandleRow row{};
        const auto st = parseFinamCandleArchiveLine(std::string_view{line}, row);
        if (!isOk(st)) {
            errorDetail_ = "failed to parse candles.jsonl line from loaded corpus";
            ++parseFailureCount_;
            status_ = st;
            refreshHealthSummary_();
            maybeWriteIntegrityReport_();
            return status_;
        }
        candles_.push_back(std::move(row));
    }
    candles2_.reserve(corpus.candle2Lines.size());
    for (const auto& line : corpus.candle2Lines) {
        if (line.empty()) continue;
        CandleRow row{};
        const auto st = parseFinamCandleArchiveLine(std::string_view{line}, row);
        if (!isOk(st)) {
            errorDetail_ = "failed to parse candles2.jsonl line from loaded corpus";
            ++parseFailureCount_;
            status_ = st;
            refreshHealthSummary_();
            maybeWriteIntegrityReport_();
            return status_;
        }
        candles2_.push_back(std::move(row));
    }
    sortCandles(candles_);
    sortCandles(candles2_);

    depths_ = std::move(corpus.depthRows);
    if (manifestHints_.exchange == "bitget") {
        normalizeFixedDepthSnapshotDeltas(depths_);
    }

    finalize();
    maybeWriteIntegrityReport_();
    return status_;
}

void SessionReplay::resetIntegrity_() noexcept {
    integritySummary_ = SessionIntegritySummary{};
    integritySummary_.trades.state = ChannelHealthState::NotCaptured;
    integritySummary_.liquidations.state = ChannelHealthState::NotCaptured;
    integritySummary_.bookTicker.state = ChannelHealthState::NotCaptured;
    integritySummary_.depth.state = ChannelHealthState::NotCaptured;
    integritySummary_.snapshot.state = ChannelHealthState::NotCaptured;
}

ChannelIntegritySummary& SessionReplay::summaryFor_(IntegrityChannel channel) noexcept {
    switch (channel) {
        case IntegrityChannel::Trades:       return integritySummary_.trades;
        case IntegrityChannel::Liquidations: return integritySummary_.liquidations;
        case IntegrityChannel::BookTicker: return integritySummary_.bookTicker;
        case IntegrityChannel::Depth:      return integritySummary_.depth;
        case IntegrityChannel::Snapshot:   return integritySummary_.snapshot;
    }
    return integritySummary_.trades;
}

void SessionReplay::restorePartialDepthIncident_() noexcept {
    if (!partialDepthCorrupt_) return;
    if (errorDetail_.empty()) errorDetail_ = partialDepthError_;
    auto& summary = summaryFor_(IntegrityChannel::Depth);
    summary.state = ChannelHealthState::Corrupt;
    summary.exactReplayEligible = false;
    noteIncident_(IntegrityIncident{
        IntegrityChannel::Depth,
        IntegrityIncidentKind::ParseError,
        IntegritySeverity::Error,
        "parse_error",
        partialDepthError_,
        0,
        partialDepthLine_,
        {},
        "depth_tape.jsonl+depth_sidecar.jsonl",
        true
    });
}

void SessionReplay::noteIncident_(const IntegrityIncident& incident) noexcept {
    integritySummary_.incidents.push_back(incident);
    ++integritySummary_.totalIncidents;
    if (incident.severity > integritySummary_.highestSeverity) {
        integritySummary_.highestSeverity = incident.severity;
    }

    auto& summary = summaryFor_(incident.channel);
    ++summary.incidentCount;
    if (incident.kind == IntegrityIncidentKind::DepthSequenceGap) ++summary.gapCount;
    if (incident.kind == IntegrityIncidentKind::ParseError) ++summary.parseErrorCount;
    if (incident.severity > summary.highestSeverity) summary.highestSeverity = incident.severity;
    if (summary.reasonCode.empty()) summary.reasonCode = incident.reasonCode;
    if (summary.reasonText.empty()) summary.reasonText = incident.message;
}

bool SessionReplay::loadManifestHints_(const std::filesystem::path& sessionDir) noexcept {
    const auto path = sessionDir / "manifest.json";
    if (!std::filesystem::exists(path)) return false;

    std::string blob;
    if (!readWholeFile(path, blob)) return false;
    capture::SessionManifest manifest{};
    if (!isOk(capture::parseManifestJson(blob, manifest))) return false;

    manifestHints_.present = true;
    manifestHints_.exchange = manifest.exchange;
    manifestHints_.tradesEnabled = manifest.tradesEnabled;
    manifestHints_.tradesRequired = manifest.tradesRequiredWhenEnabled;
    manifestHints_.bookTickerEnabled = manifest.bookTickerEnabled;
    manifestHints_.bookTickerRequired = manifest.bookTickerRequiredWhenEnabled;
    manifestHints_.orderbookEnabled = manifest.orderbookEnabled;
    manifestHints_.orderbookRequired = manifest.orderbookRequiredWhenEnabled;
    manifestHints_.endedAtNs = manifest.endedAtNs;
    return true;
}

void SessionReplay::markStaleLiveChannels_() noexcept {
    constexpr std::int64_t kLiveStreamStaleGraceNs = 30000000000LL;
    if (manifestHints_.endedAtNs <= 0) return;
    const auto mark = [&](IntegrityChannel channel,
                          std::int64_t lastReceiveRealtimeNs) noexcept {
        if (lastReceiveRealtimeNs <= 0 ||
            manifestHints_.endedAtNs <= lastReceiveRealtimeNs) return;
        const std::int64_t gapNs =
            manifestHints_.endedAtNs - lastReceiveRealtimeNs;
        if (gapNs <= kLiveStreamStaleGraceNs) return;
        noteIncident_(IntegrityIncident{
            channel,
            IntegrityIncidentKind::StreamStale,
            IntegritySeverity::Warning,
            "stream_stale",
            "enabled live stream stopped before session end",
            manifestHints_.endedAtNs,
            0,
            std::to_string(manifestHints_.endedAtNs),
            std::to_string(lastReceiveRealtimeNs),
            true
        });
    };
    const auto latestReceiveRealtime = [](const auto& rows) noexcept {
        std::int64_t latest = 0;
        for (const auto& row : rows) {
            if (row.arrival.receiveRealtimeNs > latest)
                latest = row.arrival.receiveRealtimeNs;
        }
        return latest;
    };
    if (manifestHints_.bookTickerEnabled && !bookTickers_.empty()) {
        mark(IntegrityChannel::BookTicker,
             latestReceiveRealtime(bookTickers_));
    }
    if (manifestHints_.orderbookEnabled && !depths_.empty()) {
        mark(IntegrityChannel::Depth, latestReceiveRealtime(depths_));
    }
}

void SessionReplay::finalizeChannelStates_() noexcept {
    const auto markSimpleChannel = [&](IntegrityChannel channel,
                                       bool enabled,
                                       bool required,
                                       std::size_t count) {
        auto& summary = summaryFor_(channel);
        if (!enabled) {
            summary.state = ChannelHealthState::NotCaptured;
            summary.exactReplayEligible = false;
            if (summary.reasonCode.empty()) summary.reasonCode = "not_captured";
            if (summary.reasonText.empty()) summary.reasonText = "channel disabled for session";
            return;
        }
        if (summary.state == ChannelHealthState::Corrupt) {
            summary.exactReplayEligible = false;
            return;
        }
        if (count == 0u) {
            if (!required) {
                summary.state = ChannelHealthState::Clean;
                summary.exactReplayEligible = true;
                if (summary.reasonCode.empty()) summary.reasonCode = "empty_optional";
                if (summary.reasonText.empty()) summary.reasonText = "optional channel captured no events";
                return;
            }
            summary.state = ChannelHealthState::Missing;
            summary.exactReplayEligible = false;
            if (summary.reasonCode.empty()) summary.reasonCode = "missing_file";
            if (summary.reasonText.empty()) summary.reasonText = "enabled channel has no rows";
            noteIncident_(IntegrityIncident{
                channel,
                IntegrityIncidentKind::MissingFile,
                IntegritySeverity::Warning,
                "missing_file",
                "enabled channel has no rows",
                0,
                0,
                {},
                {},
                true
            });
            return;
        }
        if (summary.incidentCount != 0u) {
            if (summary.state != ChannelHealthState::Corrupt) summary.state = ChannelHealthState::Degraded;
            summary.exactReplayEligible = false;
            return;
        }
        summary.state = ChannelHealthState::Clean;
        summary.exactReplayEligible = true;
        if (summary.reasonCode.empty()) summary.reasonCode = "ok";
        if (summary.reasonText.empty()) summary.reasonText = "channel loaded cleanly";
    };

    markSimpleChannel(IntegrityChannel::Trades, manifestHints_.tradesEnabled,
                      manifestHints_.tradesRequired, trades_.size());
    markSimpleChannel(IntegrityChannel::BookTicker,
                      manifestHints_.bookTickerEnabled,
                      manifestHints_.bookTickerRequired,
                      bookTickers_.size());

    auto& snapshotSummary = integritySummary_.snapshot;
    if (!manifestHints_.orderbookEnabled) {
        snapshotSummary.state = ChannelHealthState::NotCaptured;
        snapshotSummary.exactReplayEligible = false;
        if (snapshotSummary.reasonCode.empty()) snapshotSummary.reasonCode = "not_captured";
        if (snapshotSummary.reasonText.empty()) snapshotSummary.reasonText = "orderbook channel disabled for session";
    } else if (!snapshotLoaded_) {
        snapshotSummary.state = ChannelHealthState::NotCaptured;
        snapshotSummary.exactReplayEligible = false;
        if (snapshotSummary.reasonCode.empty()) snapshotSummary.reasonCode = "optional_not_captured";
        if (snapshotSummary.reasonText.empty()) snapshotSummary.reasonText = "separate orderbook snapshot file is not part of the corpus";
    } else if (snapshotSummary.incidentCount == 0u) {
        snapshotSummary.state = ChannelHealthState::Clean;
        snapshotSummary.exactReplayEligible = true;
        if (snapshotSummary.reasonCode.empty()) snapshotSummary.reasonCode = "ok";
        if (snapshotSummary.reasonText.empty()) snapshotSummary.reasonText = "snapshot anchor available";
    }

    auto& depthSummary = integritySummary_.depth;
    if (!manifestHints_.orderbookEnabled) {
        depthSummary.state = ChannelHealthState::NotCaptured;
        depthSummary.exactReplayEligible = false;
        if (depthSummary.reasonCode.empty()) depthSummary.reasonCode = "not_captured";
        if (depthSummary.reasonText.empty()) depthSummary.reasonText = "orderbook channel disabled for session";
    } else if (depthSummary.state == ChannelHealthState::Corrupt) {
        depthSummary.exactReplayEligible = false;
    } else if (depths_.empty() && manifestHints_.orderbookRequired) {
        depthSummary.state = ChannelHealthState::Missing;
        depthSummary.exactReplayEligible = false;
        if (depthSummary.reasonCode.empty()) depthSummary.reasonCode = "missing_file";
        if (depthSummary.reasonText.empty()) depthSummary.reasonText = "depth channel enabled but file has no rows";
    } else if (depthSummary.incidentCount == 0u) {
        depthSummary.state = ChannelHealthState::Clean;
        depthSummary.exactReplayEligible = true;
        if (depthSummary.reasonCode.empty()) depthSummary.reasonCode = "ok";
        if (depthSummary.reasonText.empty()) {
            depthSummary.reasonText = depths_.empty()
                ? "optional depth channel captured no events"
                : "depth rows loaded";
        }
    } else {
        depthSummary.state = ChannelHealthState::Degraded;
        depthSummary.exactReplayEligible = false;
    }
}

void SessionReplay::refreshHealthSummary_() noexcept {
    finalizeChannelStates_();
    bool anyLiveChannel = false;
    bool exact = true;
    const auto includeTracked = [&](bool enabled,
                                    const ChannelIntegritySummary& summary) noexcept {
        if (!enabled) return;
        anyLiveChannel = true;
        exact = exact && summary.exactReplayEligible;
    };
    includeTracked(manifestHints_.tradesEnabled, integritySummary_.trades);
    includeTracked(manifestHints_.bookTickerEnabled,
                   integritySummary_.bookTicker);
    includeTracked(manifestHints_.orderbookEnabled, integritySummary_.depth);

    integritySummary_.exactReplayEligible = anyLiveChannel && exact;

    integritySummary_.sessionHealth = SessionHealth::Clean;
    const ChannelIntegritySummary* channels[] = {
        &integritySummary_.trades,
        &integritySummary_.liquidations,
        &integritySummary_.bookTicker,
        &integritySummary_.depth,
        &integritySummary_.snapshot,
    };
    for (const auto* channel : channels) {
        if (channel->state == ChannelHealthState::Corrupt) {
            integritySummary_.sessionHealth = SessionHealth::Corrupt;
            return;
        }
        if (channel->state == ChannelHealthState::Degraded || channel->state == ChannelHealthState::Missing) {
            integritySummary_.sessionHealth = SessionHealth::Degraded;
        }
    }
}

void SessionReplay::maybeWriteIntegrityReport_() noexcept {
    if (sessionDir_.empty()) return;
    std::error_code ec;
    std::filesystem::create_directories(sessionDir_ / "reports", ec);
    if (ec) return;

    std::ofstream out(sessionDir_ / "reports" / "integrity_report.json", std::ios::out | std::ios::trunc);
    if (!out.is_open()) return;

    const auto writeChannel = [&](std::ostream& stream,
                                  std::string_view name,
                                  const ChannelIntegritySummary& summary,
                                  bool trailingComma) {
        stream << "    \"" << name << "\": {\n";
        stream << "      \"state\": " << json::quote(std::string{toString(summary.state)}) << ",\n";
        stream << "      \"exact_replay_eligible\": " << (summary.exactReplayEligible ? "true" : "false") << ",\n";
        stream << "      \"incident_count\": " << summary.incidentCount << ",\n";
        stream << "      \"gap_count\": " << summary.gapCount << ",\n";
        stream << "      \"parse_error_count\": " << summary.parseErrorCount << ",\n";
        stream << "      \"highest_severity\": " << json::quote(std::string{toString(summary.highestSeverity)}) << ",\n";
        stream << "      \"reason_code\": " << json::quote(summary.reasonCode) << ",\n";
        stream << "      \"reason_text\": " << json::quote(summary.reasonText) << "\n";
        stream << "    }" << (trailingComma ? "," : "") << "\n";
    };

    out << "{\n";
    out << "  \"session_health\": " << json::quote(std::string{toString(integritySummary_.sessionHealth)}) << ",\n";
    out << "  \"exact_replay_eligible\": " << (integritySummary_.exactReplayEligible ? "true" : "false") << ",\n";
    out << "  \"total_incidents\": " << integritySummary_.totalIncidents << ",\n";
    out << "  \"highest_severity\": " << json::quote(std::string{toString(integritySummary_.highestSeverity)}) << ",\n";
    out << "  \"channels\": {\n";
    writeChannel(out, "trades", integritySummary_.trades, true);
    writeChannel(out, "liquidations", integritySummary_.liquidations, true);
    writeChannel(out, "bookticker", integritySummary_.bookTicker, true);
    writeChannel(out, "depth", integritySummary_.depth, true);
    writeChannel(out, "snapshot", integritySummary_.snapshot, false);
    out << "  },\n";
    out << "  \"incidents\": [\n";
    for (std::size_t i = 0; i < integritySummary_.incidents.size(); ++i) {
        const auto& incident = integritySummary_.incidents[i];
        out << "    {\n";
        out << "      \"channel\": " << json::quote(std::string{toString(incident.channel)}) << ",\n";
        out << "      \"kind\": " << json::quote(std::string{toString(incident.kind)}) << ",\n";
        out << "      \"severity\": " << json::quote(std::string{toString(incident.severity)}) << ",\n";
        out << "      \"reason_code\": " << json::quote(incident.reasonCode) << ",\n";
        out << "      \"message\": " << json::quote(incident.message) << ",\n";
        out << "      \"detected_at_ts_ns\": " << incident.detectedAtTsNs << ",\n";
        out << "      \"row_index\": " << incident.rowIndex << ",\n";
        out << "      \"expected_value\": " << json::quote(incident.expectedValue) << ",\n";
        out << "      \"observed_value\": " << json::quote(incident.observedValue) << ",\n";
        out << "      \"exactness_lost\": " << (incident.exactnessLost ? "true" : "false") << "\n";
        out << "    }" << (i + 1u < integritySummary_.incidents.size() ? "," : "") << "\n";
    }
    out << "  ]\n";
    out << "}\n";
}

}  // namespace hftrec::replay
