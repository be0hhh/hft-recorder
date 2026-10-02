#include "JsonSessionStorage.hpp"

#include <string>
#include <system_error>

#include "../../Capture/Serialization/JsonSerializers.hpp"

namespace hftrec::storage {

namespace {

constexpr char kJsonSessionBackendId[] = {'j', 's', 'o', 'n', '_', 's', 'e', 's', 's', 'i', 'o', 'n', '\0'};

replay::DepthRow depthRowFromSnapshot(const replay::SnapshotDocument& snapshot) {
    replay::DepthRow row{};
    row.tsNs = snapshot.tsNs;
    row.levels = snapshot.levels;
    return row;
}

Status closeStreamChecked(std::ofstream& stream) noexcept {
    if (!stream.is_open()) return Status::Ok;
    stream.flush();
    const bool flushed = stream.good();
    stream.close();
    return flushed && stream.good() ? Status::Ok : Status::IoError;
}

Status mergeStatus(Status lhs, Status rhs) noexcept {
    return isOk(lhs) ? rhs : lhs;
}

}  // namespace

Status JsonSessionSink::open(const std::filesystem::path& sessionDir) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    sessionDir_ = sessionDir;
    stats_ = EventStoreStats{};
    return sessionDir_.empty() ? Status::InvalidArgument : Status::Ok;
}

const char* JsonSessionSink::backendId() const noexcept {
    return kJsonSessionBackendId;
}

EventStoreStats JsonSessionSink::stats() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return stats_;
}

Status JsonSessionSink::ensureChannelFile(capture::ChannelKind channel) noexcept {
    // Historical ChannelKind numbers remain corpus truth; this current sink
    // never opens retired public-feed artifacts or an unknown channel.
    switch (channel) {
        case capture::ChannelKind::Trades:
        case capture::ChannelKind::BookTicker:
        case capture::ChannelKind::DepthTape:
        case capture::ChannelKind::DepthSidecar:
        case capture::ChannelKind::Candles:
        case capture::ChannelKind::Candles2:
            break;
        default:
            return Status::Unsupported;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (sessionDir_.empty()) return Status::InvalidArgument;
    const auto path = sessionDir_ / std::string{capture::channelJsonlRelativePath(channel)};
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return Status::IoError;
    std::ofstream stream(path, std::ios::out | std::ios::app);
    return stream.is_open() ? Status::Ok : Status::IoError;
}

Status JsonSessionSink::ensureLineStream_(capture::ChannelKind channel, std::ofstream& stream) noexcept {
    if (sessionDir_.empty()) return Status::InvalidArgument;
    if (stream.is_open()) return Status::Ok;
    const auto path = sessionDir_ / std::string{capture::channelJsonlRelativePath(channel)};
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) return Status::IoError;
    stream.open(path, std::ios::out | std::ios::app);
    return stream.is_open() ? Status::Ok : Status::IoError;
}

Status JsonSessionSink::writeLine_(capture::ChannelKind channel,
                                   std::ofstream& stream,
                                   const std::string& line) noexcept {
    if (const auto openStatus = ensureLineStream_(channel, stream); !isOk(openStatus)) return openStatus;
    stream << line << '\n';
    return stream.good() ? Status::Ok : Status::IoError;
}

Status JsonSessionSink::appendTrade(const replay::TradeRow& row) noexcept {
    return appendTradeLine(row, capture::renderTradeJsonLine(row));
}

Status JsonSessionSink::appendBookTicker(const replay::BookTickerRow& row) noexcept {
    return appendBookTickerLine(row, capture::renderBookTickerJsonLine(row));
}

Status JsonSessionSink::appendDepth(const replay::DepthRow& row) noexcept {
    return appendDepthTapeSidecarLines(row,
                                       capture::renderDepthTapeJsonLine(row),
                                       capture::renderDepthRleSidecarJsonLine(row));
}

Status JsonSessionSink::appendTradeLine(const replay::TradeRow&, const std::string& line) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto status = writeLine_(capture::ChannelKind::Trades, trades_, line);
    if (isOk(status)) {
        ++stats_.tradesTotal;
        ++stats_.version;
    }
    return status;
}

Status JsonSessionSink::appendBookTickerLine(const replay::BookTickerRow&, const std::string& line) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto status = writeLine_(capture::ChannelKind::BookTicker, bookTicker_, line);
    if (isOk(status)) {
        ++stats_.bookTickersTotal;
        ++stats_.version;
    }
    return status;
}

Status JsonSessionSink::appendDepthTapeSidecarLines(const replay::DepthRow&,
                                                    const std::string& tapeLine,
                                                    const std::string& sidecarLine) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto tapeStatus = writeLine_(capture::ChannelKind::DepthTape, depthTape_, tapeLine);
    if (!isOk(tapeStatus)) return tapeStatus;
    const auto sidecarStatus = writeLine_(capture::ChannelKind::DepthSidecar, depthSidecar_, sidecarLine);
    if (isOk(sidecarStatus)) {
        ++stats_.depthsTotal;
        ++stats_.version;
    }
    return sidecarStatus;
}

Status JsonSessionSink::appendSnapshot(const replay::SnapshotDocument& snapshot,
                                       std::uint64_t snapshotIndex) noexcept {
    (void)snapshotIndex;
    const auto row = depthRowFromSnapshot(snapshot);
    const auto tapeLine = capture::renderDepthTapeJsonLine(row);
    const auto sidecarLine = capture::renderDepthRleSidecarJsonLine(row);
    std::lock_guard<std::mutex> lock(mutex_);
    const auto tapeStatus = writeLine_(capture::ChannelKind::DepthTape, depthTape_, tapeLine);
    if (!isOk(tapeStatus)) return tapeStatus;
    const auto sidecarStatus = writeLine_(capture::ChannelKind::DepthSidecar, depthSidecar_, sidecarLine);
    if (isOk(sidecarStatus)) {
        ++stats_.depthsTotal;
        ++stats_.version;
    }
    return sidecarStatus;
}

Status JsonSessionSink::flush() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (trades_.is_open()) trades_.flush();
    if (bookTicker_.is_open()) bookTicker_.flush();
    if (depthTape_.is_open()) depthTape_.flush();
    if (depthSidecar_.is_open()) depthSidecar_.flush();
    return Status::Ok;
}

Status JsonSessionSink::close() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    Status status = Status::Ok;
    status = mergeStatus(status, closeStreamChecked(trades_));
    status = mergeStatus(status, closeStreamChecked(bookTicker_));
    status = mergeStatus(status, closeStreamChecked(depthTape_));
    status = mergeStatus(status, closeStreamChecked(depthSidecar_));
    sessionDir_.clear();
    return status;
}

}  // namespace hftrec::storage
