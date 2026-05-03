#include "core/capture/CaptureCoordinator.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>


#include "api/run/RunByConfig.hpp"
#include "core/capture/CaptureCoordinatorInternal.hpp"
#include "core/capture/JsonSerializers.hpp"
#include "core/cxet_bridge/CxetCaptureBridge.hpp"
#include "core/local_exchange/LocalMarketDataBus.hpp"
#include "core/local_exchange/LocalOrderEngine.hpp"
#include "core/metrics/Metrics.hpp"
#include "primitives/composite/OrderBookDeltaRuntimeV1.hpp"
#include "primitives/composite/LiquidationEvent.hpp"
#include "primitives/composite/StreamMeta.hpp"

#include "metrics/MetricsControl.hpp"
#include "metrics/Probes.hpp"
#include "probes/TimeDelta.hpp"

namespace hftrec::capture {

namespace {

constexpr unsigned kRecorderTradesKeepaliveMs = 0u;
constexpr unsigned kRecorderQuietStreamKeepaliveMs = 30000u;

EventSequenceIds nextEventSequenceIds(std::atomic<std::uint64_t>& channelCounter,
                                      std::atomic<std::uint64_t>& ingestCounter) noexcept {
    EventSequenceIds ids{};
    ids.captureSeq = channelCounter.fetch_add(1, std::memory_order_acq_rel) + 1u;
    ids.ingestSeq = ingestCounter.fetch_add(1, std::memory_order_acq_rel) + 1u;
    return ids;
}

void recordCxetLatencyIfEnabled(cxet::metrics::LatencyProbe& probe,
                                TscTick startTsc,
                                bool captureMetrics) noexcept {
    if (captureMetrics) {
        probe.record(startTsc, cxet::probes::captureTsc());
    }
}

bool sleepBeforeRecorderRestart(std::atomic<bool>& stopRequested) noexcept {
    for (unsigned i = 0; i < 10u; ++i) {
        if (stopRequested.load(std::memory_order_acquire)) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return !stopRequested.load(std::memory_order_acquire);
}

bool symbolEqualsIgnoreCaseAscii(std::string_view lhs, std::string_view rhs) noexcept {
    if (lhs.size() != rhs.size()) return false;
    for (std::size_t i = 0; i < lhs.size(); ++i) {
        char a = lhs[i];
        char b = rhs[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a + ('a' - 'A'));
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b + ('a' - 'A'));
        if (a != b) return false;
    }
    return true;
}

replay::TradeRow makeTradeRow(const cxet_bridge::CapturedTradeRow& trade,
                              std::string_view exchange,
                              std::string_view market,
                              const EventSequenceIds& sequenceIds) noexcept {
    replay::TradeRow row{};
    row.tradeId = trade.tradeId;
    row.firstTradeId = trade.firstTradeId;
    row.lastTradeId = trade.lastTradeId;
    row.symbol = trade.symbol;
    row.exchange = std::string(exchange);
    row.market = std::string(market);
    row.tsNs = static_cast<std::int64_t>(trade.tsNs);
    row.captureSeq = static_cast<std::int64_t>(sequenceIds.captureSeq);
    row.ingestSeq = static_cast<std::int64_t>(sequenceIds.ingestSeq);
    row.priceE8 = trade.priceE8;
    row.qtyE8 = trade.qtyE8;
    row.quoteQtyE8 = trade.quoteQtyE8;
    row.side = trade.side;
    row.isBuyerMaker = trade.isBuyerMaker ? 1u : 0u;
    row.sideBuy = trade.sideBuy ? 1u : 0u;
    return row;
}


replay::LiquidationRow makeLiquidationRow(const cxet_bridge::CapturedLiquidationRow& liquidation,
                                          std::string_view exchange,
                                          std::string_view market,
                                          const EventSequenceIds& sequenceIds) noexcept {
    replay::LiquidationRow row{};
    row.symbol = liquidation.symbol;
    row.exchange = std::string(exchange);
    row.market = std::string(market);
    row.tsNs = static_cast<std::int64_t>(liquidation.tsNs);
    row.captureSeq = static_cast<std::int64_t>(sequenceIds.captureSeq);
    row.ingestSeq = static_cast<std::int64_t>(sequenceIds.ingestSeq);
    row.priceE8 = liquidation.priceE8;
    row.qtyE8 = liquidation.qtyE8;
    row.avgPriceE8 = liquidation.avgPriceE8;
    row.filledQtyE8 = liquidation.filledQtyE8;
    row.side = liquidation.side;
    row.sideBuy = liquidation.sideBuy ? 1u : 0u;
    row.orderType = liquidation.orderType;
    row.timeInForce = liquidation.timeInForce;
    row.status = liquidation.status;
    row.sourceMode = liquidation.sourceMode;
    return row;
}
replay::BookTickerRow makeBookTickerRow(const cxet_bridge::CapturedBookTickerRow& bookTicker,
                                        std::string_view exchange,
                                        std::string_view market,
                                        const EventSequenceIds& sequenceIds) noexcept {
    replay::BookTickerRow row{};
    row.symbol = bookTicker.symbol;
    row.exchange = std::string(exchange);
    row.market = std::string(market);
    row.tsNs = static_cast<std::int64_t>(bookTicker.tsNs);
    row.captureSeq = static_cast<std::int64_t>(sequenceIds.captureSeq);
    row.ingestSeq = static_cast<std::int64_t>(sequenceIds.ingestSeq);
    row.bidPriceE8 = bookTicker.bidPriceE8;
    row.bidQtyE8 = bookTicker.bidQtyE8;
    row.askPriceE8 = bookTicker.askPriceE8;
    row.askQtyE8 = bookTicker.askQtyE8;
    return row;
}

std::vector<replay::PricePair> makePricePairs(const std::vector<cxet_bridge::CapturedLevel>& levels) {
    std::vector<replay::PricePair> out;
    out.reserve(levels.size());
    for (const auto& level : levels) {
        out.push_back(replay::PricePair{level.priceI64, level.qtyI64, level.side});
    }
    return out;
}

std::vector<replay::PricePair> makeOrderbookLevels(const cxet_bridge::CapturedOrderBookRow& depth) {
    auto out = makePricePairs(depth.bids);
    const auto asks = makePricePairs(depth.asks);
    out.insert(out.end(), asks.begin(), asks.end());
    return out;
}

replay::DepthRow makeDepthRow(const cxet_bridge::CapturedOrderBookRow& depth) {
    replay::DepthRow row{};
    row.tsNs = static_cast<std::int64_t>(depth.tsNs);
    row.levels = makeOrderbookLevels(depth);
    return row;
}

replay::SnapshotDocument makeSnapshotDocument(const cxet_bridge::CapturedOrderBookRow& snapshot) {
    replay::SnapshotDocument document{};
    document.tsNs = static_cast<std::int64_t>(snapshot.tsNs);
    document.levels = makeOrderbookLevels(snapshot);
    return document;
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
        if (!isOk(jsonSink_.ensureChannelFile(ChannelKind::Trades))) {
            tradesRunning_.store(false, std::memory_order_release);
            lastError_ = "failed to create trades.jsonl";
            return Status::IoError;
        }
    }

    const auto sessionId = manifest_.sessionId;
    const auto exchange = manifest_.exchange;
    const auto market = manifest_.market;
    const auto tradesAliases = config.tradesAliases;
    tradesThread_ = std::thread([this, subscribeBuilder, sessionId, exchange, market, tradesAliases]() mutable noexcept {
        MessageBuffer payloadBuf{};
        MessageBuffer combinedPayloadBuf{};
        MessageBuffer recvBuf{};

        struct CallbackContext {
            CaptureCoordinator* self;
            std::string sessionId;
            std::string exchange;
            std::string market;
            std::vector<std::string> aliases;
            bool fatalStop{false};
        } callbackContext{this, sessionId, exchange, market, tradesAliases};

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

        while (!tradesStop_.load(std::memory_order_acquire)) {
            diagContext = {};
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
                const bool captureMetrics = cxet::metrics::shouldCaptureLatency();

                TscTick bridgeStartTsc{};
                if (captureMetrics) bridgeStartTsc = cxet::probes::captureTsc();
                const auto capturedTrade = cxet_bridge::CxetCaptureBridge::captureTrade(trade, meta);
                const auto row = makeTradeRow(capturedTrade, context->exchange, context->market, sequenceIds);
                recordCxetLatencyIfEnabled(cxet::metrics::recorderBridgeMaterialize, bridgeStartTsc, captureMetrics);

                TscTick localEngineStartTsc{};
                if (captureMetrics) localEngineStartTsc = cxet::probes::captureTsc();
                local_exchange::globalLocalOrderEngine().onTrade(capturedTrade);
                recordCxetLatencyIfEnabled(cxet::metrics::recorderLocalEngine, localEngineStartTsc, captureMetrics);

                TscTick jsonRenderStartTsc{};
                if (captureMetrics) jsonRenderStartTsc = cxet::probes::captureTsc();
                const auto jsonLine = renderTradeJsonLine(row, context->aliases);
                recordCxetLatencyIfEnabled(cxet::metrics::recorderJsonRender, jsonRenderStartTsc, captureMetrics);

                local_exchange::globalLocalMarketDataBus().publish("trades", row.symbol, jsonLine);

                TscTick eventSinkStartTsc{};
                if (captureMetrics) eventSinkStartTsc = cxet::probes::captureTsc();
                const auto liveStatus = self->liveStore_.appendTrade(row);
                const auto fileStatus = isOk(liveStatus)
                    ? self->jsonSink_.appendTradeLine(row, jsonLine)
                    : liveStatus;
                if (!isOk(fileStatus)) {
                    recordCxetLatencyIfEnabled(cxet::metrics::recorderEventSink, eventSinkStartTsc, captureMetrics);
                    metrics::recordCaptureWriteError("trades");
                    std::lock_guard<std::mutex> lock(self->stateMutex_);
                    const auto failure = cxet_bridge::CxetCaptureBridge::makeFailure(
                        cxet_bridge::CaptureFailureKind::WriteFailed,
                        "trades",
                        "failed to write trades.jsonl",
                        false);
                    self->lastError_ = failure.channel + ": " + failure.detail;
                    context->fatalStop = true;
                    return false;
                }
                recordCxetLatencyIfEnabled(cxet::metrics::recorderEventSink, eventSinkStartTsc, captureMetrics);

                TscTick recorderMetricsStartTsc{};
                if (captureMetrics) recorderMetricsStartTsc = cxet::probes::captureTsc();
                metrics::recordCaptureEvent("trades",
                                            capturedTrade.tsNs,
                                            static_cast<std::uint64_t>(jsonLine.size() + 1u),
                                            static_cast<std::uint64_t>(internal::nowNs()));
                recordCxetLatencyIfEnabled(cxet::metrics::recorderMetrics, recorderMetricsStartTsc, captureMetrics);
                return !self->tradesStop_.load(std::memory_order_acquire);
            },
            &callbackContext,
            false,
            0,
            &diagnostics,
            &tradesStop_,
            10,
            kRecorderTradesKeepaliveMs);
            (void)subscribeOk;
            if (diagContext.reconnectCount > 0u) {
                metrics::addWsReconnects("trades", static_cast<std::uint64_t>(diagContext.reconnectCount));
            }
            if (callbackContext.fatalStop || tradesStop_.load(std::memory_order_acquire)) break;
            metrics::recordWsRestart("trades");
            if (!sleepBeforeRecorderRestart(tradesStop_)) break;
        }
        tradesRunning_.store(false, std::memory_order_release);
    });

    return Status::Ok;
}

Status CaptureCoordinator::requestStopTrades() noexcept {
    tradesStop_.store(true, std::memory_order_release);
    return Status::Ok;
}

Status CaptureCoordinator::stopTrades() noexcept {
    (void)requestStopTrades();
    if (tradesThread_.joinable()) {
        tradesThread_.join();
    }
    tradesRunning_.store(false, std::memory_order_release);
    return Status::Ok;
}


Status CaptureCoordinator::startLiquidations(const CaptureConfig& config) noexcept {
    if (liquidationsThread_.joinable() && !liquidationsRunning_.load(std::memory_order_acquire)) {
        liquidationsThread_.join();
    }

    const auto sessionStatus = ensureSession(config);
    if (!isOk(sessionStatus)) return sessionStatus;

    auto subscribeBuilder = internal::makeLiquidationBuilder(config.symbols.front());
    auto aliases = config.liquidationAliases;
    if (aliases.empty()) aliases = {"price", "amount", "side", "timestamp", "avgPrice", "filledQty"};
    for (const auto* requiredAlias : {"price", "amount", "side", "timestamp", "avgPrice", "filledQty"}) {
        if (std::find(aliases.begin(), aliases.end(), requiredAlias) == aliases.end()) aliases.push_back(requiredAlias);
    }
    if (!internal::applyRequestedAliases(aliases, subscribeBuilder, lastError_)) return Status::InvalidArgument;

    bool expected = false;
    if (!liquidationsRunning_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return Status::Ok;

    liquidationsStop_.store(false, std::memory_order_release);
    {
        std::lock_guard<std::mutex> lock(stateMutex_);
        manifest_.liquidationsEnabled = true;
        if (std::find(manifest_.canonicalArtifacts.begin(), manifest_.canonicalArtifacts.end(), manifest_.liquidationsPath)
            == manifest_.canonicalArtifacts.end()) {
            manifest_.canonicalArtifacts.push_back(manifest_.liquidationsPath);
        }
        if (!isOk(jsonSink_.ensureChannelFile(ChannelKind::Liquidations))) {
            liquidationsRunning_.store(false, std::memory_order_release);
            lastError_ = "failed to create liquidations.jsonl";
            return Status::IoError;
        }
    }

    const auto exchange = manifest_.exchange;
    const auto market = manifest_.market;
    const auto symbol = config.symbols.front();
    liquidationsThread_ = std::thread([this, subscribeBuilder, exchange, market, symbol, aliases]() mutable noexcept {
        MessageBuffer payloadBuf{};
        MessageBuffer recvBuf{};
        std::FILE* debugOut = std::fopen("/dev/null", "w");
        if (debugOut == nullptr) debugOut = stdout;

        struct CallbackContext {
            CaptureCoordinator* self;
            std::string exchange;
            std::string market;
            std::string symbol;
            std::vector<std::string> aliases;
            bool fatalStop{false};
        } callbackContext{this, exchange, market, symbol, aliases};

        while (!liquidationsStop_.load(std::memory_order_acquire)) {
            const bool subscribeOk = cxet::api::runSubscribeLiquidationByConfig(
                subscribeBuilder,
            payloadBuf,
            recvBuf,
            subscribeBuilder.requestedFields(),
            [](const cxet::composite::LiquidationEvent& event, void* userData) noexcept -> bool {
                auto* context = static_cast<CallbackContext*>(userData);
                auto* self = context->self;
                if (!context->symbol.empty() && !symbolEqualsIgnoreCaseAscii(context->symbol, "all") && !symbolEqualsIgnoreCaseAscii(event.symbol.data, context->symbol)) {
                    return !self->liquidationsStop_.load(std::memory_order_acquire);
                }
                self->liquidationsCount_.fetch_add(1, std::memory_order_acq_rel);
                const auto sequenceIds = nextEventSequenceIds(self->liquidationsCaptureSeq_, self->ingestSeq_);
                const bool captureMetrics = cxet::metrics::shouldCaptureLatency();

                TscTick bridgeStartTsc{};
                if (captureMetrics) bridgeStartTsc = cxet::probes::captureTsc();
                const auto captured = cxet_bridge::CxetCaptureBridge::captureLiquidation(event);
                const auto row = makeLiquidationRow(captured, context->exchange, context->market, sequenceIds);
                recordCxetLatencyIfEnabled(cxet::metrics::recorderBridgeMaterialize, bridgeStartTsc, captureMetrics);

                TscTick jsonRenderStartTsc{};
                if (captureMetrics) jsonRenderStartTsc = cxet::probes::captureTsc();
                const auto jsonLine = renderLiquidationJsonLine(row, context->aliases);
                recordCxetLatencyIfEnabled(cxet::metrics::recorderJsonRender, jsonRenderStartTsc, captureMetrics);

                TscTick eventSinkStartTsc{};
                if (captureMetrics) eventSinkStartTsc = cxet::probes::captureTsc();
                const auto liveStatus = self->liveStore_.appendLiquidation(row);
                const auto fileStatus = isOk(liveStatus) ? self->jsonSink_.appendLiquidationLine(row, jsonLine) : liveStatus;
                if (!isOk(fileStatus)) {
                    recordCxetLatencyIfEnabled(cxet::metrics::recorderEventSink, eventSinkStartTsc, captureMetrics);
                    metrics::recordCaptureWriteError("liquidations");
                    std::lock_guard<std::mutex> lock(self->stateMutex_);
                    const auto failure = cxet_bridge::CxetCaptureBridge::makeFailure(
                        cxet_bridge::CaptureFailureKind::WriteFailed,
                        "liquidations",
                        "failed to write liquidations.jsonl",
                        false);
                    self->lastError_ = failure.channel + ": " + failure.detail;
                    context->fatalStop = true;
                    return false;
                }
                recordCxetLatencyIfEnabled(cxet::metrics::recorderEventSink, eventSinkStartTsc, captureMetrics);

                TscTick recorderMetricsStartTsc{};
                if (captureMetrics) recorderMetricsStartTsc = cxet::probes::captureTsc();
                metrics::recordCaptureEvent("liquidations",
                                            captured.tsNs,
                                            static_cast<std::uint64_t>(jsonLine.size() + 1u),
                                            static_cast<std::uint64_t>(internal::nowNs()));
                recordCxetLatencyIfEnabled(cxet::metrics::recorderMetrics, recorderMetricsStartTsc, captureMetrics);
                return !self->liquidationsStop_.load(std::memory_order_acquire);
            },
            &callbackContext,
            0,
            &liquidationsStop_,
            10,
            kRecorderQuietStreamKeepaliveMs,
            debugOut);
            (void)subscribeOk;
            if (callbackContext.fatalStop || liquidationsStop_.load(std::memory_order_acquire)) break;
            metrics::recordWsRestart("liquidations");
            if (!sleepBeforeRecorderRestart(liquidationsStop_)) break;
        }
        if (debugOut != nullptr && debugOut != stdout) std::fclose(debugOut);
        liquidationsRunning_.store(false, std::memory_order_release);
    });

    return Status::Ok;
}

Status CaptureCoordinator::requestStopLiquidations() noexcept {
    liquidationsStop_.store(true, std::memory_order_release);
    return Status::Ok;
}

Status CaptureCoordinator::stopLiquidations() noexcept {
    (void)requestStopLiquidations();
    if (liquidationsThread_.joinable()) liquidationsThread_.join();
    liquidationsRunning_.store(false, std::memory_order_release);
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
    auto bookTickerAliases = config.bookTickerAliases;
    for (const auto* requiredAlias : {"bidQty", "askQty"}) {
        if (std::find(bookTickerAliases.begin(), bookTickerAliases.end(), requiredAlias) == bookTickerAliases.end()) {
            bookTickerAliases.push_back(requiredAlias);
        }
    }
    if (!internal::applyRequestedAliases(bookTickerAliases, subscribeBuilder, lastError_)) {
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
        if (!isOk(jsonSink_.ensureChannelFile(ChannelKind::BookTicker))) {
            bookTickerRunning_.store(false, std::memory_order_release);
            lastError_ = "failed to create bookticker.jsonl";
            return Status::IoError;
        }
    }

    const auto exchange = manifest_.exchange;
    const auto market = manifest_.market;
    const auto persistedBookTickerAliases = config.bookTickerAliases;
    bookTickerThread_ = std::thread([this, subscribeBuilder, exchange, market, persistedBookTickerAliases]() mutable noexcept {
        MessageBuffer payloadBuf{};
        MessageBuffer recvBuf{};
        std::FILE* debugOut = std::fopen("/dev/null", "w");
        if (debugOut == nullptr) {
            debugOut = stdout;
        }

        struct CallbackContext {
            CaptureCoordinator* self;
            std::string exchange;
            std::string market;
            std::vector<std::string> aliases;
            bool fatalStop{false};
        } callbackContext{this, exchange, market, persistedBookTickerAliases};

        while (!bookTickerStop_.load(std::memory_order_acquire)) {
            const bool subscribeOk = cxet::api::runSubscribeBookTickerRuntimeByConfig(
                subscribeBuilder,
            payloadBuf,
            recvBuf,
            [](const cxet::composite::BookTickerRuntimeV1& bookTicker,
               const cxet::composite::StreamMeta& meta,
               const cxet::api::BookTickerRuntimeLatency* latency,
               void* userData) noexcept -> bool {
                (void)latency;
                auto* context = static_cast<CallbackContext*>(userData);
                auto* self = context->self;
                self->bookTickerCount_.fetch_add(1, std::memory_order_acq_rel);
                const auto sequenceIds = nextEventSequenceIds(self->bookTickerCaptureSeq_, self->ingestSeq_);
                const bool captureMetrics = cxet::metrics::shouldCaptureLatency();

                TscTick bridgeStartTsc{};
                if (captureMetrics) bridgeStartTsc = cxet::probes::captureTsc();
                const auto capturedBookTicker = cxet_bridge::CxetCaptureBridge::captureBookTicker(bookTicker, meta);
                const auto row = makeBookTickerRow(capturedBookTicker, context->exchange, context->market, sequenceIds);
                recordCxetLatencyIfEnabled(cxet::metrics::recorderBridgeMaterialize, bridgeStartTsc, captureMetrics);

                TscTick localEngineStartTsc{};
                if (captureMetrics) localEngineStartTsc = cxet::probes::captureTsc();
                local_exchange::globalLocalOrderEngine().onBookTicker(capturedBookTicker);
                recordCxetLatencyIfEnabled(cxet::metrics::recorderLocalEngine, localEngineStartTsc, captureMetrics);

                TscTick jsonRenderStartTsc{};
                if (captureMetrics) jsonRenderStartTsc = cxet::probes::captureTsc();
                const auto jsonLine = renderBookTickerJsonLine(row, context->aliases);
                recordCxetLatencyIfEnabled(cxet::metrics::recorderJsonRender, jsonRenderStartTsc, captureMetrics);

                local_exchange::globalLocalMarketDataBus().publish("bookticker", row.symbol, jsonLine);

                TscTick eventSinkStartTsc{};
                if (captureMetrics) eventSinkStartTsc = cxet::probes::captureTsc();
                const auto liveStatus = self->liveStore_.appendBookTicker(row);
                const auto fileStatus = isOk(liveStatus)
                    ? self->jsonSink_.appendBookTickerLine(row, jsonLine)
                    : liveStatus;
                if (!isOk(fileStatus)) {
                    recordCxetLatencyIfEnabled(cxet::metrics::recorderEventSink, eventSinkStartTsc, captureMetrics);
                    metrics::recordCaptureWriteError("bookticker");
                    std::lock_guard<std::mutex> lock(self->stateMutex_);
                    const auto failure = cxet_bridge::CxetCaptureBridge::makeFailure(
                        cxet_bridge::CaptureFailureKind::WriteFailed,
                        "bookticker",
                        "failed to write bookticker.jsonl",
                        false);
                    self->lastError_ = failure.channel + ": " + failure.detail;
                    context->fatalStop = true;
                    return false;
                }
                recordCxetLatencyIfEnabled(cxet::metrics::recorderEventSink, eventSinkStartTsc, captureMetrics);

                TscTick recorderMetricsStartTsc{};
                if (captureMetrics) recorderMetricsStartTsc = cxet::probes::captureTsc();
                metrics::recordCaptureEvent("bookticker",
                                            capturedBookTicker.tsNs,
                                            static_cast<std::uint64_t>(jsonLine.size() + 1u),
                                            static_cast<std::uint64_t>(internal::nowNs()));
                recordCxetLatencyIfEnabled(cxet::metrics::recorderMetrics, recorderMetricsStartTsc, captureMetrics);
                return !self->bookTickerStop_.load(std::memory_order_acquire);
            },
            &callbackContext,
            0,
            &bookTickerStop_,
            10,
            1000,
            debugOut);
            (void)subscribeOk;
            if (callbackContext.fatalStop || bookTickerStop_.load(std::memory_order_acquire)) break;
            metrics::recordWsRestart("bookticker");
            if (!sleepBeforeRecorderRestart(bookTickerStop_)) break;
        }

        if (debugOut != nullptr && debugOut != stdout) {
            std::fclose(debugOut);
        }

        bookTickerRunning_.store(false, std::memory_order_release);
    });

    return Status::Ok;
}

Status CaptureCoordinator::requestStopBookTicker() noexcept {
    bookTickerStop_.store(true, std::memory_order_release);
    return Status::Ok;
}

Status CaptureCoordinator::stopBookTicker() noexcept {
    (void)requestStopBookTicker();
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
    auto orderbookAliases = config.orderbookAliases;
    if (orderbookAliases.empty()) {
        orderbookAliases = {"bidPrice", "bidQty", "askPrice", "askQty", "side", "timestamp"};
    }
    for (const auto* requiredAlias : {"bidPrice", "bidQty", "askPrice", "askQty", "side", "timestamp"}) {
        if (std::find(orderbookAliases.begin(), orderbookAliases.end(), requiredAlias) == orderbookAliases.end()) {
            orderbookAliases.push_back(requiredAlias);
        }
    }
    if (!internal::applyRequestedAliases(orderbookAliases, subscribeBuilder, lastError_)) {
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
        if (!isOk(jsonSink_.ensureChannelFile(ChannelKind::DepthDelta))) {
            orderbookRunning_.store(false, std::memory_order_release);
            lastError_ = "failed to create depth.jsonl";
            return Status::IoError;
        }
    }

    const auto sessionId = manifest_.sessionId;
    orderbookThread_ = std::thread([this, subscribeBuilder, sessionId, orderbookAliases]() mutable noexcept {
        MessageBuffer payloadBuf{};
        MessageBuffer deltaRecvBuf{};

        struct CallbackContext {
            CaptureCoordinator* self;
            std::string sessionId;
            std::vector<std::string> aliases;
            bool fatalStop{false};
        } callbackContext{this, sessionId, orderbookAliases};

        while (!orderbookStop_.load(std::memory_order_acquire)) {
            const bool subscribeOk = cxet::api::runSubscribeOrderBookRuntimeByConfig(
                subscribeBuilder,
            payloadBuf,
            deltaRecvBuf,
            [](const cxet::composite::OrderBookDeltaRuntimeV1& delta,
               const cxet::composite::StreamMeta& meta,
               void* userData) noexcept -> bool {
                auto* context = static_cast<CallbackContext*>(userData);
                auto* self = context->self;
                self->depthCount_.fetch_add(1, std::memory_order_acq_rel);
                const bool captureMetrics = cxet::metrics::shouldCaptureLatency();

                TscTick bridgeStartTsc{};
                if (captureMetrics) bridgeStartTsc = cxet::probes::captureTsc();
                const auto capturedDepth = cxet_bridge::CxetCaptureBridge::captureOrderBook(delta, meta);
                const auto row = makeDepthRow(capturedDepth);
                recordCxetLatencyIfEnabled(cxet::metrics::recorderBridgeMaterialize, bridgeStartTsc, captureMetrics);

                TscTick jsonRenderStartTsc{};
                if (captureMetrics) jsonRenderStartTsc = cxet::probes::captureTsc();
                const auto jsonLine = renderDepthJsonLine(row, context->aliases);
                recordCxetLatencyIfEnabled(cxet::metrics::recorderJsonRender, jsonRenderStartTsc, captureMetrics);

                local_exchange::globalLocalMarketDataBus().publish("orderbook.delta", std::string_view{meta.symbol.data}, jsonLine);

                TscTick eventSinkStartTsc{};
                if (captureMetrics) eventSinkStartTsc = cxet::probes::captureTsc();
                const auto liveStatus = self->liveStore_.appendDepth(row);
                const auto fileStatus = isOk(liveStatus)
                    ? self->jsonSink_.appendDepthLine(row, jsonLine)
                    : liveStatus;
                if (!isOk(fileStatus)) {
                    recordCxetLatencyIfEnabled(cxet::metrics::recorderEventSink, eventSinkStartTsc, captureMetrics);
                    metrics::recordCaptureWriteError("depth");
                    std::lock_guard<std::mutex> lock(self->stateMutex_);
                    const auto failure = cxet_bridge::CxetCaptureBridge::makeFailure(
                        cxet_bridge::CaptureFailureKind::WriteFailed,
                        "depth",
                        "failed to write depth.jsonl",
                        false);
                    self->lastError_ = failure.channel + ": " + failure.detail;
                    context->fatalStop = true;
                    return false;
                }
                recordCxetLatencyIfEnabled(cxet::metrics::recorderEventSink, eventSinkStartTsc, captureMetrics);

                TscTick recorderMetricsStartTsc{};
                if (captureMetrics) recorderMetricsStartTsc = cxet::probes::captureTsc();
                metrics::recordCaptureEvent("depth",
                                            capturedDepth.tsNs,
                                            static_cast<std::uint64_t>(jsonLine.size() + 1u),
                                            static_cast<std::uint64_t>(internal::nowNs()));
                recordCxetLatencyIfEnabled(cxet::metrics::recorderMetrics, recorderMetricsStartTsc, captureMetrics);
                return !self->orderbookStop_.load(std::memory_order_acquire);
            },
            &callbackContext,
            0,
            &orderbookStop_,
            10,
            1000);
            (void)subscribeOk;
            if (callbackContext.fatalStop || orderbookStop_.load(std::memory_order_acquire)) break;
            metrics::recordWsRestart("depth");
            if (!sleepBeforeRecorderRestart(orderbookStop_)) break;
        }

        orderbookRunning_.store(false, std::memory_order_release);
    });

    return Status::Ok;
}

Status CaptureCoordinator::requestStopOrderbook() noexcept {
    orderbookStop_.store(true, std::memory_order_release);
    return Status::Ok;
}

Status CaptureCoordinator::stopOrderbook() noexcept {
    (void)requestStopOrderbook();
    if (orderbookThread_.joinable()) {
        orderbookThread_.join();
    }
    orderbookRunning_.store(false, std::memory_order_release);
    return Status::Ok;
}

void CaptureCoordinator::reapStoppedThreads() noexcept {
    if (tradesThread_.joinable() && !tradesRunning_.load(std::memory_order_acquire)) tradesThread_.join();
    if (liquidationsThread_.joinable() && !liquidationsRunning_.load(std::memory_order_acquire)) liquidationsThread_.join();
    if (bookTickerThread_.joinable() && !bookTickerRunning_.load(std::memory_order_acquire)) bookTickerThread_.join();
    if (orderbookThread_.joinable() && !orderbookRunning_.load(std::memory_order_acquire)) orderbookThread_.join();
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
    (void)snapshotKind;
    (void)source;
    (void)trustedReplayAnchor;
    const auto capturedSnapshot = cxet_bridge::CxetCaptureBridge::captureOrderBook(snapshot);
    const auto document = makeSnapshotDocument(capturedSnapshot);
    std::string snapshotPayload = renderSnapshotJson(document);
    if (!snapshotPayload.empty() && snapshotPayload.back() == '\n') snapshotPayload.pop_back();
    local_exchange::globalLocalMarketDataBus().publish("orderbook.snapshot", std::string_view{snapshot.symbol.data}, snapshotPayload);
    if (!isOk(eventSink_.appendSnapshot(document, snapshotIndex))) return Status::IoError;
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
