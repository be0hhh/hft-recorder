#include "core/capture/CaptureCoordinator.hpp"

#include "core/capture/CaptureCoordinatorRuntimeHelpers.hpp"
#include "core/capture/JsonSerializers.hpp"
#include "core/cxet_bridge/CxetCaptureBridge.hpp"
#include "core/local_exchange/LocalMarketDataBus.hpp"
#include "core/local_exchange/LocalOrderEngine.hpp"
#include "core/metrics/Metrics.hpp"
#include "hft_trader/runtime/history/orderbook/OrderBookSnapshotLoader.hpp"
#include "hft_trader/runtime/history/trades/TradeHistoryLoader.hpp"
#include "hft_trader/runtime/market/MarketDataRuntime.hpp"

#include "metrics/MetricsControl.hpp"
#include "metrics/Probes.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <thread>
#include <vector>

namespace hftrec::capture {
using namespace runtime;

namespace {

using TraderMarketDataRuntimePtr = std::unique_ptr<hft_trader::runtime::MarketDataRuntime>;

TraderMarketDataRuntimePtr makeTraderMarketDataRuntime() noexcept {
    return TraderMarketDataRuntimePtr(new (std::nothrow) hft_trader::runtime::MarketDataRuntime{});
}

}  // namespace

void CaptureCoordinator::liquidationsLoop_(CaptureConfig config) noexcept {
    auto traderMarket = makeTraderMarketDataRuntime();
    if (!traderMarket) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        lastError_ = "liquidations: failed to allocate trader market-data runtime";
        liquidationsRunning_.store(false, std::memory_order_release);
        return;
    }
    const cxet::api::market::PublicMarketDataStream streams[] = {
        cxet::api::market::PublicMarketDataStream::Liquidations,
    };
    std::string err;
    if (!applyTraderMarketDataConfig(*traderMarket, config, Span<const cxet::api::market::PublicMarketDataStream>(streams, 1u), err)) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        lastError_ = err.empty() ? "liquidations: trader market-data apply failed" : err;
        liquidationsRunning_.store(false, std::memory_order_release);
        return;
    }

    std::int64_t nextManifestFlushNs = internal::nowNs() + kRecordingManifestFlushIntervalNs;
    std::int64_t nextLifecyclePollNs = internal::nowNs() + kMarketDataLifecyclePollIntervalNs;
    while (!liquidationsStop_.load(std::memory_order_acquire)) {
        (void)flushRecordingManifestIfDue_(nextManifestFlushNs);
        std::string routeDiagnostic;
        pollMarketDataLifecycleIfDue(*traderMarket, nextLifecyclePollNs, &routeDiagnostic, "liquidations");
        if (!routeDiagnostic.empty()) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastError_ = routeDiagnostic;
        }
        hft_trader::runtime::MarketDataRuntimeEvent event{};
        if (traderMarket->pollAvailableOne(event)) {
            if (event.stream != cxet::api::market::PublicMarketDataStream::Liquidations ||
                event.status != cxet::api::market::PublicMarketDataStatus::Parsed) {
                continue;
            }
            const auto sequenceIds = nextEventSequenceIds(liquidationsCaptureSeq_, ingestSeq_);
            const auto capturedLiquidation = cxet_bridge::CxetCaptureBridge::captureLiquidation(event.liquidation);
            auto row = makeLiquidationRow(capturedLiquidation, config.exchange, config.market, sequenceIds);
            if (!internal::primaryIdentitySymbolText(config).empty()) {
                row.symbol = std::string{internal::primaryIdentitySymbolText(config)};
            }
            const auto jsonLine = renderLiquidationJsonLine(row, config.liquidationAliases);
            local_exchange::globalLocalMarketDataBus().publish("liquidations", row.symbol, jsonLine);
            const auto fileStatus = jsonSink_.appendLiquidationLine(row, jsonLine);
            if (!isOk(fileStatus)) {
                metrics::recordCaptureWriteError("liquidations");
                std::lock_guard<std::mutex> lock(stateMutex_);
                lastError_ = "liquidations: failed to write liquidations.jsonl";
                liquidationsStop_.store(true, std::memory_order_release);
                break;
            }
            (void)appendLiveLiquidation(row);
            liquidationsCount_.fetch_add(1, std::memory_order_acq_rel);
            metrics::recordCaptureEvent("liquidations",
                                        capturedLiquidation.tsNs,
                                        static_cast<std::uint64_t>(jsonLine.size() + 1u),
                                        static_cast<std::uint64_t>(internal::nowNs()));
            continue;
        }
        (void)sleepCaptureStopAware(&liquidationsStop_, 1u);
    }
    nextManifestFlushNs = 0;
    (void)flushRecordingManifestIfDue_(nextManifestFlushNs);
    traderMarket->closeAll();
    liquidationsRunning_.store(false, std::memory_order_release);
}

void CaptureCoordinator::referenceDataManagerLoop_(CaptureConfig config) noexcept {
    auto traderMarket = makeTraderMarketDataRuntime();
    if (!traderMarket) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        lastError_ = "reference: failed to allocate trader market-data runtime";
        priceLimitRunning_.store(false, std::memory_order_release);
        markPriceRunning_.store(false, std::memory_order_release);
        indexPriceRunning_.store(false, std::memory_order_release);
        fundingRunning_.store(false, std::memory_order_release);
        referenceDataRunning_.store(false, std::memory_order_release);
        return;
    }
    std::uint8_t appliedMask = 0u;
    bool haveLastFunding = false;
    replay::FundingRow lastFunding{};

    auto desiredMask = [&]() noexcept -> std::uint8_t {
        std::uint8_t mask = 0u;
        if (desiredPriceLimit_.load(std::memory_order_acquire)) mask |= 1u;
        if (desiredMarkPrice_.load(std::memory_order_acquire)) mask |= 2u;
        if (desiredIndexPrice_.load(std::memory_order_acquire)) mask |= 4u;
        if (desiredFunding_.load(std::memory_order_acquire)) mask |= 8u;
        return mask;
    };

    auto rebuildDesired = [&]() -> bool {
        std::size_t count = 0u;
        std::array<cxet::api::market::PublicMarketDataStream, 4> streams{};
        const std::uint8_t mask = desiredMask();
        if ((mask & 1u) != 0u) streams[count++] = cxet::api::market::PublicMarketDataStream::PriceLimit;
        if ((mask & 2u) != 0u) streams[count++] = cxet::api::market::PublicMarketDataStream::MarkPrice;
        if ((mask & 4u) != 0u) streams[count++] = cxet::api::market::PublicMarketDataStream::IndexPrice;
        if ((mask & 8u) != 0u) streams[count++] = cxet::api::market::PublicMarketDataStream::Funding;

        std::string err;
        const Span<const cxet::api::market::PublicMarketDataStream> desiredStreams(streams.data(), count);
        if (applyTraderMarketDataConfig(*traderMarket, config, desiredStreams, err)) return true;

        auto clearUnsupportedReferenceStream = [&](cxet::api::market::PublicMarketDataStream stream) noexcept {
            if (stream == cxet::api::market::PublicMarketDataStream::PriceLimit) {
                desiredPriceLimit_.store(false, std::memory_order_release);
                priceLimitRunning_.store(false, std::memory_order_release);
            } else if (stream == cxet::api::market::PublicMarketDataStream::MarkPrice) {
                desiredMarkPrice_.store(false, std::memory_order_release);
                markPriceRunning_.store(false, std::memory_order_release);
            } else if (stream == cxet::api::market::PublicMarketDataStream::IndexPrice) {
                desiredIndexPrice_.store(false, std::memory_order_release);
                indexPriceRunning_.store(false, std::memory_order_release);
            } else if (stream == cxet::api::market::PublicMarketDataStream::Funding) {
                desiredFunding_.store(false, std::memory_order_release);
                fundingRunning_.store(false, std::memory_order_release);
            }
        };

        if (count <= 1u) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastError_ = err.empty() ? "reference trader market-data apply failed" : err;
            if (count == 1u) clearUnsupportedReferenceStream(streams[0]);
            return false;
        }

        std::array<cxet::api::market::PublicMarketDataStream, 4> supported{};
        std::size_t supportedCount = 0u;
        std::string skipped;
        for (std::size_t i = 0u; i < count; ++i) {
            auto probe = makeTraderMarketDataRuntime();
            std::string singleErr;
            const bool supportedStream = probe &&
                applyTraderMarketDataConfig(*probe,
                                            config,
                                            Span<const cxet::api::market::PublicMarketDataStream>(&streams[i], 1u),
                                            singleErr);
            if (probe) probe->closeAll();
            if (supportedStream) {
                supported[supportedCount++] = streams[i];
            } else {
                clearUnsupportedReferenceStream(streams[i]);
                if (!skipped.empty()) skipped += ", ";
                skipped += referenceStreamName(streams[i]);
            }
        }

        if (supportedCount == 0u) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastError_ = err.empty() ? "reference trader market-data apply failed" : err;
            return false;
        }

        err.clear();
        if (!applyTraderMarketDataConfig(*traderMarket,
                                         config,
                                         Span<const cxet::api::market::PublicMarketDataStream>(supported.data(), supportedCount),
                                         err)) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastError_ = err.empty() ? "reference trader market-data apply failed" : err;
            return false;
        }
        if (!skipped.empty()) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastError_ = "reference market-data skipped unsupported stream(s): " + skipped;
        }
        return true;
    };

    auto writeMarkPrice = [&](const replay::MarkPriceRow& row) -> bool {
        const auto line = renderMarkPriceJsonLine(row);
        const auto fileStatus = jsonSink_.appendMarkPriceLine(row, line);
        if (!isOk(fileStatus)) {
            metrics::recordCaptureWriteError("mark_price");
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastError_ = "mark_price: failed to write mark_price.jsonl";
            referenceDataStop_.store(true, std::memory_order_release);
            return false;
        }
        (void)appendLiveMarkPrice(row);
        markPriceCount_.fetch_add(1, std::memory_order_acq_rel);
        metrics::recordCaptureEvent("mark_price", static_cast<std::uint64_t>(row.tsNs),
                                    static_cast<std::uint64_t>(line.size() + 1u),
                                    static_cast<std::uint64_t>(internal::nowNs()));
        return true;
    };

    auto writeIndexPrice = [&](const replay::IndexPriceRow& row) -> bool {
        const auto line = renderIndexPriceJsonLine(row);
        const auto fileStatus = jsonSink_.appendIndexPriceLine(row, line);
        if (!isOk(fileStatus)) {
            metrics::recordCaptureWriteError("index_price");
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastError_ = "index_price: failed to write index_price.jsonl";
            referenceDataStop_.store(true, std::memory_order_release);
            return false;
        }
        (void)appendLiveIndexPrice(row);
        indexPriceCount_.fetch_add(1, std::memory_order_acq_rel);
        metrics::recordCaptureEvent("index_price", static_cast<std::uint64_t>(row.tsNs),
                                    static_cast<std::uint64_t>(line.size() + 1u),
                                    static_cast<std::uint64_t>(internal::nowNs()));
        return true;
    };

    auto writeFunding = [&](const replay::FundingRow& row) -> bool {
        if (haveLastFunding && sameFundingTuple(lastFunding, row)) return true;
        const auto line = renderFundingJsonLine(row);
        const auto fileStatus = jsonSink_.appendFundingLine(row, line);
        if (!isOk(fileStatus)) {
            metrics::recordCaptureWriteError("funding");
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastError_ = "funding: failed to write funding.jsonl";
            referenceDataStop_.store(true, std::memory_order_release);
            return false;
        }
        (void)appendLiveFunding(row);
        lastFunding = row;
        haveLastFunding = true;
        fundingCount_.fetch_add(1, std::memory_order_acq_rel);
        metrics::recordCaptureEvent("funding", static_cast<std::uint64_t>(row.tsNs),
                                    static_cast<std::uint64_t>(line.size() + 1u),
                                    static_cast<std::uint64_t>(internal::nowNs()));
        return true;
    };

    auto writePriceLimit = [&](const replay::PriceLimitRow& row) -> bool {
        const auto line = renderPriceLimitJsonLine(row);
        const auto fileStatus = jsonSink_.appendPriceLimitLine(row, line);
        if (!isOk(fileStatus)) {
            metrics::recordCaptureWriteError("price_limit");
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastError_ = "price_limit: failed to write price_limit.jsonl";
            referenceDataStop_.store(true, std::memory_order_release);
            return false;
        }
        (void)appendLivePriceLimit(row);
        priceLimitCount_.fetch_add(1, std::memory_order_acq_rel);
        metrics::recordCaptureEvent("price_limit", static_cast<std::uint64_t>(row.tsNs),
                                    static_cast<std::uint64_t>(line.size() + 1u),
                                    static_cast<std::uint64_t>(internal::nowNs()));
        return true;
    };

    std::int64_t nextLifecyclePollNs = internal::nowNs() + kMarketDataLifecyclePollIntervalNs;
    while (!referenceDataStop_.load(std::memory_order_acquire)) {
        const std::uint8_t mask = desiredMask();
        if (mask == 0u) break;
        if (mask != appliedMask) {
            if (!rebuildDesired()) break;
            appliedMask = mask;
        }
        std::string routeDiagnostic;
        pollMarketDataLifecycleIfDue(*traderMarket, nextLifecyclePollNs, &routeDiagnostic, "reference");
        if (!routeDiagnostic.empty()) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastError_ = routeDiagnostic;
        }

        hft_trader::runtime::MarketDataRuntimeEvent event{};
        if (!traderMarket->pollAvailableOne(event)) {
            (void)sleepCaptureStopAware(&referenceDataStop_, 1u);
            continue;
        }
        if (event.status != cxet::api::market::PublicMarketDataStatus::Parsed) continue;
        if (event.stream == cxet::api::market::PublicMarketDataStream::MarkPrice &&
            !writeMarkPrice(makeMarkPriceRow(event.markPrice))) break;
        if (event.stream == cxet::api::market::PublicMarketDataStream::IndexPrice &&
            !writeIndexPrice(makeIndexPriceRow(event.indexPrice))) break;
        if (event.stream == cxet::api::market::PublicMarketDataStream::Funding &&
            !writeFunding(makeFundingRow(event.funding))) break;
        if (event.stream == cxet::api::market::PublicMarketDataStream::PriceLimit &&
            !writePriceLimit(makePriceLimitRow(event.priceLimit))) break;
    }

    traderMarket->closeAll();
    priceLimitRunning_.store(false, std::memory_order_release);
    markPriceRunning_.store(false, std::memory_order_release);
    indexPriceRunning_.store(false, std::memory_order_release);
    fundingRunning_.store(false, std::memory_order_release);
    referenceDataRunning_.store(false, std::memory_order_release);
}

void CaptureCoordinator::marketDataManagerLoop_(CaptureConfig config) noexcept {
    auto traderMarket = makeTraderMarketDataRuntime();
    if (!traderMarket) {
        std::lock_guard<std::mutex> lock(stateMutex_);
        lastError_ = "market-data: failed to allocate trader market-data runtime";
        tradesRunning_.store(false, std::memory_order_release);
        bookTickerRunning_.store(false, std::memory_order_release);
        orderbookRunning_.store(false, std::memory_order_release);
        marketDataRunning_.store(false, std::memory_order_release);
        return;
    }
    std::uint8_t appliedMask = 0u;
    bool initialOrderbookSeedAttempted = depthCount_.load(std::memory_order_acquire) != 0u;
    std::vector<replay::PricePair> bitgetPreviousOrderbookLevels{};
    TradesHistoryWarmupState tradesWarmup{};
    std::thread tradesWarmupThread{};
    std::vector<replay::TradeRow> bufferedLiveTradeRows{};
    bool tradesWarmupFlushed = false;
    std::size_t tradesWarmupDisplayedRows = 0u;

    auto rebuildDesired = [&]() -> bool {
        const bool wantTrades = desiredTrades_.load(std::memory_order_acquire);
        const bool wantBookTicker = desiredBookTicker_.load(std::memory_order_acquire);
        const bool wantOrderbook = desiredOrderbook_.load(std::memory_order_acquire);
        std::array<cxet::api::market::PublicMarketDataStream, 3> streams{};
        std::size_t streamCount = 0u;
        if (wantTrades) streams[streamCount++] = cxet::api::market::PublicMarketDataStream::Trades;
        if (wantBookTicker) streams[streamCount++] = cxet::api::market::PublicMarketDataStream::BookTicker;
        if (wantOrderbook) streams[streamCount++] = cxet::api::market::PublicMarketDataStream::Orderbook;

        std::string err;
        if (!applyTraderMarketDataConfig(*traderMarket,
                                         config,
                                         Span<const cxet::api::market::PublicMarketDataStream>(streams.data(), streamCount),
                                         err)) {
            auto clearUnsupportedMarketStream = [&](cxet::api::market::PublicMarketDataStream stream) noexcept {
                if (stream == cxet::api::market::PublicMarketDataStream::Trades) {
                    desiredTrades_.store(false, std::memory_order_release);
                    tradesRunning_.store(false, std::memory_order_release);
                } else if (stream == cxet::api::market::PublicMarketDataStream::BookTicker) {
                    desiredBookTicker_.store(false, std::memory_order_release);
                    bookTickerRunning_.store(false, std::memory_order_release);
                } else if (stream == cxet::api::market::PublicMarketDataStream::Orderbook) {
                    desiredOrderbook_.store(false, std::memory_order_release);
                    orderbookRunning_.store(false, std::memory_order_release);
                }
            };

            if (streamCount > 1u) {
                std::array<cxet::api::market::PublicMarketDataStream, 3> supported{};
                std::size_t supportedCount = 0u;
                std::string skipped;
                for (std::size_t i = 0u; i < streamCount; ++i) {
                    auto probe = makeTraderMarketDataRuntime();
                    std::string singleErr;
                    const bool supportedStream = probe &&
                        applyTraderMarketDataConfig(*probe,
                                                    config,
                                                    Span<const cxet::api::market::PublicMarketDataStream>(&streams[i], 1u),
                                                    singleErr);
                    if (probe) probe->closeAll();
                    if (supportedStream) {
                        supported[supportedCount++] = streams[i];
                    } else {
                        clearUnsupportedMarketStream(streams[i]);
                        if (!skipped.empty()) skipped += ", ";
                        skipped += marketStreamName(streams[i]);
                        if (!singleErr.empty()) {
                            skipped += "(";
                            skipped += singleErr;
                            skipped += ")";
                        }
                    }
                }

                if (supportedCount != 0u) {
                    err.clear();
                    if (applyTraderMarketDataConfig(
                            *traderMarket,
                            config,
                            Span<const cxet::api::market::PublicMarketDataStream>(supported.data(), supportedCount),
                            err)) {
                        if (!skipped.empty()) {
                            std::lock_guard<std::mutex> lock(stateMutex_);
                            lastError_ = "market-data skipped unsupported stream(s): " + skipped;
                        }
                        return true;
                    }
                }
            }

            for (std::size_t i = 0u; i < streamCount; ++i) clearUnsupportedMarketStream(streams[i]);
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastError_ = err.empty() ? "market-data: trader apply failed" : err;
            return false;
        }

        if (wantOrderbook) {
            if (!initialOrderbookSeedAttempted) {
                initialOrderbookSeedAttempted = true;
                cxet::composite::OrderBookSnapshot initialSnapshot{};
                if (!shouldFetchInitialOrderbookSnapshot(config)) {
                    // Bitget has no REST orderbook route in the trader loader here; use WS orderbook only.
                } else {
                    Symbol symbol{};
                    copySymbolFromText(symbol, internal::primaryRouteSymbolText(config));
                    MessageBuffer requestBuf{};
                    MessageBuffer recvBuf{};
                    const bool snapshotOk = hft_trader::runtime::orderbook::loadOrderBookSnapshotForVenue(
                        makeTraderVenueConfig(config),
                        symbol,
                        initialSnapshot,
                        requestBuf,
                        recvBuf);
                    if (!snapshotOk) {
                        metrics::recordSnapshotFetchFailure("snapshot");
                        std::lock_guard<std::mutex> lock(stateMutex_);
                        lastError_ = "orderbook: initial trader snapshot fetch failed; continuing with WS depth";
                    } else {
                        for (std::size_t i = 0u; i < traderMarket->channelCount(); ++i) {
                            const auto* channel = traderMarket->channelAt(i);
                            if (channel && channel->stream == cxet::api::market::PublicMarketDataStream::Orderbook) {
                                (void)traderMarket->seedOrderBookSnapshot(i, initialSnapshot);
                                break;
                            }
                        }
                        const auto capturedSnapshot = cxet_bridge::CxetCaptureBridge::captureOrderBook(initialSnapshot);
                        auto row = makeDepthRow(capturedSnapshot);
                        if (row.tsNs <= 0) row.tsNs = internal::nowNs();
                        const auto tapeLine = renderDepthTapeJsonLine(row);
                        const auto sidecarLine = renderDepthRleSidecarJsonLine(row);
                        const auto fileStatus = jsonSink_.appendDepthTapeSidecarLines(row, tapeLine, sidecarLine);
                        if (!isOk(fileStatus)) {
                            metrics::recordCaptureWriteError("depth");
                            std::lock_guard<std::mutex> lock(stateMutex_);
                            lastError_ = "orderbook: failed to write initial trader snapshot into depth_tape/depth_sidecar";
                            return false;
                        }
                        (void)appendLiveDepth(row);
                        depthCount_.fetch_add(1, std::memory_order_acq_rel);
                        metrics::recordCaptureEvent("depth", static_cast<std::uint64_t>(row.tsNs),
                                                    static_cast<std::uint64_t>(tapeLine.size() + sidecarLine.size() + 2u),
                                                    static_cast<std::uint64_t>(internal::nowNs()));
                    }
                }
            }
        }
        return true;
    };

    auto appendTradeRowToLiveOnly = [&](const replay::TradeRow& row, const std::string& jsonLine) -> bool {
        static constexpr char kTradesText[] = {'t', 'r', 'a', 'd', 'e', 's', '\0'};
        local_exchange::globalLocalMarketDataBus().publish(kTradesText, row.symbol, jsonLine);
        const auto liveStatus = appendLiveTrade(row);
        if (!isOk(liveStatus)) {
            metrics::recordCaptureWriteError(kTradesText);
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastError_ = kTradesText;
            marketDataStop_.store(true, std::memory_order_release);
            return false;
        }
        tradesCount_.fetch_add(1, std::memory_order_acq_rel);
        metrics::recordCaptureEvent(kTradesText,
                                    static_cast<std::uint64_t>(row.tsNs > 0 ? row.tsNs : 0),
                                    static_cast<std::uint64_t>(jsonLine.size() + 1u),
                                    static_cast<std::uint64_t>(internal::nowNs()));
        return true;
    };

    auto appendTradeRowToLiveCacheOnly = [&](const replay::TradeRow& row) -> bool {
        static constexpr char kTradesText[] = {'t', 'r', 'a', 'd', 'e', 's', '\0'};
        const auto liveStatus = appendLiveTrade(row);
        if (!isOk(liveStatus)) {
            metrics::recordCaptureWriteError(kTradesText);
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastError_ = kTradesText;
            marketDataStop_.store(true, std::memory_order_release);
            return false;
        }
        const auto jsonLine = renderTradeJsonLine(row, config.tradesAliases);
        tradesCount_.fetch_add(1, std::memory_order_acq_rel);
        metrics::recordCaptureEvent(kTradesText,
                                    static_cast<std::uint64_t>(row.tsNs > 0 ? row.tsNs : 0),
                                    static_cast<std::uint64_t>(jsonLine.size() + 1u),
                                    static_cast<std::uint64_t>(internal::nowNs()));
        return true;
    };

    auto appendTradeRowToFileOnly = [&](const replay::TradeRow& row) -> bool {
        static constexpr char kTradesText[] = {'t', 'r', 'a', 'd', 'e', 's', '\0'};
        const auto jsonLine = renderTradeJsonLine(row, config.tradesAliases);
        const auto fileStatus = jsonSink_.appendTradeLine(row, jsonLine);
        if (!isOk(fileStatus)) {
            metrics::recordCaptureWriteError(kTradesText);
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastError_ = kTradesText;
            marketDataStop_.store(true, std::memory_order_release);
            return false;
        }
        return true;
    };

    auto startTradesWarmupIfNeeded = [&]() {
        if (tradesWarmup.started.load(std::memory_order_acquire)) return;
        if (!desiredTrades_.load(std::memory_order_acquire)) return;
        if (config.tradesHistoryWarmupSec <= 0) return;
        const auto warmupSec = std::min<std::int64_t>(config.tradesHistoryWarmupSec, kTradesHistoryWarmupMaxSec);
        const auto warmupNs = warmupSec * 1000000000LL;
        const auto endNs = internal::nowNs();
        const auto startNs = endNs > warmupNs ? endNs - warmupNs : 0;
        tradesWarmup.requestedStartNs = startNs;
        tradesWarmup.requestedEndNs = endNs;
        tradesWarmup.started.store(true, std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            manifest_.tradesHistoryWarmupSec = warmupSec;
            manifest_.tradesHistoryRequestedStartNs = startNs;
            manifest_.tradesHistoryRequestedEndNs = endNs;
            manifest_.tradesHistoryStatus = "running";
        }
        tradesWarmupThread = std::thread([this, &tradesWarmup, config, startNs, endNs]() noexcept {
            Symbol symbol{};
            if (!internal::primaryRouteSymbolText(config).empty()) {
                copySymbolFromText(symbol, internal::primaryRouteSymbolText(config));
            }
            TimeNs startTime{};
            TimeNs endTime{};
            CountVal pageLimit{};
            startTime.raw = static_cast<std::uint64_t>(startNs > 0 ? startNs : 0);
            endTime.raw = static_cast<std::uint64_t>(endNs > 0 ? endNs : 0);
            pageLimit.raw = kTradesHistoryWarmupPageLimit;

            TradesHistorySinkContext sinkContext{};
            sinkContext.state = &tradesWarmup;
            sinkContext.tradesCaptureSeq = &tradesCaptureSeq_;
            sinkContext.ingestSeq = &ingestSeq_;
            sinkContext.exchange = config.exchange;
            sinkContext.market = config.market;
            sinkContext.identitySymbol = std::string{internal::primaryIdentitySymbolText(config)};
            sinkContext.maxRows = kTradesHistoryWarmupTargetRows;

            MessageBuffer requestBuf{};
            MessageBuffer recvBuf{};
            cxet::api::trades::HistoricalTradesResult result{};
            bool ok = hft_trader::runtime::trades::loadPublicTradeHistoryForVenue(
                makeTraderVenueConfig(config),
                symbol,
                startTime,
                endTime,
                pageLimit,
                true,
                appendHistoricalTradesToWarmup,
                &sinkContext,
                result,
                requestBuf,
                recvBuf);
            if (sinkContext.hitRowLimit) {
                std::lock_guard<std::mutex> lock(tradesWarmup.mutex);
                result.rowsTotal.raw = static_cast<std::uint64_t>(tradesWarmup.historyRows.size());
                result.status = cxet::api::trades::HistoricalTradesStatus::Ok;
                ok = true;
            }
            tradesWarmup.result = result;
            if (!ok) {
                tradesWarmup.error = std::string{"trades history warmup failed: "}
                    + historicalTradesStatusName(result.status);
            }
            tradesWarmup.ok.store(ok, std::memory_order_release);
            tradesWarmup.done.store(true, std::memory_order_release);
        });
    };

    auto drainTradesWarmupPages = [&]() -> bool {
        if (!tradesWarmup.started.load(std::memory_order_acquire)) return true;
        if (tradesWarmupFlushed) return true;
        std::vector<replay::TradeRow> rows;
        {
            std::lock_guard<std::mutex> lock(tradesWarmup.mutex);
            if (tradesWarmupDisplayedRows >= tradesWarmup.historyRows.size()) return true;
            rows.insert(rows.end(),
                        tradesWarmup.historyRows.begin() + static_cast<std::ptrdiff_t>(tradesWarmupDisplayedRows),
                        tradesWarmup.historyRows.end());
            tradesWarmupDisplayedRows = tradesWarmup.historyRows.size();
        }
        for (const auto& row : rows) {
            if (!appendTradeRowToLiveCacheOnly(row)) return false;
        }
        return true;
    };

    auto flushTradesWarmupIfReady = [&]() -> bool {
        if (tradesWarmupFlushed) return true;
        if (!tradesWarmup.started.load(std::memory_order_acquire)) return true;
        if (!tradesWarmup.done.load(std::memory_order_acquire)) return true;
        if (tradesWarmupThread.joinable()) tradesWarmupThread.join();
        if (!drainTradesWarmupPages()) return false;

        std::vector<replay::TradeRow> historyRows;
        {
            std::lock_guard<std::mutex> lock(tradesWarmup.mutex);
            historyRows = std::move(tradesWarmup.historyRows);
        }
        struct PendingTradeRow {
            replay::TradeRow row{};
            bool publish{false};
        };
        std::vector<PendingTradeRow> pending;
        pending.reserve(historyRows.size() + bufferedLiveTradeRows.size());
        for (const auto& row : historyRows) pending.push_back(PendingTradeRow{row, false});
        for (const auto& row : bufferedLiveTradeRows) pending.push_back(PendingTradeRow{row, true});
        std::sort(pending.begin(), pending.end(), [](const PendingTradeRow& lhs, const PendingTradeRow& rhs) noexcept {
            if (tradeLessByEventTime(lhs.row, rhs.row)) return true;
            if (tradeLessByEventTime(rhs.row, lhs.row)) return false;
            return !lhs.publish && rhs.publish;
        });

        bool havePrevious = false;
        replay::TradeRow previous{};
        for (const auto& pendingRow : pending) {
            if (havePrevious && sameTradeEvent(previous, pendingRow.row)) {
                if (pendingRow.publish) tradesCount_.fetch_sub(1, std::memory_order_acq_rel);
                continue;
            }
            if (!appendTradeRowToFileOnly(pendingRow.row)) return false;
            previous = pendingRow.row;
            havePrevious = true;
        }
        bufferedLiveTradeRows.clear();
        {
            std::lock_guard<std::mutex> lock(stateMutex_);
            manifest_.tradesHistoryWarmupSec = std::min<std::int64_t>(config.tradesHistoryWarmupSec, kTradesHistoryWarmupMaxSec);
            manifest_.tradesHistoryRequestedStartNs = tradesWarmup.requestedStartNs;
            manifest_.tradesHistoryRequestedEndNs = tradesWarmup.requestedEndNs;
            manifest_.tradesHistoryRows = static_cast<std::uint64_t>(historyRows.size());
            manifest_.tradesHistoryRequests = static_cast<std::uint64_t>(tradesWarmup.result.requestsTotal.raw);
            manifest_.tradesHistoryFeedKind = historicalTradeFeedKindName(tradesWarmup.result.feedKind);
            manifest_.tradesHistoryStatus = historicalTradesStatusName(tradesWarmup.result.status);
            if (!tradesWarmup.ok.load(std::memory_order_acquire)) {
                lastError_ = tradesWarmup.error;
            }
        }
        tradesWarmupFlushed = true;
        return true;
    };

    auto desiredMask = [&]() noexcept -> std::uint8_t {
        std::uint8_t mask = 0u;
        if (desiredTrades_.load(std::memory_order_acquire)) mask |= 1u;
        if (desiredBookTicker_.load(std::memory_order_acquire)) mask |= 2u;
        if (desiredOrderbook_.load(std::memory_order_acquire)) mask |= 4u;
        return mask;
    };

    std::int64_t nextManifestFlushNs = internal::nowNs() + kRecordingManifestFlushIntervalNs;
    std::int64_t nextLifecyclePollNs = internal::nowNs() + kMarketDataLifecyclePollIntervalNs;
    while (!marketDataStop_.load(std::memory_order_acquire)) {
        (void)flushRecordingManifestIfDue_(nextManifestFlushNs);
        const std::uint8_t mask = desiredMask();
        if (mask == 0u) break;
        if (mask != appliedMask) {
            if (!rebuildDesired()) break;
            appliedMask = mask;
            startTradesWarmupIfNeeded();
        }
        std::string routeDiagnostic;
        pollMarketDataLifecycleIfDue(*traderMarket, nextLifecyclePollNs, &routeDiagnostic, "market-data");
        if (!routeDiagnostic.empty()) {
            std::lock_guard<std::mutex> lock(stateMutex_);
            lastError_ = routeDiagnostic;
        }
        if (!drainTradesWarmupPages()) break;
        if (!flushTradesWarmupIfReady()) break;

        hft_trader::runtime::MarketDataRuntimeEvent event{};
        if (!traderMarket->pollAvailableOne(event)) {
            (void)sleepCaptureStopAware(&marketDataStop_, 1u);
            continue;
        }
        if (event.status != cxet::api::market::PublicMarketDataStatus::Parsed) continue;
        const bool captureMetrics = cxet::metrics::shouldCaptureLatency();
        const auto meta = streamMetaFromTraderEvent(event, internal::primaryIdentitySymbolText(config));
        if (event.stream == cxet::api::market::PublicMarketDataStream::Trades) {
                const auto sequenceIds = nextEventSequenceIds(tradesCaptureSeq_, ingestSeq_);
                TscTick bridgeStartTsc{};
                if (captureMetrics) bridgeStartTsc = cxet::probes::captureTsc();
                const auto capturedTrade = cxet_bridge::CxetCaptureBridge::captureTrade(event.trade, meta);
                const auto row = makeTradeRow(capturedTrade, config.exchange, config.market, sequenceIds);
                recordCxetLatencyIfEnabled(cxet::metrics::recorderBridgeMaterialize, bridgeStartTsc, captureMetrics);
                TscTick localEngineStartTsc{};
                if (captureMetrics) localEngineStartTsc = cxet::probes::captureTsc();
                local_exchange::globalLocalOrderEngine().onTrade(capturedTrade);
                recordCxetLatencyIfEnabled(cxet::metrics::recorderLocalEngine, localEngineStartTsc, captureMetrics);
                TscTick jsonRenderStartTsc{};
                if (captureMetrics) jsonRenderStartTsc = cxet::probes::captureTsc();
                const auto jsonLine = renderTradeJsonLine(row, config.tradesAliases);
                recordCxetLatencyIfEnabled(cxet::metrics::recorderJsonRender, jsonRenderStartTsc, captureMetrics);
                if (tradesWarmup.started.load(std::memory_order_acquire)
                    && !tradesWarmupFlushed) {
                    bufferedLiveTradeRows.push_back(row);
                    if (!appendTradeRowToLiveOnly(row, jsonLine)) break;
                    continue;
                }
                local_exchange::globalLocalMarketDataBus().publish("trades", row.symbol, jsonLine);
                TscTick eventSinkStartTsc{};
                if (captureMetrics) eventSinkStartTsc = cxet::probes::captureTsc();
                const auto fileStatus = jsonSink_.appendTradeLine(row, jsonLine);
                if (!isOk(fileStatus)) {
                    recordCxetLatencyIfEnabled(cxet::metrics::recorderEventSink, eventSinkStartTsc, captureMetrics);
                    metrics::recordCaptureWriteError("trades");
                    std::lock_guard<std::mutex> lock(stateMutex_);
                    lastError_ = "trades: failed to write trades.jsonl";
                    marketDataStop_.store(true, std::memory_order_release);
                    break;
                }
                (void)appendLiveTrade(row);
                tradesCount_.fetch_add(1, std::memory_order_acq_rel);
                recordCxetLatencyIfEnabled(cxet::metrics::recorderEventSink, eventSinkStartTsc, captureMetrics);
                metrics::recordCaptureEvent("trades", capturedTrade.tsNs,
                                            static_cast<std::uint64_t>(jsonLine.size() + 1u),
                                            static_cast<std::uint64_t>(internal::nowNs()));
        } else if (event.stream == cxet::api::market::PublicMarketDataStream::BookTicker) {
                bookTickerCount_.fetch_add(1, std::memory_order_acq_rel);
                const auto sequenceIds = nextEventSequenceIds(bookTickerCaptureSeq_, ingestSeq_);
                TscTick bridgeStartTsc{};
                if (captureMetrics) bridgeStartTsc = cxet::probes::captureTsc();
                const auto capturedBookTicker = cxet_bridge::CxetCaptureBridge::captureBookTicker(event.bookTicker, meta);
                const auto row = makeBookTickerRow(capturedBookTicker, config.exchange, config.market, sequenceIds);
                recordCxetLatencyIfEnabled(cxet::metrics::recorderBridgeMaterialize, bridgeStartTsc, captureMetrics);
                TscTick localEngineStartTsc{};
                if (captureMetrics) localEngineStartTsc = cxet::probes::captureTsc();
                local_exchange::globalLocalOrderEngine().onBookTicker(capturedBookTicker);
                recordCxetLatencyIfEnabled(cxet::metrics::recorderLocalEngine, localEngineStartTsc, captureMetrics);
                auto aliases = config.bookTickerAliases;
                for (const auto* requiredAlias : {"bidQty", "askQty"}) {
                    if (std::find(aliases.begin(), aliases.end(), requiredAlias) == aliases.end()) aliases.push_back(requiredAlias);
                }
                TscTick jsonRenderStartTsc{};
                if (captureMetrics) jsonRenderStartTsc = cxet::probes::captureTsc();
                const auto jsonLine = renderBookTickerJsonLine(row, aliases);
                recordCxetLatencyIfEnabled(cxet::metrics::recorderJsonRender, jsonRenderStartTsc, captureMetrics);
                local_exchange::globalLocalMarketDataBus().publish("bookticker", row.symbol, jsonLine);
                TscTick eventSinkStartTsc{};
                if (captureMetrics) eventSinkStartTsc = cxet::probes::captureTsc();
                const auto fileStatus = jsonSink_.appendBookTickerLine(row, jsonLine);
                if (!isOk(fileStatus)) {
                    recordCxetLatencyIfEnabled(cxet::metrics::recorderEventSink, eventSinkStartTsc, captureMetrics);
                    metrics::recordCaptureWriteError("bookticker");
                    std::lock_guard<std::mutex> lock(stateMutex_);
                    lastError_ = "bookticker: failed to write bookticker.jsonl";
                    marketDataStop_.store(true, std::memory_order_release);
                    break;
                }
                (void)appendLiveBookTicker(row);
                recordCxetLatencyIfEnabled(cxet::metrics::recorderEventSink, eventSinkStartTsc, captureMetrics);
                metrics::recordCaptureEvent("bookticker", capturedBookTicker.tsNs,
                                            static_cast<std::uint64_t>(jsonLine.size() + 1u),
                                            static_cast<std::uint64_t>(internal::nowNs()));
        } else if (event.stream == cxet::api::market::PublicMarketDataStream::Orderbook) {
                if (!event.depth || !event.depthSides) continue;
                TscTick bridgeStartTsc{};
                if (captureMetrics) bridgeStartTsc = cxet::probes::captureTsc();
                const auto capturedDepth = cxet_bridge::CxetCaptureBridge::captureOrderBook(*event.depth, *event.depthSides, meta);
                auto row = makeDepthRow(capturedDepth);
                if (textEqualsAscii(config.exchange, "bitget")) {
                    normalizeFixedDepthSnapshotDelta(row, bitgetPreviousOrderbookLevels);
                }
                recordCxetLatencyIfEnabled(cxet::metrics::recorderBridgeMaterialize, bridgeStartTsc, captureMetrics);
                TscTick jsonRenderStartTsc{};
                if (captureMetrics) jsonRenderStartTsc = cxet::probes::captureTsc();
                const auto tapeLine = renderDepthTapeJsonLine(row);
                const auto sidecarLine = renderDepthRleSidecarJsonLine(row);
                const auto localBusLine = renderDepthJsonLine(row);
                recordCxetLatencyIfEnabled(cxet::metrics::recorderJsonRender, jsonRenderStartTsc, captureMetrics);
                local_exchange::globalLocalMarketDataBus().publish("orderbook.delta", std::string_view{meta.symbol.data}, localBusLine);
                TscTick eventSinkStartTsc{};
                if (captureMetrics) eventSinkStartTsc = cxet::probes::captureTsc();
                const auto fileStatus = jsonSink_.appendDepthTapeSidecarLines(row, tapeLine, sidecarLine);
                if (!isOk(fileStatus)) {
                    recordCxetLatencyIfEnabled(cxet::metrics::recorderEventSink, eventSinkStartTsc, captureMetrics);
                    metrics::recordCaptureWriteError("depth");
                    std::lock_guard<std::mutex> lock(stateMutex_);
                    lastError_ = "depth: failed to write depth_tape/depth_sidecar";
                    marketDataStop_.store(true, std::memory_order_release);
                    break;
                }
                (void)appendLiveDepth(row);
                depthCount_.fetch_add(1, std::memory_order_acq_rel);
                recordCxetLatencyIfEnabled(cxet::metrics::recorderEventSink, eventSinkStartTsc, captureMetrics);
                metrics::recordCaptureEvent("depth", capturedDepth.tsNs,
                                            static_cast<std::uint64_t>(tapeLine.size() + sidecarLine.size() + 2u),
                                            static_cast<std::uint64_t>(internal::nowNs()));
        }
        if (!drainTradesWarmupPages()) break;
        if (!flushTradesWarmupIfReady()) break;
    }

    if (tradesWarmupThread.joinable()) {
        tradesWarmupThread.join();
        (void)flushTradesWarmupIfReady();
    }

    nextManifestFlushNs = 0;
    (void)flushRecordingManifestIfDue_(nextManifestFlushNs);
    traderMarket->closeAll();
    tradesRunning_.store(false, std::memory_order_release);
    bookTickerRunning_.store(false, std::memory_order_release);
    orderbookRunning_.store(false, std::memory_order_release);
    marketDataRunning_.store(false, std::memory_order_release);
}

void CaptureCoordinator::reapStoppedThreads() noexcept {
    if (marketDataThread_.joinable() && !marketDataRunning_.load(std::memory_order_acquire)) marketDataThread_.join();
    if (referenceDataThread_.joinable() && !referenceDataRunning_.load(std::memory_order_acquire)) referenceDataThread_.join();
    if (liquidationsThread_.joinable() && !liquidationsRunning_.load(std::memory_order_acquire)) liquidationsThread_.join();
}

}  // namespace hftrec::capture
