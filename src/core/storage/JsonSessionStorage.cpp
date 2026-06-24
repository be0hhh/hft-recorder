#include "core/storage/JsonSessionStorage.hpp"

#include <string>
#include <system_error>

#include "core/capture/JsonSerializers.hpp"

namespace hftrec::storage {

namespace {

constexpr char kJsonSessionBackendId[] = {'j', 's', 'o', 'n', '_', 's', 'e', 's', 's', 'i', 'o', 'n', '\0'};

replay::DepthRow depthRowFromSnapshot(const replay::SnapshotDocument& snapshot) {
    replay::DepthRow row{};
    row.tsNs = snapshot.tsNs;
    row.levels = snapshot.levels;
    return row;
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

Status JsonSessionSink::appendLiquidation(const replay::LiquidationRow& row) noexcept {
    return appendLiquidationLine(row, capture::renderLiquidationJsonLine(row));
}

Status JsonSessionSink::appendBookTicker(const replay::BookTickerRow& row) noexcept {
    return appendBookTickerLine(row, capture::renderBookTickerJsonLine(row));
}

Status JsonSessionSink::appendMarkPrice(const replay::MarkPriceRow& row) noexcept {
    return appendMarkPriceLine(row, capture::renderMarkPriceJsonLine(row));
}

Status JsonSessionSink::appendIndexPrice(const replay::IndexPriceRow& row) noexcept {
    return appendIndexPriceLine(row, capture::renderIndexPriceJsonLine(row));
}

Status JsonSessionSink::appendFunding(const replay::FundingRow& row) noexcept {
    return appendFundingLine(row, capture::renderFundingJsonLine(row));
}

Status JsonSessionSink::appendPriceLimit(const replay::PriceLimitRow& row) noexcept {
    return appendPriceLimitLine(row, capture::renderPriceLimitJsonLine(row));
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

Status JsonSessionSink::appendLiquidationLine(const replay::LiquidationRow&, const std::string& line) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto status = writeLine_(capture::ChannelKind::Liquidations, liquidations_, line);
    if (!isOk(status)) return status;
    liquidations_.flush();
    if (!liquidations_.good()) return Status::IoError;
    ++stats_.liquidationsTotal;
    ++stats_.version;
    return Status::Ok;
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

Status JsonSessionSink::appendMarkPriceLine(const replay::MarkPriceRow&, const std::string& line) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto status = writeLine_(capture::ChannelKind::MarkPrice, markPrice_, line);
    if (isOk(status)) {
        ++stats_.markPricesTotal;
        ++stats_.version;
    }
    return status;
}

Status JsonSessionSink::appendIndexPriceLine(const replay::IndexPriceRow&, const std::string& line) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto status = writeLine_(capture::ChannelKind::IndexPrice, indexPrice_, line);
    if (isOk(status)) {
        ++stats_.indexPricesTotal;
        ++stats_.version;
    }
    return status;
}

Status JsonSessionSink::appendFundingLine(const replay::FundingRow&, const std::string& line) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto status = writeLine_(capture::ChannelKind::Funding, funding_, line);
    if (isOk(status)) {
        ++stats_.fundingsTotal;
        ++stats_.version;
    }
    return status;
}

Status JsonSessionSink::appendPriceLimitLine(const replay::PriceLimitRow&, const std::string& line) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto status = writeLine_(capture::ChannelKind::PriceLimit, priceLimit_, line);
    if (isOk(status)) {
        ++stats_.priceLimitsTotal;
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
    if (liquidations_.is_open()) liquidations_.flush();
    if (bookTicker_.is_open()) bookTicker_.flush();
    if (markPrice_.is_open()) markPrice_.flush();
    if (indexPrice_.is_open()) indexPrice_.flush();
    if (funding_.is_open()) funding_.flush();
    if (priceLimit_.is_open()) priceLimit_.flush();
    if (depth_.is_open()) depth_.flush();
    if (depthTape_.is_open()) depthTape_.flush();
    if (depthSidecar_.is_open()) depthSidecar_.flush();
    if ((trades_.is_open() && !trades_.good())
        || (liquidations_.is_open() && !liquidations_.good())
        || (bookTicker_.is_open() && !bookTicker_.good())
        || (markPrice_.is_open() && !markPrice_.good())
        || (indexPrice_.is_open() && !indexPrice_.good())
        || (funding_.is_open() && !funding_.good())
        || (priceLimit_.is_open() && !priceLimit_.good())
        || (depth_.is_open() && !depth_.good())
        || (depthTape_.is_open() && !depthTape_.good())
        || (depthSidecar_.is_open() && !depthSidecar_.good())) {
        return Status::IoError;
    }
    return Status::Ok;
}

Status JsonSessionSink::close() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (trades_.is_open()) trades_.close();
    if (liquidations_.is_open()) liquidations_.close();
    if (bookTicker_.is_open()) bookTicker_.close();
    if (markPrice_.is_open()) markPrice_.close();
    if (indexPrice_.is_open()) indexPrice_.close();
    if (funding_.is_open()) funding_.close();
    if (priceLimit_.is_open()) priceLimit_.close();
    if (depth_.is_open()) depth_.close();
    if (depthTape_.is_open()) depthTape_.close();
    if (depthSidecar_.is_open()) depthSidecar_.close();
    sessionDir_.clear();
    return Status::Ok;
}

}  // namespace hftrec::storage
