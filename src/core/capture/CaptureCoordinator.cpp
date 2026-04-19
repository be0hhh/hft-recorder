#include "core/capture/CaptureCoordinator.hpp"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string_view>
#include <thread>

#include "api/dispatch/BuildDispatch.hpp"
#include "api/run/RunByConfig.hpp"
#include "api/stream/CxetStream.hpp"
#include "canon/PositionAndExchange.hpp"
#include "canon/Subtypes.hpp"
#include "composite/level_0/GetObject.hpp"
#include "composite/level_0/SubscribeObject.hpp"
#include "core/capture/JsonSerializers.hpp"
#include "core/capture/SessionId.hpp"
#include "cxet.hpp"
#include "primitives/buf/Symbol.hpp"

namespace hftrec::capture {

namespace {

using namespace std::chrono_literals;

constexpr std::string_view kSupportedExchange = "binance";
constexpr std::string_view kSupportedMarket = "futures_usd";

std::once_flag gBuildDispatchOnce;

void ensureCxetInitialized() {
    std::call_once(gBuildDispatchOnce, []() {
        cxet::initBuildDispatch();
    });
}

std::int64_t nowNs() noexcept {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

long long nowSec() noexcept {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

Symbol makeSymbol(const std::string& symbolText) noexcept {
    Symbol symbol{};
    symbol.copyFrom(symbolText.c_str());
    return symbol;
}

cxet::UnifiedRequestBuilder makeTradesBuilder(const std::string& symbolText) noexcept {
    auto symbol = makeSymbol(symbolText);
    return cxet::subscribe()
        .object(cxet::composite::out::SubscribeObject::Trades)
        .exchange(canon::kExchangeIdBinance)
        .market(canon::kMarketTypeFutures)
        .symbol(symbol);
}

cxet::UnifiedRequestBuilder makeBookTickerBuilder(const std::string& symbolText) noexcept {
    auto symbol = makeSymbol(symbolText);
    return cxet::subscribe()
        .object(cxet::composite::out::SubscribeObject::BookTicker)
        .exchange(canon::kExchangeIdBinance)
        .market(canon::kMarketTypeFutures)
        .symbol(symbol);
}

cxet::UnifiedRequestBuilder makeOrderbookGetBuilder(const std::string& symbolText) noexcept {
    auto symbol = makeSymbol(symbolText);
    return cxet::get()
        .object(cxet::composite::out::GetObject::Orderbook)
        .exchange(canon::kExchangeIdBinance)
        .market(canon::kMarketTypeFutures)
        .symbol(symbol);
}

cxet::UnifiedRequestBuilder makeOrderbookSubscribeBuilder(const std::string& symbolText) noexcept {
    auto symbol = makeSymbol(symbolText);
    return cxet::subscribe()
        .object(cxet::composite::out::SubscribeObject::Orderbook)
        .exchange(canon::kExchangeIdBinance)
        .market(canon::kMarketTypeFutures)
        .symbol(symbol);
}

void sleepIfIdle() noexcept {
    std::this_thread::sleep_for(100us);
}

Status validateSupportedConfig(const CaptureConfig& config, std::string& lastError) {
    if (config.symbols.empty()) {
        lastError = "capture config must contain exactly one symbol";
        return Status::InvalidArgument;
    }
    if (config.symbols.size() != 1u) {
        lastError = "current capture path supports exactly one symbol per coordinator";
        return Status::InvalidArgument;
    }
    if (config.exchange != kSupportedExchange) {
        lastError = "current capture path supports exchange=binance only";
        return Status::InvalidArgument;
    }
    if (config.market != kSupportedMarket) {
        lastError = "current capture path supports market=futures_usd only";
        return Status::InvalidArgument;
    }
    if (config.outputDir.empty()) {
        lastError = "capture output directory must not be empty";
        return Status::InvalidArgument;
    }
    return Status::Ok;
}

}  // namespace

CaptureCoordinator::CaptureCoordinator() = default;

CaptureCoordinator::~CaptureCoordinator() {
    (void)finalizeSession();
}

Status CaptureCoordinator::ensureSession(const CaptureConfig& config) noexcept {
    std::lock_guard<std::mutex> lock(stateMutex_);
    if (sessionOpen()) {
        return Status::Ok;
    }

    if (const auto validateStatus = validateSupportedConfig(config, lastError_); !isOk(validateStatus)) {
        return validateStatus;
    }

    config_ = config;
    manifest_ = {};
    manifest_.sessionId = makeSessionId(config.exchange, config.market, config.symbols.front(), nowSec());
    manifest_.exchange = config.exchange;
    manifest_.market = config.market;
    manifest_.symbols = config.symbols;
    manifest_.selectedParentDir = config.outputDir.string();
    manifest_.startedAtNs = nowNs();
    manifest_.targetDurationSec = config.durationSec;
    manifest_.snapshotIntervalSec = config.snapshotIntervalSec;

    sessionDir_ = config.outputDir / manifest_.sessionId;
    std::error_code ec;
    if (std::filesystem::exists(sessionDir_, ec)) {
        lastError_ = "session path already exists: " + sessionDir_.string();
        return Status::IoError;
    }

    std::filesystem::create_directories(sessionDir_, ec);
    if (ec) {
        lastError_ = "failed to create session directory: " + sessionDir_.string();
        return Status::IoError;
    }

    lastError_.clear();
    return Status::Ok;
}

Status CaptureCoordinator::startTrades(const CaptureConfig& config) noexcept {
    const auto sessionStatus = ensureSession(config);
    if (!isOk(sessionStatus)) {
        return sessionStatus;
    }

    bool expected = false;
    if (!tradesRunning_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return Status::Ok;
    }

    tradesStop_.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        manifest_.tradesEnabled = true;
        if (!isOk(tradesWriter_.open(ChannelKind::Trades, sessionDir_))) {
            tradesRunning_.store(false, std::memory_order_release);
            lastError_ = "failed to open trades.jsonl";
            return Status::IoError;
        }
    }

    const auto symbolText = config.symbols.front();
    const auto sessionId = manifest_.sessionId;
    const auto exchange = manifest_.exchange;
    const auto market = manifest_.market;
    tradesThread_ = std::thread([this, symbolText, sessionId, exchange, market]() noexcept {
        ensureCxetInitialized();
        cxet::api::CxetStream<cxet::composite::TradePublic, 2048> stream{makeTradesBuilder(symbolText)};
        stream.start();

        while (!tradesStop_.load(std::memory_order_acquire)) {
            cxet::composite::TradePublic trade{};
            if (!stream.tryPop(trade)) {
                sleepIfIdle();
                continue;
            }

            const auto eventIndex = tradesCount_.fetch_add(1, std::memory_order_acq_rel) + 1u;
            const auto jsonLine = renderTradeJsonLine(sessionId, exchange, market, trade, eventIndex);
            if (!isOk(tradesWriter_.writeLine(jsonLine))) {
                std::lock_guard<std::mutex> lock(stateMutex_);
                lastError_ = "failed to write trades.jsonl";
                break;
            }
        }

        stream.stop();
        tradesRunning_.store(false, std::memory_order_release);
    });

    return Status::Ok;
}

Status CaptureCoordinator::stopTrades() noexcept {
    tradesStop_.store(true, std::memory_order_release);
    if (tradesThread_.joinable()) {
        tradesThread_.join();
    }
    tradesRunning_.store(false, std::memory_order_release);
    return Status::Ok;
}

Status CaptureCoordinator::startBookTicker(const CaptureConfig& config) noexcept {
    const auto sessionStatus = ensureSession(config);
    if (!isOk(sessionStatus)) {
        return sessionStatus;
    }

    bool expected = false;
    if (!bookTickerRunning_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return Status::Ok;
    }

    bookTickerStop_.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        manifest_.bookTickerEnabled = true;
        if (!isOk(bookTickerWriter_.open(ChannelKind::BookTicker, sessionDir_))) {
            bookTickerRunning_.store(false, std::memory_order_release);
            lastError_ = "failed to open bookticker.jsonl";
            return Status::IoError;
        }
    }

    const auto symbolText = config.symbols.front();
    const auto sessionId = manifest_.sessionId;
    const auto exchange = manifest_.exchange;
    const auto market = manifest_.market;
    bookTickerThread_ = std::thread([this, symbolText, sessionId, exchange, market]() noexcept {
        ensureCxetInitialized();
        auto builder = makeBookTickerBuilder(symbolText);
        MessageBuffer payloadBuf{};
        MessageBuffer recvBuf{};
        std::FILE* debugOut = std::fopen("/dev/null", "w");
        if (debugOut == nullptr) {
            debugOut = stdout;
        }

        struct CallbackContext {
            CaptureCoordinator* self;
            std::string sessionId;
            std::string exchange;
            std::string market;
        } callbackContext{this, sessionId, exchange, market};

        const bool subscribeOk = cxet::api::runSubscribeBookTickerByConfig(
            builder,
            payloadBuf,
            recvBuf,
            [](const cxet::composite::BookTickerData& bookTicker, void* userData) noexcept -> bool {
                auto* context = static_cast<CallbackContext*>(userData);
                auto* self = context->self;
                const auto eventIndex =
                    self->bookTickerCount_.fetch_add(1, std::memory_order_acq_rel) + 1u;
                const auto jsonLine = renderBookTickerJsonLine(
                    context->sessionId, context->exchange, context->market, bookTicker, eventIndex);
                if (!isOk(self->bookTickerWriter_.writeLine(jsonLine))) {
                    std::lock_guard<std::mutex> lock(self->stateMutex_);
                    self->lastError_ = "failed to write bookticker.jsonl";
                    return false;
                }
                return !self->bookTickerStop_.load(std::memory_order_acquire);
            },
            &callbackContext,
            0,
            &bookTickerStop_,
            10,
            0,
            debugOut);

        if (debugOut != nullptr && debugOut != stdout) {
            std::fclose(debugOut);
        }

        if (!subscribeOk && !bookTickerStop_.load(std::memory_order_acquire)) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (lastError_.empty()) {
                lastError_ = "bookticker subscription stopped with failure";
            }
        }

        bookTickerRunning_.store(false, std::memory_order_release);
    });

    return Status::Ok;
}

Status CaptureCoordinator::stopBookTicker() noexcept {
    bookTickerStop_.store(true, std::memory_order_release);
    if (bookTickerThread_.joinable()) {
        bookTickerThread_.join();
    }
    bookTickerRunning_.store(false, std::memory_order_release);
    return Status::Ok;
}

Status CaptureCoordinator::startOrderbook(const CaptureConfig& config) noexcept {
    const auto sessionStatus = ensureSession(config);
    if (!isOk(sessionStatus)) {
        return sessionStatus;
    }

    bool expected = false;
    if (!orderbookRunning_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return Status::Ok;
    }

    orderbookStop_.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        manifest_.orderbookEnabled = true;
        if (!isOk(depthWriter_.open(ChannelKind::DepthDelta, sessionDir_))) {
            orderbookRunning_.store(false, std::memory_order_release);
            lastError_ = "failed to open depth.jsonl";
            return Status::IoError;
        }
    }

    const auto symbolText = config.symbols.front();
    const auto sessionId = manifest_.sessionId;
    const auto exchange = manifest_.exchange;
    const auto market = manifest_.market;
    orderbookThread_ = std::thread([this, symbolText, sessionId, exchange, market]() noexcept {
        ensureCxetInitialized();

        MessageBuffer requestBuf{};
        MessageBuffer recvBuf{};
        cxet::composite::OrderBookSnapshot snapshot{};
        auto getBuilder = makeOrderbookGetBuilder(symbolText);

        if (!cxet::api::runGetOrderBookByConfig(getBuilder, requestBuf, recvBuf, &snapshot)) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastError_ = "failed to fetch initial REST orderbook snapshot";
            orderbookRunning_.store(false, std::memory_order_release);
            return;
        }

        const auto snapshotStatus = writeSnapshotFile(snapshot, 0u);
        if (!isOk(snapshotStatus)) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastError_ = "failed to write snapshot_000.json";
            orderbookRunning_.store(false, std::memory_order_release);
            return;
        }

        auto subscribeBuilder = makeOrderbookSubscribeBuilder(symbolText);
        MessageBuffer payloadBuf{};
        MessageBuffer deltaRecvBuf{};

        struct CallbackContext {
            CaptureCoordinator* self;
            std::string sessionId;
            std::string exchange;
            std::string market;
        } callbackContext{this, {}, {}, {}};
        callbackContext.sessionId = sessionId;
        callbackContext.exchange = exchange;
        callbackContext.market = market;

        const bool subscribeOk = cxet::api::runSubscribeOrderBookDeltaByConfig(
            subscribeBuilder,
            payloadBuf,
            deltaRecvBuf,
            [](const cxet::composite::OrderBookSnapshot& delta, void* userData) noexcept -> bool {
                auto* context = static_cast<CallbackContext*>(userData);
                auto* self = context->self;
                const auto eventIndex = self->depthCount_.fetch_add(1, std::memory_order_acq_rel) + 1u;
                const auto jsonLine = renderDepthJsonLine(
                    context->sessionId, context->exchange, context->market, delta, eventIndex);
                if (!isOk(self->depthWriter_.writeLine(jsonLine))) {
                    std::lock_guard<std::mutex> lock(self->stateMutex_);
                    self->lastError_ = "failed to write depth.jsonl";
                    return false;
                }
                return !self->orderbookStop_.load(std::memory_order_acquire);
            },
            &callbackContext,
            0,
            &orderbookStop_,
            10,
            0);

        if (!subscribeOk && !orderbookStop_.load(std::memory_order_acquire)) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (lastError_.empty()) {
                lastError_ = "orderbook delta subscription stopped with failure";
            }
        }

        orderbookRunning_.store(false, std::memory_order_release);
    });

    return Status::Ok;
}

Status CaptureCoordinator::stopOrderbook() noexcept {
    orderbookStop_.store(true, std::memory_order_release);
    if (orderbookThread_.joinable()) {
        orderbookThread_.join();
    }
    orderbookRunning_.store(false, std::memory_order_release);
    return Status::Ok;
}

Status CaptureCoordinator::finalizeSession() noexcept {
    (void)stopTrades();
    (void)stopBookTicker();
    (void)stopOrderbook();

    std::lock_guard<std::mutex> lock(stateMutex_);
    if (!sessionOpen()) {
        return Status::Ok;
    }

    manifest_.endedAtNs = nowNs();
    if (manifest_.startedAtNs > 0 && manifest_.endedAtNs >= manifest_.startedAtNs) {
        manifest_.actualDurationSec = (manifest_.endedAtNs - manifest_.startedAtNs) / 1000000000LL;
    }
    manifest_.tradesCount = tradesCount_.load(std::memory_order_relaxed);
    manifest_.bookTickerCount = bookTickerCount_.load(std::memory_order_relaxed);
    manifest_.depthCount = depthCount_.load(std::memory_order_relaxed);
    manifest_.snapshotCount = snapshotCount_.load(std::memory_order_relaxed);
    manifest_.warningSummary = lastError_;

    std::ofstream manifestStream(sessionDir_ / "manifest.json", std::ios::out | std::ios::trunc);
    if (!manifestStream.is_open()) {
        lastError_ = "failed to open manifest.json for writing";
        return Status::IoError;
    }
    manifestStream << renderManifestJson(manifest_);
    if (!manifestStream.good()) {
        lastError_ = "failed to write manifest.json";
        return Status::IoError;
    }

    (void)tradesWriter_.close();
    (void)bookTickerWriter_.close();
    (void)depthWriter_.close();
    resetSessionState();
    return Status::Ok;
}

std::string CaptureCoordinator::lastError() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return lastError_;
}

SessionManifest CaptureCoordinator::manifestCopy() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return manifest_;
}

std::filesystem::path CaptureCoordinator::sessionDirCopy() const {
    std::lock_guard<std::mutex> lock(stateMutex_);
    return sessionDir_;
}

void CaptureCoordinator::resetSessionState() noexcept {
    sessionDir_.clear();
    config_ = {};
    tradesStop_.store(false, std::memory_order_release);
    bookTickerStop_.store(false, std::memory_order_release);
    orderbookStop_.store(false, std::memory_order_release);
    tradesRunning_.store(false, std::memory_order_release);
    bookTickerRunning_.store(false, std::memory_order_release);
    orderbookRunning_.store(false, std::memory_order_release);
    tradesCount_.store(0, std::memory_order_release);
    bookTickerCount_.store(0, std::memory_order_release);
    depthCount_.store(0, std::memory_order_release);
    snapshotCount_.store(0, std::memory_order_release);
}

bool CaptureCoordinator::sessionOpen() const noexcept {
    return !sessionDir_.empty();
}

Status CaptureCoordinator::writeSnapshotFile(const cxet::composite::OrderBookSnapshot& snapshot,
                                             std::uint64_t snapshotIndex) noexcept {
    std::string fileName;
    if (snapshotIndex == 0u) {
        fileName = std::string{channelFileName(ChannelKind::Snapshot)};
    } else {
        fileName = "snapshot_";
        if (snapshotIndex < 10u) {
            fileName += "00";
        } else if (snapshotIndex < 100u) {
            fileName += "0";
        }
        fileName += std::to_string(snapshotIndex);
        fileName += ".json";
    }
    std::ofstream out(sessionDir_ / fileName, std::ios::out | std::ios::trunc);
    if (!out.is_open()) {
        return Status::IoError;
    }
    out << renderSnapshotJson(manifest_.sessionId, manifest_.exchange, manifest_.market, snapshot, snapshotIndex);
    if (!out.good()) {
        return Status::IoError;
    }
    snapshotCount_.fetch_add(1, std::memory_order_acq_rel);
    return Status::Ok;
}

}  // namespace hftrec::capture
