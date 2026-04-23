#include "core/replay/SessionReplay.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <utility>

#include "core/capture/SessionManifest.hpp"
#include "core/common/JsonString.hpp"
#include "core/corpus/CorpusLoader.hpp"
#include "core/corpus/SessionCorpus.hpp"
#include "core/metrics/Metrics.hpp"
#include "core/replay/JsonLineParser.hpp"

namespace hftrec::replay {

namespace {

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

template <typename RowT, typename Parser>
Status loadJsonl(const std::filesystem::path& path,
                 std::vector<RowT>& out,
                 std::string& errorDetail,
                 Parser&& parse,
                 std::size_t& lineNumberOut) noexcept {
    std::ifstream in(path, std::ios::binary);
    if (!in) return Status::Ok;
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

}  // namespace

void SessionReplay::reset() noexcept {
    trades_.clear();
    bookTickers_.clear();
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
    loadReport_ = hftrec::corpus::LoadReport{};
    manifestHints_ = ManifestHints{};
    sessionDir_.clear();
    resetIntegrity_();
}

Status SessionReplay::addTradesFile(const std::filesystem::path& path) noexcept {
    if (path.empty()) {
        errorDetail_ = "trades path is empty";
        ++parseFailureCount_;
        metrics::recordReplayParseFailure("trades");
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
    const auto st = loadJsonl<TradeRow>(path, trades_, errorDetail_, parseTradeLine, lineNumber);
    if (!isOk(st)) {
        ++parseFailureCount_;
        metrics::recordReplayParseFailure("trades");
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

Status SessionReplay::addBookTickerFile(const std::filesystem::path& path) noexcept {
    if (path.empty()) {
        errorDetail_ = "bookticker path is empty";
        ++parseFailureCount_;
        metrics::recordReplayParseFailure("bookticker");
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
    const auto st = loadJsonl<BookTickerRow>(path, bookTickers_, errorDetail_, parseBookTickerLine, lineNumber);
    if (!isOk(st)) {
        ++parseFailureCount_;
        metrics::recordReplayParseFailure("bookticker");
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

Status SessionReplay::addDepthFile(const std::filesystem::path& path) noexcept {
    if (path.empty()) {
        errorDetail_ = "depth path is empty";
        ++parseFailureCount_;
        metrics::recordReplayParseFailure("depth");
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
    const auto st = loadJsonl<DepthRow>(path, depths_, errorDetail_, parseDepthLine, lineNumber);
    if (!isOk(st)) {
        ++parseFailureCount_;
        metrics::recordReplayParseFailure("depth");
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
    }
    return st;
}

Status SessionReplay::addSnapshotFile(const std::filesystem::path& path) noexcept {
    if (path.empty()) {
        errorDetail_ = "snapshot path is empty";
        ++parseFailureCount_;
        metrics::recordReplayParseFailure("snapshot");
        noteIncident_(IntegrityIncident{
            IntegrityChannel::Snapshot,
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
    if (!std::filesystem::exists(path)) {
        errorDetail_ = "snapshot file does not exist: " + path.string();
        ++parseFailureCount_;
        metrics::recordReplayParseFailure("snapshot");
        auto& summary = summaryFor_(IntegrityChannel::Snapshot);
        summary.state = ChannelHealthState::Missing;
        summary.exactReplayEligible = false;
        noteIncident_(IntegrityIncident{
            IntegrityChannel::Snapshot,
            IntegrityIncidentKind::MissingFile,
            IntegritySeverity::Warning,
            "missing_file",
            errorDetail_,
            0,
            0,
            {},
            path.filename().string(),
            true
        });
        return Status::InvalidArgument;
    }
    std::string blob;
    if (!readWholeFile(path, blob)) {
        errorDetail_ = "failed to read snapshot file: " + path.string();
        ++parseFailureCount_;
        metrics::recordReplayParseFailure("snapshot");
        auto& summary = summaryFor_(IntegrityChannel::Snapshot);
        summary.state = ChannelHealthState::Corrupt;
        summary.exactReplayEligible = false;
        noteIncident_(IntegrityIncident{
            IntegrityChannel::Snapshot,
            IntegrityIncidentKind::ParseError,
            IntegritySeverity::Error,
            "read_failed",
            errorDetail_,
            0,
            0,
            {},
            path.filename().string(),
            true
        });
        return Status::IoError;
    }
    SnapshotDocument snap{};
    const auto st = parseSnapshotDocument(blob, snap);
    if (!isOk(st)) {
        errorDetail_ = "failed to parse snapshot file " + path.filename().string()
            + ": " + std::string{statusToString(st)};
        ++parseFailureCount_;
        metrics::recordReplayParseFailure("snapshot");
        auto& summary = summaryFor_(IntegrityChannel::Snapshot);
        summary.state = ChannelHealthState::Corrupt;
        summary.exactReplayEligible = false;
        noteIncident_(IntegrityIncident{
            IntegrityChannel::Snapshot,
            IntegrityIncidentKind::ParseError,
            IntegritySeverity::Error,
            "parse_error",
            errorDetail_,
            0,
            0,
            {},
            path.filename().string(),
            true
        });
        return st;
    }
    snapshot_ = snap;
    snapshotLoaded_ = true;
    book_.applySnapshot(snapshot_);
    return Status::Ok;
}

Status SessionReplay::open(const std::filesystem::path& sessionDir) noexcept {
    const auto startedAt = std::chrono::steady_clock::now();
    reset();
    sessionDir_ = sessionDir;

    hftrec::corpus::CorpusLoader loader{};
    hftrec::corpus::SessionCorpus corpus{};
    loadReport_ = hftrec::corpus::LoadReport{};
    status_ = loader.loadDetailed(sessionDir, corpus, loadReport_);

    if (loadReport_.manifestPresent) {
        manifestHints_.present = true;
        manifestHints_.tradesEnabled = corpus.manifest.tradesEnabled;
        manifestHints_.bookTickerEnabled = corpus.manifest.bookTickerEnabled;
        manifestHints_.orderbookEnabled = corpus.manifest.orderbookEnabled;
    } else {
        (void)loadManifestHints_(sessionDir);
    }

    if (!isOk(status_)) {
        ++parseFailureCount_;
        metrics::recordReplayParseFailure("session");
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
            metrics::recordReplayParseFailure("trades");
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
            metrics::recordReplayParseFailure("bookticker");
            status_ = st;
            refreshHealthSummary_();
            maybeWriteIntegrityReport_();
            return status_;
        }
        bookTickers_.push_back(std::move(row));
    }

    depths_.reserve(corpus.depthLines.size());
    for (const auto& line : corpus.depthLines) {
        if (line.empty()) continue;
        DepthRow row{};
        const auto st = parseDepthLine(std::string_view{line}, row);
        if (!isOk(st)) {
            errorDetail_ = "failed to parse depth.jsonl line from loaded corpus";
            ++parseFailureCount_;
            metrics::recordReplayParseFailure("depth");
            status_ = st;
            refreshHealthSummary_();
            maybeWriteIntegrityReport_();
            return status_;
        }
        depths_.push_back(std::move(row));
    }

    if (!corpus.snapshotDocuments.empty()) {
        const auto st = parseSnapshotDocument(std::string_view{corpus.snapshotDocuments.front()}, snapshot_);
        if (!isOk(st)) {
            errorDetail_ = "failed to parse canonical snapshot document from loaded corpus";
            ++parseFailureCount_;
            metrics::recordReplayParseFailure("snapshot");
            status_ = st;
            refreshHealthSummary_();
            maybeWriteIntegrityReport_();
            return status_;
        }
        snapshotLoaded_ = true;
        book_.applySnapshot(snapshot_);
    }

    finalize();
    maybeWriteIntegrityReport_();
    if (isOk(status_)) {
        const auto loadNs = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - startedAt).count());
        metrics::recordReplayLoad(trades_.size() + bookTickers_.size() + depths_.size(), loadNs);
    }
    return status_;
}

void SessionReplay::resetIntegrity_() noexcept {
    integritySummary_ = SessionIntegritySummary{};
    integritySummary_.trades.state = ChannelHealthState::NotCaptured;
    integritySummary_.bookTicker.state = ChannelHealthState::NotCaptured;
    integritySummary_.depth.state = ChannelHealthState::NotCaptured;
    integritySummary_.snapshot.state = ChannelHealthState::NotCaptured;
}

ChannelIntegritySummary& SessionReplay::summaryFor_(IntegrityChannel channel) noexcept {
    switch (channel) {
        case IntegrityChannel::Trades:     return integritySummary_.trades;
        case IntegrityChannel::BookTicker: return integritySummary_.bookTicker;
        case IntegrityChannel::Depth:      return integritySummary_.depth;
        case IntegrityChannel::Snapshot:   return integritySummary_.snapshot;
    }
    return integritySummary_.trades;
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
    manifestHints_.tradesEnabled = manifest.tradesEnabled;
    manifestHints_.bookTickerEnabled = manifest.bookTickerEnabled;
    manifestHints_.orderbookEnabled = manifest.orderbookEnabled;
    return true;
}

void SessionReplay::finalizeChannelStates_() noexcept {
    const auto markSimpleChannel = [&](IntegrityChannel channel, bool enabled, std::size_t count) {
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

    markSimpleChannel(IntegrityChannel::Trades, manifestHints_.tradesEnabled, trades_.size());
    markSimpleChannel(IntegrityChannel::BookTicker, manifestHints_.bookTickerEnabled, bookTickers_.size());

    auto& snapshotSummary = integritySummary_.snapshot;
    if (!manifestHints_.orderbookEnabled) {
        snapshotSummary.state = ChannelHealthState::NotCaptured;
        snapshotSummary.exactReplayEligible = false;
        if (snapshotSummary.reasonCode.empty()) snapshotSummary.reasonCode = "not_captured";
        if (snapshotSummary.reasonText.empty()) snapshotSummary.reasonText = "orderbook channel disabled for session";
    } else if (!snapshotLoaded_) {
        snapshotSummary.state = ChannelHealthState::Missing;
        snapshotSummary.exactReplayEligible = false;
        if (snapshotSummary.reasonCode.empty()) snapshotSummary.reasonCode = "missing_file";
        if (snapshotSummary.reasonText.empty()) snapshotSummary.reasonText = "orderbook replay has no snapshot anchor";
        noteIncident_(IntegrityIncident{
            IntegrityChannel::Snapshot,
            IntegrityIncidentKind::MissingFile,
            IntegritySeverity::Warning,
            "missing_file",
            "orderbook replay has no snapshot anchor",
            0,
            0,
            {},
            {},
            true
        });
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
    } else if (depths_.empty()) {
        depthSummary.state = ChannelHealthState::Missing;
        depthSummary.exactReplayEligible = false;
        if (depthSummary.reasonCode.empty()) depthSummary.reasonCode = "missing_file";
        if (depthSummary.reasonText.empty()) depthSummary.reasonText = "depth channel enabled but file has no rows";
    } else if (!snapshotLoaded_) {
        depthSummary.state = ChannelHealthState::Degraded;
        depthSummary.exactReplayEligible = false;
        if (depthSummary.reasonCode.empty()) depthSummary.reasonCode = "snapshot_missing_for_depth";
        if (depthSummary.reasonText.empty()) depthSummary.reasonText = "depth rows loaded without snapshot anchor";
    } else if (!sequenceValidationAvailable_) {
        depthSummary.state = ChannelHealthState::Degraded;
        depthSummary.exactReplayEligible = false;
        if (depthSummary.reasonCode.empty()) depthSummary.reasonCode = "exactness_unprovable";
        if (depthSummary.reasonText.empty()) depthSummary.reasonText = "depth continuity cannot be proven from sequence metadata";
    } else if (depthSummary.incidentCount == 0u) {
        depthSummary.state = ChannelHealthState::Clean;
        depthSummary.exactReplayEligible = true;
        if (depthSummary.reasonCode.empty()) depthSummary.reasonCode = "ok";
        if (depthSummary.reasonText.empty()) depthSummary.reasonText = "depth continuity validated";
    }
}

void SessionReplay::refreshHealthSummary_() noexcept {
    finalizeChannelStates_();
    integritySummary_.exactReplayEligible =
        integritySummary_.snapshot.exactReplayEligible && integritySummary_.depth.exactReplayEligible;

    integritySummary_.sessionHealth = SessionHealth::Clean;
    const ChannelIntegritySummary* channels[] = {
        &integritySummary_.trades,
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
