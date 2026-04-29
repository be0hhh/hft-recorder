#include "core/local_exchange/LocalReplayRunner.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <string_view>
#include <thread>
#include <utility>

#include "core/capture/JsonSerializers.hpp"
#include "core/local_exchange/LocalMarketDataBus.hpp"
#include "core/local_exchange/LocalOrderEngine.hpp"
#include "core/replay/SessionReplay.hpp"

namespace hftrec::local_exchange {
namespace {

constexpr std::int64_t kReplaySleepStepNs = 10000000LL;

bool waitWhilePaused(const std::atomic<bool>& stopRequested,
                     const std::atomic<bool>& paused) noexcept {
    while (paused.load(std::memory_order_acquire)) {
        if (stopRequested.load(std::memory_order_acquire)) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return !stopRequested.load(std::memory_order_acquire);
}

bool waitReplayDelay(std::int64_t deltaNs,
                     std::uint64_t speedMultiplier,
                     bool maxSpeed,
                     const std::atomic<bool>& stopRequested,
                     const std::atomic<bool>& paused) noexcept {
    if (!waitWhilePaused(stopRequested, paused)) return false;
    if (maxSpeed || deltaNs <= 0) return !stopRequested.load(std::memory_order_acquire);
    const std::uint64_t speed = speedMultiplier == 0u ? 1u : speedMultiplier;
    std::int64_t remainingNs = deltaNs / static_cast<std::int64_t>(speed);
    while (remainingNs > 0) {
        if (!waitWhilePaused(stopRequested, paused)) return false;
        const std::int64_t stepNs = std::min(remainingNs, kReplaySleepStepNs);
        std::this_thread::sleep_for(std::chrono::nanoseconds(stepNs));
        remainingNs -= stepNs;
    }
    return !stopRequested.load(std::memory_order_acquire);
}

std::string replaySymbol(const LocalReplayConfig& config,
                         const replay::SessionReplay& replay) {
    if (!config.symbolOverride.empty()) return config.symbolOverride;
    if (!replay.bookTickers().empty()) return replay.bookTickers().front().symbol;
    if (!replay.trades().empty()) return replay.trades().front().symbol;
    if (!replay.liquidations().empty()) return replay.liquidations().front().symbol;
    return {};
}

cxet_bridge::CapturedTradeRow capturedTradeFromReplay(const replay::TradeRow& row) {
    cxet_bridge::CapturedTradeRow out{};
    out.symbol = row.symbol;
    out.tradeId = row.tradeId;
    out.firstTradeId = row.firstTradeId;
    out.lastTradeId = row.lastTradeId;
    out.tsNs = static_cast<std::uint64_t>(row.tsNs);
    out.priceE8 = row.priceE8;
    out.qtyE8 = row.qtyE8;
    out.quoteQtyE8 = row.quoteQtyE8;
    out.side = row.side;
    out.isBuyerMaker = row.isBuyerMaker != 0u;
    out.sideBuy = row.sideBuy != 0u;
    return out;
}

cxet_bridge::CapturedBookTickerRow capturedBookTickerFromReplay(const replay::BookTickerRow& row) {
    cxet_bridge::CapturedBookTickerRow out{};
    out.symbol = row.symbol;
    out.tsNs = static_cast<std::uint64_t>(row.tsNs);
    out.bidPriceE8 = row.bidPriceE8;
    out.bidQtyE8 = row.bidQtyE8;
    out.askPriceE8 = row.askPriceE8;
    out.askQtyE8 = row.askQtyE8;
    out.includeBidQty = true;
    out.includeAskQty = true;
    return out;
}

void storeBoundedError(std::string& dst, std::string_view value) {
    dst.assign(value.data(), value.size());
}

}  // namespace

LocalReplayRunner::~LocalReplayRunner() {
    stop();
}

bool LocalReplayRunner::start(LocalReplayConfig config) noexcept {
    if (running()) return false;
    if (worker_.joinable()) worker_.join();
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return false;
    stopRequested_.store(false, std::memory_order_release);
    paused_.store(config.startPaused, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        lastStats_ = LocalReplayStats{};
        lastError_.clear();
    }
    try {
        worker_ = std::thread([this, config = std::move(config)]() mutable {
            LocalReplayStats stats{};
            Status status = Status::Unknown;
            if (config.mode != LocalReplayMode::Replay) {
                status = Status::Unimplemented;
                std::lock_guard<std::mutex> lock(statsMutex_);
                lastError_ = "local replay runner supports only replay mode in v1";
            } else if (config.sessionPath.empty()) {
                status = Status::InvalidArgument;
                std::lock_guard<std::mutex> lock(statsMutex_);
                lastError_ = "local replay runner session path is empty";
            } else {
                status = runReplay_(config, stats);
            }
            stats.status = status;
            stats.stopped = stopRequested_.load(std::memory_order_acquire);
            {
                std::lock_guard<std::mutex> lock(statsMutex_);
                lastStats_ = stats;
            }
            running_.store(false, std::memory_order_release);
        });
    } catch (...) {
        std::lock_guard<std::mutex> lock(statsMutex_);
        lastStats_.status = Status::Unknown;
        lastError_ = "failed to start local replay runner thread";
        running_.store(false, std::memory_order_release);
        return false;
    }
    return true;
}

Status LocalReplayRunner::runBlocking(const LocalReplayConfig& config,
                                      LocalReplayStats* outStats) noexcept {
    if (running_.exchange(true, std::memory_order_acq_rel)) return Status::InvalidArgument;
    stopRequested_.store(false, std::memory_order_release);
    paused_.store(config.startPaused, std::memory_order_release);
    LocalReplayStats stats{};
    Status status = Status::Unknown;
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        lastError_.clear();
    }
    if (config.mode != LocalReplayMode::Replay) {
        status = Status::Unimplemented;
        std::lock_guard<std::mutex> lock(statsMutex_);
        lastError_ = "local replay runner supports only replay mode in v1";
    } else if (config.sessionPath.empty()) {
        status = Status::InvalidArgument;
        std::lock_guard<std::mutex> lock(statsMutex_);
        lastError_ = "local replay runner session path is empty";
    } else {
        status = runReplay_(config, stats);
    }
    stats.status = status;
    stats.stopped = stopRequested_.load(std::memory_order_acquire);
    {
        std::lock_guard<std::mutex> lock(statsMutex_);
        lastStats_ = stats;
    }
    if (outStats != nullptr) *outStats = stats;
    running_.store(false, std::memory_order_release);
    return status;
}

void LocalReplayRunner::requestStop() noexcept {
    stopRequested_.store(true, std::memory_order_release);
}

void LocalReplayRunner::stop() noexcept {
    requestStop();
    if (worker_.joinable()) worker_.join();
}

void LocalReplayRunner::setPaused(bool paused) noexcept {
    paused_.store(paused, std::memory_order_release);
}

LocalReplayStats LocalReplayRunner::lastStats() const noexcept {
    std::lock_guard<std::mutex> lock(statsMutex_);
    return lastStats_;
}

std::string LocalReplayRunner::lastError() const {
    std::lock_guard<std::mutex> lock(statsMutex_);
    return lastError_;
}

Status LocalReplayRunner::runReplay_(const LocalReplayConfig& config,
                                     LocalReplayStats& stats) noexcept {
    replay::SessionReplay replay;
    const Status openStatus = replay.open(config.sessionPath);
    if (!isOk(openStatus)) {
        std::lock_guard<std::mutex> lock(statsMutex_);
        storeBoundedError(lastError_, replay.errorDetail().empty()
            ? std::string_view{"failed to open replay session"}
            : replay.errorDetail());
        return openStatus;
    }
    if (replay.buckets().empty()) return Status::Ok;

    const std::string fallbackSymbol = replaySymbol(config, replay);
    const bool infinite = config.repeatCount == 0u;
    const std::uint64_t repeatLimit = infinite ? 1u : config.repeatCount;
    std::uint64_t loop = 0u;
    while (!stopRequested_.load(std::memory_order_acquire) && (infinite || loop < repeatLimit)) {
        if (config.resetOrderEngineOnStart) globalLocalOrderEngine().reset();
        if (config.publishSnapshot && replay.hasSnapshot()) {
            std::string payload = capture::renderSnapshotJson(replay.snapshot());
            if (!payload.empty() && payload.back() == '\n') payload.pop_back();
            globalLocalMarketDataBus().publish("orderbook.snapshot", fallbackSymbol, payload);
            ++stats.eventsDelivered;
        }

        std::int64_t previousTsNs = replay.buckets().front().tsNs;
        for (const auto& bucket : replay.buckets()) {
            if (!waitReplayDelay(bucket.tsNs - previousTsNs,
                                 config.speedMultiplier,
                                 config.maxSpeed,
                                 stopRequested_,
                                 paused_)) {
                stats.stopped = true;
                return Status::Cancelled;
            }
            previousTsNs = bucket.tsNs;
            stats.replayTimeNs = bucket.tsNs;
            ++stats.bucketsDelivered;

            for (const auto& item : bucket.items) {
                if (stopRequested_.load(std::memory_order_acquire)) {
                    stats.stopped = true;
                    return Status::Cancelled;
                }
                if (item.kind == replay::SessionReplay::EventKind::Trade && item.rowIndex < replay.trades().size()) {
                    const replay::TradeRow& row = replay.trades()[item.rowIndex];
                    globalLocalOrderEngine().onTrade(capturedTradeFromReplay(row));
                    const std::string payload = capture::renderTradeJsonLine(row);
                    globalLocalMarketDataBus().publish("trades", row.symbol, payload);
                    ++stats.eventsDelivered;
                } else if (item.kind == replay::SessionReplay::EventKind::BookTicker && item.rowIndex < replay.bookTickers().size()) {
                    const replay::BookTickerRow& row = replay.bookTickers()[item.rowIndex];
                    globalLocalOrderEngine().onBookTicker(capturedBookTickerFromReplay(row));
                    const std::string payload = capture::renderBookTickerJsonLine(row);
                    globalLocalMarketDataBus().publish("bookticker", row.symbol, payload);
                    ++stats.eventsDelivered;
                } else if (item.kind == replay::SessionReplay::EventKind::Depth && item.rowIndex < replay.depths().size()) {
                    const replay::DepthRow& row = replay.depths()[item.rowIndex];
                    const std::string payload = capture::renderDepthJsonLine(row);
                    globalLocalMarketDataBus().publish("orderbook.delta", fallbackSymbol, payload);
                    ++stats.eventsDelivered;
                }
            }
        }
        ++stats.loopsCompleted;
        ++loop;
    }
    stats.stopped = stopRequested_.load(std::memory_order_acquire);
    return stats.stopped ? Status::Cancelled : Status::Ok;
}

}  // namespace hftrec::local_exchange