#include "core/capture/CaptureCoordinator.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <string>
#include <thread>


#include "api/run/RunByConfig.hpp"
#include "composite/level_0/GetObject.hpp"
#include "core/capture/CaptureCoordinatorInternal.hpp"
#include "core/capture/JsonSerializers.hpp"
#include "core/cxet_bridge/CxetCaptureBridge.hpp"
#include "core/local_exchange/LocalOrderEngine.hpp"
#include "core/metrics/Metrics.hpp"

namespace hftrec::capture {

namespace {

EventSequenceIds nextEventSequenceIds(std::atomic<std::uint64_t>& channelCounter,
                                      std::atomic<std::uint64_t>& ingestCounter) noexcept {
    EventSequenceIds ids{};
    ids.captureSeq = channelCounter.fetch_add(1, std::memory_order_acq_rel) + 1u;
    ids.ingestSeq = ingestCounter.fetch_add(1, std::memory_order_acq_rel) + 1u;
    return ids;
}

std::string snapshotSymbolString(const cxet::composite::OrderBookSnapshot& snapshot) {
    return std::string(snapshot.symbol.data);
}

}  // namespace

Status CaptureCoordinator::startTrades(const CaptureConfig& config) noexcept {
    if (tradesThread_.joinable() && !tradesRunning_.load(std::memory_order_acquire)) {
        tradesThread_.join();
    }

    const auto sessionStatus = ensureSession(config);
    if (!isOk(sessionStatus)) {
        return sessionStatus;
    }

    auto subscribeBuilder = internal::makeTradesBuilder(config.symbols.front());
    if (!internal::applyRequestedAliases(config.tradesAliases, subscribeBuilder, lastError_)) {
        return Status::InvalidArgument;
    }

    bool expected = false;
    if (!tradesRunning_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return Status::Ok;
    }

    tradesStop_.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        manifest_.tradesEnabled = true;
        if (std::find(manifest_.canonicalArtifacts.begin(), manifest_.canonicalArtifacts.end(), manifest_.tradesPath)
            == manifest_.canonicalArtifacts.end()) {
            manifest_.canonicalArtifacts.push_back(manifest_.tradesPath);
        }
        if (!isOk(tradesWriter_.open(ChannelKind::Trades, sessionDir_))) {
            tradesRunning_.store(false, std::memory_order_release);
            lastError_ = "failed to open trades.jsonl";
            return Status::IoError;
        }
    }

    const auto sessionId = manifest_.sessionId;
    const auto exchange = manifest_.exchange;
    const auto market = manifest_.market;
    tradesThread_ = std::thread([this, subscribeBuilder, sessionId, exchange, market]() mutable noexcept {
        MessageBuffer payloadBuf{};
        MessageBuffer combinedPayloadBuf{};
        MessageBuffer recvBuf{};

        struct CallbackContext {
            CaptureCoordinator* self;
            std::string sessionId;
            std::string exchange;
            std::string market;
        } callbackContext{this, sessionId, exchange, market};

        struct TradesDiagContext {
            bool sawConnectOk{false};
            bool connectOk{false};
            bool sawSend{false};
            std::string lastPath{};
            std::string lastPayload{};
            std::size_t recvCount{0};
            std::size_t lastRecvSize{0};
            bool lastParseOk{false};
            std::size_t reconnectCount{0};
            std::size_t readTimeoutCount{0};
            bool streamEnded{false};
        } diagContext{};

        cxet::api::TradeStreamDiagnostics diagnostics{};
        diagnostics.userData = &diagContext;
        diagnostics.onConnectOk = [](bool ok, void* userData) noexcept {
            auto* diag = static_cast<TradesDiagContext*>(userData);
            diag->sawConnectOk = true;
            diag->connectOk = ok;
        };
        diagnostics.onSend = [](const char* path,
                                std::size_t pathLen,
                                const char* payload,
                                std::size_t payloadLen,
                                void* userData) noexcept {
            auto* diag = static_cast<TradesDiagContext*>(userData);
            diag->sawSend = true;
            diag->lastPath.assign(path != nullptr ? path : "", pathLen);
            diag->lastPayload.assign(payload != nullptr ? payload : "", payloadLen);
        };
        diagnostics.onRecv = [](std::size_t size,
                                bool parseOk,
                                DurationNs,
                                void* userData) noexcept {
            auto* diag = static_cast<TradesDiagContext*>(userData);
            ++diag->recvCount;
            diag->lastRecvSize = size;
            diag->lastParseOk = parseOk;
        };
        diagnostics.onReadTimeout = [](void* userData) noexcept {
            auto* diag = static_cast<TradesDiagContext*>(userData);
            ++diag->readTimeoutCount;
        };
        diagnostics.onReconnectAttempt = [](unsigned, unsigned, void* userData) noexcept {
            auto* diag = static_cast<TradesDiagContext*>(userData);
            ++diag->reconnectCount;
        };
        diagnostics.onStreamEnd = [](void* userData) noexcept {
            auto* diag = static_cast<TradesDiagContext*>(userData);
            diag->streamEnded = true;
        };

        const bool subscribeOk = cxet::api::runSubscribeTradeRuntimeByConfigStream(
            subscribeBuilder,
            payloadBuf,
            combinedPayloadBuf,
            recvBuf,
            subscribeBuilder.requestedFields(),
            [](const cxet::composite::TradeRuntimeV1& trade,
               const cxet::composite::StreamMeta& meta,
               std::size_t,
               const cxet::api::TradeStreamLatency*,
               void* userData) noexcept -> bool {
                auto* context = static_cast<CallbackContext*>(userData);
                auto* self = context->self;
                self->tradesCount_.fetch_add(1, std::memory_order_acq_rel);
                const auto sequenceIds = nextEventSequenceIds(self->tradesCaptureSeq_, self->ingestSeq_);
                const auto capturedTrade = cxet_bridge::CxetCaptureBridge::captureTrade(trade, meta);
                local_exchange::globalLocalOrderEngine().onTrade(capturedTrade);
                const auto jsonLine = renderTradeJsonLine(capturedTrade, sequenceIds);
                if (!isOk(self->tradesWriter_.writeLine(jsonLine))) {
                    metrics::recordCaptureWriteError("trades");
                    std::lock_guard<std::mutex> lock(self->stateMutex_);
                    const auto failure = cxet_bridge::CxetCaptureBridge::makeFailure(
                        cxet_bridge::CaptureFailureKind::WriteFailed,
                        "trades",
                        "failed to write trades.jsonl",
                        false);
                    self->lastError_ = failure.channel + ": " + failure.detail;
                    return false;
                }
                metrics::recordCaptureEvent("trades",
                                            capturedTrade.tsNs,
                                            static_cast<std::uint64_t>(jsonLine.size() + 1u),
                                            static_cast<std::uint64_t>(internal::nowNs()));
                return !self->tradesStop_.load(std::memory_order_acquire);
            },
            &callbackContext,
            false,
            0,
            &diagnostics,
            &tradesStop_,
            10,
            0);

        if (!subscribeOk && !tradesStop_.load(std::memory_order_acquire)) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            if (lastError_.empty()) {
                lastError_ = "trades subscription stopped with failure";
                if (diagContext.sawConnectOk && !diagContext.connectOk) {
                    lastError_ += " | connect=false";
                }
                if (diagContext.sawSend) {
                    lastError_ += " | payload=" + diagContext.lastPayload;
                }
                if (!diagContext.lastPath.empty()) {
                    lastError_ += " | path=" + diagContext.lastPath;
                }
                if (diagContext.recvCount > 0u) {
                    lastError_ += " | recvCount=" + std::to_string(diagContext.recvCount);
                    lastError_ += " lastRecvBytes=" + std::to_string(diagContext.lastRecvSize);
                    lastError_ += " lastParseOk=" + std::string(diagContext.lastParseOk ? "true" : "false");
                }
                if (diagContext.readTimeoutCount > 0u) {
                    lastError_ += " | readTimeouts=" + std::to_string(diagContext.readTimeoutCount);
                }
                if (diagContext.reconnectCount > 0u) {
                    lastError_ += " | reconnects=" + std::to_string(diagContext.reconnectCount);
                }
            }
        }
        if (diagContext.reconnectCount > 0u) {
            metrics::addWsReconnects("trades", static_cast<std::uint64_t>(diagContext.reconnectCount));
        }
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
    if (bookTickerThread_.joinable() && !bookTickerRunning_.load(std::memory_order_acquire)) {
        bookTickerThread_.join();
    }

    const auto sessionStatus = ensureSession(config);
    if (!isOk(sessionStatus)) {
        return sessionStatus;
    }

    auto subscribeBuilder = internal::makeBookTickerBuilder(config.symbols.front());
    if (!internal::applyRequestedAliases(config.bookTickerAliases, subscribeBuilder, lastError_)) {
        return Status::InvalidArgument;
    }

    bool expected = false;
    if (!bookTickerRunning_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return Status::Ok;
    }

    bookTickerStop_.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        manifest_.bookTickerEnabled = true;
        if (std::find(manifest_.canonicalArtifacts.begin(), manifest_.canonicalArtifacts.end(), manifest_.bookTickerPath)
            == manifest_.canonicalArtifacts.end()) {
            manifest_.canonicalArtifacts.push_back(manifest_.bookTickerPath);
        }
        if (!isOk(bookTickerWriter_.open(ChannelKind::BookTicker, sessionDir_))) {
            bookTickerRunning_.store(false, std::memory_order_release);
            lastError_ = "failed to open bookticker.jsonl";
            return Status::IoError;
        }
    }

    bookTickerThread_ = std::thread([this, subscribeBuilder]() mutable noexcept {
        MessageBuffer payloadBuf{};
        MessageBuffer recvBuf{};
        std::FILE* debugOut = std::fopen("/dev/null", "w");
        if (debugOut == nullptr) {
            debugOut = stdout;
        }

        struct CallbackContext {
            CaptureCoordinator* self;
        } callbackContext{this};

        const bool subscribeOk = cxet::api::runSubscribeBookTickerRuntimeByConfig(
            subscribeBuilder,
            payloadBuf,
            recvBuf,
            [](const cxet::composite::BookTickerRuntimeV1& bookTicker,
               const cxet::composite::StreamMeta& meta,
               void* userData) noexcept -> bool {
                auto* context = static_cast<CallbackContext*>(userData);
                auto* self = context->self;
                self->bookTickerCount_.fetch_add(1, std::memory_order_acq_rel);
                const auto sequenceIds = nextEventSequenceIds(self->bookTickerCaptureSeq_, self->ingestSeq_);
                const auto capturedBookTicker = cxet_bridge::CxetCaptureBridge::captureBookTicker(bookTicker, meta);
                local_exchange::globalLocalOrderEngine().onBookTicker(capturedBookTicker);
                const auto jsonLine = renderBookTickerJsonLine(capturedBookTicker, sequenceIds);
                if (!isOk(self->bookTickerWriter_.writeLine(jsonLine))) {
                    metrics::recordCaptureWriteError("bookticker");
                    std::lock_guard<std::mutex> lock(self->stateMutex_);
                    const auto failure = cxet_bridge::CxetCaptureBridge::makeFailure(
                        cxet_bridge::CaptureFailureKind::WriteFailed,
                        "bookticker",
                        "failed to write bookticker.jsonl",
                        false);
                    self->lastError_ = failure.channel + ": " + failure.detail;
                    return false;
                }
                metrics::recordCaptureEvent("bookticker",
                                            capturedBookTicker.tsNs,
                                            static_cast<std::uint64_t>(jsonLine.size() + 1u),
                                            static_cast<std::uint64_t>(internal::nowNs()));
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
    if (orderbookThread_.joinable() && !orderbookRunning_.load(std::memory_order_acquire)) {
        orderbookThread_.join();
    }

    const auto sessionStatus = ensureSession(config);
    if (!isOk(sessionStatus)) {
        return sessionStatus;
    }

    auto subscribeBuilder = internal::makeOrderbookSubscribeBuilder(config.symbols.front());
    if (!internal::applyRequestedAliases(config.orderbookAliases, subscribeBuilder, lastError_)) {
        return Status::InvalidArgument;
    }

    bool expected = false;
    if (!orderbookRunning_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return Status::Ok;
    }

    orderbookStop_.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        manifest_.orderbookEnabled = true;
        if (std::find(manifest_.canonicalArtifacts.begin(), manifest_.canonicalArtifacts.end(), manifest_.depthPath)
            == manifest_.canonicalArtifacts.end()) {
            manifest_.canonicalArtifacts.push_back(manifest_.depthPath);
        }
        if (!isOk(depthWriter_.open(ChannelKind::DepthDelta, sessionDir_))) {
            orderbookRunning_.store(false, std::memory_order_release);
            lastError_ = "failed to open depth.jsonl";
            return Status::IoError;
        }
    }

    const auto sessionId = manifest_.sessionId;
    const auto exchange = manifest_.exchange;
    const auto market = manifest_.market;
    orderbookThread_ = std::thread([this, subscribeBuilder, sessionId, exchange, market]() mutable noexcept {
        MessageBuffer requestBuf{};
        MessageBuffer recvBuf{};
        cxet::composite::OrderBookSnapshot snapshot{};
        cxet::UnifiedRequestBuilder getBuilder{};
        CountVal depthLimit{};
        depthLimit.raw = 1000u;
        getBuilder.get()
            .object(cxet::composite::out::GetObject::Orderbook)
            .exchange(subscribeBuilder.exchange())
            .market(subscribeBuilder.market())
            .symbol(subscribeBuilder.symbol(0))
            .limit(depthLimit)
            .aliases(subscribeBuilder.requestedFields());

        if (!cxet::api::runGetOrderBookByConfig(getBuilder, requestBuf, recvBuf, &snapshot)) {
            metrics::recordSnapshotFetchFailure("depth");
            std::lock_guard<std::mutex> lock(stateMutex_);
            const auto failure = cxet_bridge::CxetCaptureBridge::makeFailure(
                cxet_bridge::CaptureFailureKind::SnapshotFetchFailed,
                "depth",
                "failed to fetch initial REST orderbook snapshot",
                true);
            lastError_ = failure.channel + ": " + failure.detail;
            orderbookRunning_.store(false, std::memory_order_release);
            return;
        }

        const auto snapshotStatus = writeSnapshotFile(snapshot, 0u, "initial", "rest_orderbook_snapshot", true);
        if (!isOk(snapshotStatus)) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastError_ = "failed to write snapshot_000.json";
            orderbookRunning_.store(false, std::memory_order_release);
            return;
        }

        MessageBuffer payloadBuf{};
        MessageBuffer deltaRecvBuf{};

        struct CallbackContext {
            CaptureCoordinator* self;
            std::string sessionId;
            std::string exchange;
            std::string market;
        } callbackContext{this, sessionId, exchange, market};

        const bool subscribeOk = cxet::api::runSubscribeOrderBookDeltaByConfig(
            subscribeBuilder,
            payloadBuf,
            deltaRecvBuf,
            [](const cxet::composite::OrderBookSnapshot& delta, void* userData) noexcept -> bool {
                auto* context = static_cast<CallbackContext*>(userData);
                auto* self = context->self;
                self->depthCount_.fetch_add(1, std::memory_order_acq_rel);
                const auto sequenceIds = nextEventSequenceIds(self->depthCaptureSeq_, self->ingestSeq_);
                const auto capturedDepth = cxet_bridge::CxetCaptureBridge::captureOrderBook(delta);
                const auto jsonLine = renderDepthJsonLine(capturedDepth, sequenceIds);
                if (!isOk(self->depthWriter_.writeLine(jsonLine))) {
                    metrics::recordCaptureWriteError("depth");
                    std::lock_guard<std::mutex> lock(self->stateMutex_);
                    const auto failure = cxet_bridge::CxetCaptureBridge::makeFailure(
                        cxet_bridge::CaptureFailureKind::WriteFailed,
                        "depth",
                        "failed to write depth.jsonl",
                        false);
                    self->lastError_ = failure.channel + ": " + failure.detail;
                    return false;
                }
                metrics::recordCaptureEvent("depth",
                                            capturedDepth.tsNs,
                                            static_cast<std::uint64_t>(jsonLine.size() + 1u),
                                            static_cast<std::uint64_t>(internal::nowNs()));
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

Status CaptureCoordinator::writeSnapshotFile(const cxet::composite::OrderBookSnapshot& snapshot,
                                             std::uint64_t snapshotIndex,
                                             std::string_view snapshotKind,
                                             std::string_view source,
                                             bool trustedReplayAnchor) noexcept {
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
    SnapshotProvenance provenance{};
    provenance.sequence = nextEventSequenceIds(snapshotCaptureSeq_, ingestSeq_);
    provenance.snapshotKind = std::string(snapshotKind);
    provenance.source = std::string(source);
    provenance.exchange = config_.exchange;
    provenance.market = config_.market;
    provenance.symbol = snapshotSymbolString(snapshot);
    provenance.sourceTsNs = static_cast<std::int64_t>(snapshot.ts.raw);
    provenance.ingestTsNs = internal::nowNs();
    provenance.anchorUpdateId = static_cast<std::uint64_t>(snapshot.updateId.raw);
    provenance.anchorFirstUpdateId = static_cast<std::uint64_t>(snapshot.firstUpdateId.raw);
    provenance.trustedReplayAnchor = trustedReplayAnchor;
    out << renderSnapshotJson(cxet_bridge::CxetCaptureBridge::captureOrderBook(snapshot), provenance);
    if (!out.good()) {
        return Status::IoError;
    }
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        if (std::find(manifest_.snapshotFiles.begin(), manifest_.snapshotFiles.end(), fileName) == manifest_.snapshotFiles.end()) {
            manifest_.snapshotFiles.push_back(fileName);
        }
        if (std::find(manifest_.canonicalArtifacts.begin(), manifest_.canonicalArtifacts.end(), fileName) == manifest_.canonicalArtifacts.end()) {
            manifest_.canonicalArtifacts.push_back(fileName);
        }
    }
    snapshotCount_.fetch_add(1, std::memory_order_acq_rel);
    return Status::Ok;
}

}  // namespace hftrec::capture
