#include "core/replay/SessionReplay.hpp"

#include <chrono>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <utility>

#include "core/metrics/Metrics.hpp"
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
                 std::string& errorDetail,
                 Parser&& parse) noexcept {
    std::ifstream in(path, std::ios::binary);
    if (!in) return Status::Ok;  // channel absent -> simply no rows
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
            return st;
        }
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
    status_ = Status::Ok;
    errorDetail_.clear();
    gapDetected_ = false;
    sequenceValidationAvailable_ = false;
    parseFailureCount_ = 0;
    integrityFailureCount_ = 0;
}

Status SessionReplay::addTradesFile(const std::filesystem::path& path) noexcept {
    if (path.empty()) {
        errorDetail_ = "trades path is empty";
        ++parseFailureCount_;
        metrics::recordReplayParseFailure("trades");
        return Status::InvalidArgument;
    }
    const auto st = loadJsonl<TradeRow>(path, trades_, errorDetail_, parseTradeLine);
    if (!isOk(st)) {
        ++parseFailureCount_;
        metrics::recordReplayParseFailure("trades");
    }
    return st;
}

Status SessionReplay::addBookTickerFile(const std::filesystem::path& path) noexcept {
    if (path.empty()) {
        errorDetail_ = "bookticker path is empty";
        ++parseFailureCount_;
        metrics::recordReplayParseFailure("bookticker");
        return Status::InvalidArgument;
    }
    const auto st = loadJsonl<BookTickerRow>(path, bookTickers_, errorDetail_, parseBookTickerLine);
    if (!isOk(st)) {
        ++parseFailureCount_;
        metrics::recordReplayParseFailure("bookticker");
    }
    return st;
}

Status SessionReplay::addDepthFile(const std::filesystem::path& path) noexcept {
    if (path.empty()) {
        errorDetail_ = "depth path is empty";
        ++parseFailureCount_;
        metrics::recordReplayParseFailure("depth");
        return Status::InvalidArgument;
    }
    const auto st = loadJsonl<DepthRow>(path, depths_, errorDetail_, parseDepthLine);
    if (!isOk(st)) {
        ++parseFailureCount_;
        metrics::recordReplayParseFailure("depth");
    }
    return st;
}

Status SessionReplay::addSnapshotFile(const std::filesystem::path& path) noexcept {
    if (path.empty()) {
        errorDetail_ = "snapshot path is empty";
        ++parseFailureCount_;
        metrics::recordReplayParseFailure("snapshot");
        return Status::InvalidArgument;
    }
    if (!std::filesystem::exists(path)) {
        errorDetail_ = "snapshot file does not exist: " + path.string();
        ++parseFailureCount_;
        metrics::recordReplayParseFailure("snapshot");
        return Status::InvalidArgument;
    }
    std::string blob;
    if (!readWholeFile(path, blob)) {
        errorDetail_ = "failed to read snapshot file: " + path.string();
        ++parseFailureCount_;
        metrics::recordReplayParseFailure("snapshot");
        return Status::IoError;
    }
    SnapshotDocument snap{};
    const auto st = parseSnapshotDocument(blob, snap);
    if (!isOk(st)) {
        errorDetail_ = "failed to parse snapshot file " + path.filename().string()
            + ": " + std::string{statusToString(st)};
        ++parseFailureCount_;
        metrics::recordReplayParseFailure("snapshot");
        return st;
    }
    snapshot_ = snap;
    book_.applySnapshot(snapshot_);
    return Status::Ok;
}

Status SessionReplay::open(const std::filesystem::path& sessionDir) noexcept {
    const auto startedAt = std::chrono::steady_clock::now();
    reset();

    if (!std::filesystem::exists(sessionDir)) {
        errorDetail_ = "session directory does not exist: " + sessionDir.string();
        ++parseFailureCount_;
        metrics::recordReplayParseFailure("session_dir");
        status_ = Status::InvalidArgument;
        return status_;
    }

    const auto snapPath = sessionDir / "snapshot_000.json";
    if (std::filesystem::exists(snapPath)) {
        if (auto st = addSnapshotFile(snapPath); !isOk(st)) {
            status_ = st;
            return status_;
        }
    } else {
        for (const auto& entry : std::filesystem::directory_iterator(sessionDir)) {
            if (!entry.is_regular_file()) continue;
            const auto name = entry.path().filename().string();
            if (name.rfind("snapshot_", 0) == 0 && entry.path().extension() == ".json") {
                if (auto st = addSnapshotFile(entry.path()); !isOk(st)) {
                    status_ = st;
                    return status_;
                }
                break;
            }
        }
    }

    if (auto st = addTradesFile(sessionDir / "trades.jsonl"); !isOk(st)) {
        status_ = st;
        return status_;
    }
    if (auto st = addBookTickerFile(sessionDir / "bookticker.jsonl"); !isOk(st)) {
        status_ = st;
        return status_;
    }
    if (auto st = addDepthFile(sessionDir / "depth.jsonl"); !isOk(st)) {
        status_ = st;
        return status_;
    }

    finalize();
    if (isOk(status_)) {
        const auto loadNs = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - startedAt).count());
        metrics::recordReplayLoad(trades_.size() + bookTickers_.size() + depths_.size(), loadNs);
    }
    return status_;
}

}  // namespace hftrec::replay
