#include "core/corpus/CorpusLoader.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <unordered_map>
#include <utility>

#include "core/capture/SessionManifest.hpp"
#include "core/corpus/InstrumentMetadata.hpp"
#include "core/common/MiniJsonParser.hpp"
#include "core/replay/EventRows.hpp"
#include "core/replay/JsonLineParser.hpp"

namespace hftrec::corpus {

namespace {

Status parseTradeCanonicalLine(std::string_view line, hftrec::replay::TradeRow& row) noexcept {
    return hftrec::replay::parseTradeLine(line, row);
}

Status parseBookTickerCanonicalLine(std::string_view line, hftrec::replay::BookTickerRow& row) noexcept {
    return hftrec::replay::parseBookTickerLine(line, row);
}

Status parseDepthCanonicalLine(std::string_view line, hftrec::replay::DepthRow& row) noexcept {
    return hftrec::replay::parseDepthLine(line, row);
}

constexpr std::int64_t kSeekIndexVersionCurrent = 1;

using JsonParser = hftrec::json::MiniJsonParser;

struct SourceArtifactInfo {
    std::uint64_t sizeBytes{0};
    std::uint64_t rowCount{0};
};

bool readWholeFile(const std::filesystem::path& path, std::string& out) noexcept {
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) return false;
    out.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    return stream.good() || stream.eof();
}

bool readWholeFileOptional(const std::filesystem::path& path, std::string& out) noexcept {
    if (!std::filesystem::exists(path)) {
        out.clear();
        return true;
    }
    return readWholeFile(path, out);
}

std::uint64_t fileSizeOrZero(const std::filesystem::path& path) noexcept {
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    return ec ? 0u : static_cast<std::uint64_t>(size);
}

void addIssue(LoadReport& report,
              LoadIssueCode code,
              LoadIssueSeverity severity,
              Status statusHint,
              std::string channel,
              std::string artifact,
              std::size_t lineOrRow,
              std::string detail) {
    appendLoadIssue(report, LoadIssue{
        code,
        severity,
        statusHint,
        std::move(channel),
        std::move(artifact),
        lineOrRow,
        std::move(detail),
    });
}

bool parseSourceInfoObject(JsonParser& parser, SourceArtifactInfo& out) noexcept {
    if (!parser.parseObjectStart()) return false;
    if (parser.peek('}')) return false;
    std::string key;
    do {
        if (!parser.parseKey(key)) return false;
        if (key == "size_bytes") {
            if (!parser.parseUInt64(out.sizeBytes)) return false;
        } else if (key == "row_count") {
            if (!parser.parseUInt64(out.rowCount)) return false;
        } else if (!parser.skipValue()) {
            return false;
        }
        if (parser.peek('}')) break;
    } while (parser.parseComma());
    return parser.parseObjectEnd();
}

bool parseSeekSources(JsonParser& parser,
                      std::unordered_map<std::string, SourceArtifactInfo>& out) noexcept {
    out.clear();
    if (!parser.parseObjectStart()) return false;
    if (parser.peek('}')) return parser.parseObjectEnd();
    std::string key;
    do {
        if (!parser.parseKey(key)) return false;
        SourceArtifactInfo info{};
        if (!parseSourceInfoObject(parser, info)) return false;
        out.emplace(std::move(key), info);
        if (parser.peek('}')) break;
    } while (parser.parseComma());
    return parser.parseObjectEnd();
}

bool parseSeekBuckets(JsonParser& parser, std::vector<SeekIndexBucket>& out) noexcept {
    out.clear();
    if (!parser.parseArrayStart()) return false;
    if (parser.peek(']')) return parser.parseArrayEnd();
    std::string key;
    do {
        SeekIndexBucket bucket{};
        if (!parser.parseObjectStart()) return false;
        if (parser.peek('}')) return false;
        do {
            if (!parser.parseKey(key)) return false;
            if (key == "ts_ns") {
                if (!parser.parseInt64(bucket.tsNs)) return false;
            } else if (key == "event_index_start") {
                if (!parser.parseUInt64(bucket.eventIndexStart)) return false;
            } else if (key == "depth_row_index") {
                if (!parser.parseUInt64(bucket.depthRowIndex)) return false;
            } else if (!parser.skipValue()) {
                return false;
            }
            if (parser.peek('}')) break;
        } while (parser.parseComma());
        if (!parser.parseObjectEnd()) return false;
        out.push_back(bucket);
        if (parser.peek(']')) break;
    } while (parser.parseComma());
    return parser.parseArrayEnd();
}

bool parseSeekIndexDocument(std::string_view document,
                            std::int64_t& version,
                            std::unordered_map<std::string, SourceArtifactInfo>& sources,
                            std::vector<SeekIndexBucket>& buckets) noexcept {
    version = 0;
    JsonParser parser{document};
    if (!parser.parseObjectStart()) return false;
    if (parser.peek('}')) return false;
    std::string key;
    do {
        if (!parser.parseKey(key)) return false;
        if (key == "version") {
            if (!parser.parseInt64(version)) return false;
        } else if (key == "sources") {
            if (!parseSeekSources(parser, sources)) return false;
        } else if (key == "buckets") {
            if (!parseSeekBuckets(parser, buckets)) return false;
        } else if (!parser.skipValue()) {
            return false;
        }
        if (parser.peek('}')) break;
    } while (parser.parseComma());
    return parser.parseObjectEnd() && parser.finish();
}

template <typename Parser, typename RowT>
Status loadJsonLines(const std::filesystem::path& path,
                     std::vector<std::string>& out,
                     Parser&& parse,
                     LoadReport& report,
                     const char* channelName,
                     const std::string& artifactName,
                     bool required,
                     ChannelLoadState& channelState) noexcept {
    if (!std::filesystem::exists(path)) {
        channelState = required ? ChannelLoadState::Missing : ChannelLoadState::NotCaptured;
        if (required) {
            addIssue(report,
                     LoadIssueCode::MissingRequiredArtifact,
                     LoadIssueSeverity::Fatal,
                     Status::CorruptData,
                     channelName,
                     artifactName,
                     0,
                     "required canonical artifact is missing");
            return Status::CorruptData;
        }
        return Status::Ok;
    }

    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open()) {
        channelState = ChannelLoadState::Corrupt;
        addIssue(report,
                 LoadIssueCode::UnreadableArtifact,
                 LoadIssueSeverity::Fatal,
                 Status::IoError,
                 channelName,
                 artifactName,
                 0,
                 "failed to open canonical artifact");
        return Status::IoError;
    }

    channelState = ChannelLoadState::Clean;
    std::string line;
    std::size_t lineNumber = 0;
    while (std::getline(stream, line)) {
        ++lineNumber;
        out.push_back(line);
        std::string_view view{out.back()};
        if (!view.empty() && view.back() == '\r') {
            out.back().pop_back();
            view = out.back();
        }
        if (view.empty()) continue;
        RowT row{};
        if (!isOk(parse(view, row))) {
            channelState = ChannelLoadState::Corrupt;
            addIssue(report,
                     LoadIssueCode::InvalidJsonLine,
                     LoadIssueSeverity::Fatal,
                     Status::CorruptData,
                     channelName,
                     artifactName,
                     lineNumber,
                     "failed to parse canonical JSON line at line " + std::to_string(lineNumber));
            return Status::CorruptData;
        }
    }
    return Status::Ok;
}

Status loadSnapshots(const std::filesystem::path& sessionDir,
                     const std::vector<std::string>& declaredSnapshotFiles,
                     bool required,
                     std::vector<std::string>& out,
                     LoadReport& report,
                     ChannelLoadState& channelState) noexcept {
    channelState = ChannelLoadState::NotCaptured;
    std::vector<std::filesystem::path> snapshotPaths;
    if (!declaredSnapshotFiles.empty()) {
        for (const auto& file : declaredSnapshotFiles) {
            snapshotPaths.push_back(sessionDir / file);
        }
    } else {
        for (const auto& entry : std::filesystem::directory_iterator(sessionDir)) {
            if (!entry.is_regular_file()) continue;
            const auto name = entry.path().filename().string();
            if (name.rfind("snapshot_", 0) == 0 && entry.path().extension() == ".json") {
                snapshotPaths.push_back(entry.path());
            }
        }
        std::sort(snapshotPaths.begin(), snapshotPaths.end());
    }

    if (snapshotPaths.empty()) {
        channelState = required ? ChannelLoadState::Missing : ChannelLoadState::NotCaptured;
        if (required) {
            addIssue(report,
                     LoadIssueCode::MissingRequiredArtifact,
                     LoadIssueSeverity::Fatal,
                     Status::CorruptData,
                     "snapshot",
                     "snapshot_000.json",
                     0,
                     "required snapshot artifact is missing");
            return Status::CorruptData;
        }
        return Status::Ok;
    }

    channelState = ChannelLoadState::Clean;
    for (std::size_t i = 0; i < snapshotPaths.size(); ++i) {
        std::string document;
        if (!readWholeFile(snapshotPaths[i], document)) {
            channelState = ChannelLoadState::Corrupt;
            addIssue(report,
                     LoadIssueCode::UnreadableArtifact,
                     LoadIssueSeverity::Fatal,
                     Status::IoError,
                     "snapshot",
                     snapshotPaths[i].filename().string(),
                     i + 1u,
                     "failed to open snapshot artifact");
            return Status::IoError;
        }
        hftrec::replay::SnapshotDocument snapshot{};
        if (!isOk(hftrec::replay::parseSnapshotDocument(document, snapshot))) {
            channelState = ChannelLoadState::Corrupt;
            addIssue(report,
                     LoadIssueCode::InvalidJsonDocument,
                     LoadIssueSeverity::Fatal,
                     Status::CorruptData,
                     "snapshot",
                     snapshotPaths[i].filename().string(),
                     i + 1u,
                     "failed to parse canonical snapshot document");
            return Status::CorruptData;
        }
        out.push_back(std::move(document));
    }
    return Status::Ok;
}

void bindSeekIndex(const std::filesystem::path& sessionDir,
                   const SessionCorpus& corpus,
                   LoadReport& report) {
    const auto seekPath = sessionDir / "seek_index.json";
    if (!std::filesystem::exists(seekPath)) {
        report.seekIndexState = ChannelLoadState::Missing;
        return;
    }

    std::string document;
    if (!readWholeFile(seekPath, document)) {
        report.seekIndexState = ChannelLoadState::Corrupt;
        addIssue(report,
                 LoadIssueCode::UnreadableArtifact,
                 LoadIssueSeverity::Fatal,
                 Status::IoError,
                 "seek_index",
                 "seek_index.json",
                 0,
                 "failed to open seek index sidecar");
        return;
    }

    std::int64_t version = 0;
    std::unordered_map<std::string, SourceArtifactInfo> sources;
    std::vector<SeekIndexBucket> buckets;
    if (!parseSeekIndexDocument(document, version, sources, buckets)) {
        report.seekIndexState = ChannelLoadState::Corrupt;
        addIssue(report,
                 LoadIssueCode::InvalidSeekIndex,
                 LoadIssueSeverity::Fatal,
                 Status::CorruptData,
                 "seek_index",
                 "seek_index.json",
                 0,
                 "seek index JSON is malformed");
        return;
    }

    if (version > kSeekIndexVersionCurrent) {
        report.seekIndexState = ChannelLoadState::Corrupt;
        addIssue(report,
                 LoadIssueCode::UnsupportedSchemaVersion,
                 LoadIssueSeverity::Fatal,
                 Status::CorruptData,
                 "seek_index",
                 "seek_index.json",
                 0,
                 "seek index version is newer than supported");
        return;
    }

    const std::unordered_map<std::string, SourceArtifactInfo> currentSources{
        {"trades.jsonl", {fileSizeOrZero(sessionDir / corpus.manifest.tradesPath), static_cast<std::uint64_t>(corpus.tradeLines.size())}},
        {"bookticker.jsonl", {fileSizeOrZero(sessionDir / corpus.manifest.bookTickerPath), static_cast<std::uint64_t>(corpus.bookTickerLines.size())}},
        {"depth.jsonl", {fileSizeOrZero(sessionDir / corpus.manifest.depthPath), static_cast<std::uint64_t>(corpus.depthLines.size())}},
    };

    for (const auto& [name, sourceInfo] : sources) {
        const auto current = currentSources.find(name);
        if (current == currentSources.end()) continue;
        if (current->second.sizeBytes != sourceInfo.sizeBytes || current->second.rowCount != sourceInfo.rowCount) {
            report.seekIndexState = ChannelLoadState::Degraded;
            report.staleSeekIndex = true;
            addIssue(report,
                     LoadIssueCode::StaleSeekIndex,
                     LoadIssueSeverity::Degraded,
                     Status::Ok,
                     "seek_index",
                     "seek_index.json",
                     0,
                     "seek index does not match canonical artifacts and was ignored");
            return;
        }
    }

    report.seekIndexState = ChannelLoadState::Clean;
    report.usedSeekIndex = !buckets.empty();
    report.seekBuckets = std::move(buckets);
}

}  // namespace

Status CorpusLoader::loadDetailed(const std::filesystem::path& sessionDir,
                                  SessionCorpus& out,
                                  LoadReport& report) noexcept {
    out = SessionCorpus{};
    report = LoadReport{};

    if (!std::filesystem::exists(sessionDir)) {
        addIssue(report,
                 LoadIssueCode::MissingSessionDirectory,
                 LoadIssueSeverity::Fatal,
                 Status::InvalidArgument,
                 "session",
                 sessionDir.string(),
                 0,
                 "session directory does not exist");
        out.report = report;
        return report.finalStatus;
    }

    const auto manifestPath = sessionDir / "manifest.json";
    if (!std::filesystem::exists(manifestPath)) {
        report.manifestState = ChannelLoadState::Warning;
        addIssue(report,
                 LoadIssueCode::MissingManifest,
                 LoadIssueSeverity::Warning,
                 Status::Ok,
                 "manifest",
                 "manifest.json",
                 0,
                 "manifest.json is absent; treating session as legacy corpus");
    } else {
        std::string manifestDocument;
        if (!readWholeFile(manifestPath, manifestDocument)) {
            report.manifestState = ChannelLoadState::Corrupt;
            addIssue(report,
                     LoadIssueCode::UnreadableArtifact,
                     LoadIssueSeverity::Fatal,
                     Status::IoError,
                     "manifest",
                     "manifest.json",
                     0,
                     "failed to open manifest.json");
            out.report = report;
            return report.finalStatus;
        }
        const auto manifestStatus = capture::parseManifestJson(manifestDocument, out.manifest);
        report.manifestPresent = true;
        report.corpusSchemaVersion = out.manifest.corpusSchemaVersion;
        if (!isOk(manifestStatus)) {
            report.manifestState = ChannelLoadState::Corrupt;
            addIssue(report,
                     LoadIssueCode::InvalidManifest,
                     LoadIssueSeverity::Fatal,
                     Status::CorruptData,
                     "manifest",
                     "manifest.json",
                     0,
                     "manifest.json is malformed or structurally invalid");
            out.report = report;
            return report.finalStatus;
        }
        if (!capture::isSupportedCorpusSchemaVersion(out.manifest.corpusSchemaVersion)) {
            report.manifestState = ChannelLoadState::Corrupt;
            addIssue(report,
                     LoadIssueCode::UnsupportedSchemaVersion,
                     LoadIssueSeverity::Fatal,
                     Status::CorruptData,
                     "manifest",
                     "manifest.json",
                     0,
                     "manifest declares unsupported corpus schema version");
            out.report = report;
            return report.finalStatus;
        }
        report.manifestState = ChannelLoadState::Clean;
    }

    const bool requireTrades = report.manifestPresent ? (out.manifest.tradesEnabled && out.manifest.tradesRequiredWhenEnabled) : false;
    const bool requireBookTicker = report.manifestPresent ? (out.manifest.bookTickerEnabled && out.manifest.bookTickerRequiredWhenEnabled) : false;
    const bool requireDepth = report.manifestPresent ? (out.manifest.orderbookEnabled && out.manifest.orderbookRequiredWhenEnabled) : false;

    const auto tradesPath = sessionDir / (report.manifestPresent ? out.manifest.tradesPath : "trades.jsonl");
    const auto bookTickerPath = sessionDir / (report.manifestPresent ? out.manifest.bookTickerPath : "bookticker.jsonl");
    const auto depthPath = sessionDir / (report.manifestPresent ? out.manifest.depthPath : "depth.jsonl");

    if (!isOk(loadJsonLines<decltype(&parseTradeCanonicalLine), hftrec::replay::TradeRow>(
                            tradesPath,
                            out.tradeLines,
                            parseTradeCanonicalLine,
                            report,
                            "trades",
                            tradesPath.filename().string(),
                            requireTrades,
                            report.tradesState))) {
        out.report = report;
        return report.finalStatus;
    }
    if (!isOk(loadJsonLines<decltype(&parseBookTickerCanonicalLine), hftrec::replay::BookTickerRow>(
                            bookTickerPath,
                            out.bookTickerLines,
                            parseBookTickerCanonicalLine,
                            report,
                            "bookticker",
                            bookTickerPath.filename().string(),
                            requireBookTicker,
                            report.bookTickerState))) {
        out.report = report;
        return report.finalStatus;
    }
    if (!isOk(loadJsonLines<decltype(&parseDepthCanonicalLine), hftrec::replay::DepthRow>(
                            depthPath,
                            out.depthLines,
                            parseDepthCanonicalLine,
                            report,
                            "depth",
                            depthPath.filename().string(),
                            requireDepth,
                            report.depthState))) {
        out.report = report;
        return report.finalStatus;
    }

    const auto declaredSnapshots = report.manifestPresent ? out.manifest.snapshotFiles : std::vector<std::string>{};
    if (!isOk(loadSnapshots(sessionDir,
                            declaredSnapshots,
                            requireDepth,
                            out.snapshotDocuments,
                            report,
                            report.snapshotState))) {
        out.report = report;
        return report.finalStatus;
    }

    if (report.manifestPresent) {
        if (!readWholeFileOptional(sessionDir / out.manifest.instrumentMetadataPath, out.instrumentMetadataDocument)) {
            addIssue(report,
                     LoadIssueCode::UnreadableArtifact,
                     LoadIssueSeverity::Fatal,
                     Status::IoError,
                     "instrument_metadata",
                     out.manifest.instrumentMetadataPath,
                     0,
                     "failed to open instrument metadata sidecar");
            out.report = report;
            return report.finalStatus;
        }
        if (!out.instrumentMetadataDocument.empty()) {
            InstrumentMetadata metadata{};
            if (!isOk(parseInstrumentMetadataJson(out.instrumentMetadataDocument, metadata))) {
                addIssue(report,
                         LoadIssueCode::InvalidJsonDocument,
                         LoadIssueSeverity::Fatal,
                         Status::CorruptData,
                         "instrument_metadata",
                         out.manifest.instrumentMetadataPath,
                         0,
                         "instrument metadata sidecar is malformed");
                out.report = report;
                return report.finalStatus;
            }
            out.instrumentMetadata = std::move(metadata);
        }
        if (!readWholeFileOptional(sessionDir / out.manifest.sessionAuditPath, out.sessionAuditDocument)
            || !readWholeFileOptional(sessionDir / out.manifest.integrityReportPath, out.integrityReportDocument)
            || !readWholeFileOptional(sessionDir / out.manifest.loaderDiagnosticsPath, out.loaderDiagnosticsDocument)) {
            addIssue(report,
                     LoadIssueCode::UnreadableArtifact,
                     LoadIssueSeverity::Fatal,
                     Status::IoError,
                     "support_artifacts",
                     "reports/*",
                     0,
                     "failed to open one or more support artifacts");
            out.report = report;
            return report.finalStatus;
        }
    }

    if (report.manifestPresent && !out.manifest.structurallyLoadable) {
        for (const auto& blocker : out.manifest.structuralBlockers) {
            addIssue(report,
                     LoadIssueCode::MissingRequiredArtifact,
                     LoadIssueSeverity::Fatal,
                     Status::CorruptData,
                     "manifest",
                     "manifest.json",
                     0,
                     blocker);
        }
        out.report = report;
        return report.finalStatus;
    }

    bindSeekIndex(sessionDir, out, report);
    out.report = report;
    return report.finalStatus;
}

Status CorpusLoader::load(const std::filesystem::path& sessionDir, SessionCorpus& out) noexcept {
    return loadDetailed(sessionDir, out, out.report);
}

}  // namespace hftrec::corpus
