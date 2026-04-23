#include "core/storage/JsonSessionStorage.hpp"

#include <string>

#include "core/capture/JsonSerializers.hpp"

namespace hftrec::storage {

namespace {

constexpr char kJsonSessionBackendId[] = {'j', 's', 'o', 'n', '_', 's', 'e', 's', 's', 'i', 'o', 'n', '\0'};

std::string snapshotFileName(std::uint64_t snapshotIndex) {
    if (snapshotIndex == 0u) return std::string{capture::channelFileName(capture::ChannelKind::Snapshot)};
    std::string fileName{"snapshot_"};
    if (snapshotIndex < 10u) {
        fileName += "00";
    } else if (snapshotIndex < 100u) {
        fileName += "0";
    }
    fileName += std::to_string(snapshotIndex);
    fileName += ".json";
    return fileName;
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
    std::lock_guard<std::mutex> lock(mutex_);
    if (sessionDir_.empty()) return Status::InvalidArgument;
    std::ofstream stream(sessionDir_ / std::string{capture::channelFileName(channel)}, std::ios::out | std::ios::app);
    return stream.is_open() ? Status::Ok : Status::IoError;
}

Status JsonSessionSink::ensureLineStream_(capture::ChannelKind channel, std::ofstream& stream) noexcept {
    if (sessionDir_.empty()) return Status::InvalidArgument;
    if (stream.is_open()) return Status::Ok;
    stream.open(sessionDir_ / std::string{capture::channelFileName(channel)}, std::ios::out | std::ios::app);
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
    return appendDepthLine(row, capture::renderDepthJsonLine(row));
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

Status JsonSessionSink::appendDepthLine(const replay::DepthRow&, const std::string& line) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto status = writeLine_(capture::ChannelKind::DepthDelta, depth_, line);
    if (isOk(status)) {
        ++stats_.depthsTotal;
        ++stats_.version;
    }
    return status;
}

Status JsonSessionSink::appendSnapshot(const replay::SnapshotDocument& snapshot,
                                       std::uint64_t snapshotIndex) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (sessionDir_.empty()) return Status::InvalidArgument;
    std::ofstream out(sessionDir_ / snapshotFileName(snapshotIndex), std::ios::out | std::ios::trunc);
    if (!out.is_open()) return Status::IoError;
    out << capture::renderSnapshotJson(snapshot);
    const auto status = out.good() ? Status::Ok : Status::IoError;
    if (isOk(status)) {
        ++stats_.snapshotsTotal;
        ++stats_.version;
    }
    return status;
}

Status JsonSessionSink::flush() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (trades_.is_open()) trades_.flush();
    if (bookTicker_.is_open()) bookTicker_.flush();
    if (depth_.is_open()) depth_.flush();
    if ((trades_.is_open() && !trades_.good())
        || (bookTicker_.is_open() && !bookTicker_.good())
        || (depth_.is_open() && !depth_.good())) {
        return Status::IoError;
    }
    return Status::Ok;
}

Status JsonSessionSink::close() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (trades_.is_open()) trades_.close();
    if (bookTicker_.is_open()) bookTicker_.close();
    if (depth_.is_open()) depth_.close();
    sessionDir_.clear();
    return Status::Ok;
}

}  // namespace hftrec::storage
