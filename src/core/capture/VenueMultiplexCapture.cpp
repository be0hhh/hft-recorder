#include "core/capture/VenueMultiplexCapture.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <string_view>
#include <utility>

#include "api/market/MarketDataReactor.hpp"
#include "core/capture/CaptureChannelSupport.hpp"
#include "core/capture/CaptureCoordinatorRuntimeHelpers.hpp"
#include "core/cxet_bridge/CxetCaptureBridge.hpp"
#include "core/replay/EventRows.hpp"
#include "hft_trader/runtime/market/MarketDataRuntime.hpp"
#include "hft_trader/runtime/history/orderbook/OrderBookSnapshotLoader.hpp"

namespace hftrec::capture {

namespace {

using PublicStream = cxet::api::market::PublicMarketDataStream;
using RuntimeEvent = hft_trader::runtime::MarketDataRuntimeEvent;

struct SinkState {
    VenueMultiplexJob job{};
    std::unique_ptr<CaptureCoordinator> coordinator{};
    std::uint64_t ingestSeq{0u};
    std::uint64_t tradesSeq{0u};
    std::uint64_t liquidationsSeq{0u};
    std::uint64_t bookTickerSeq{0u};
    std::vector<replay::PricePair> previousBitgetDepth{};
    bool haveFunding{false};
    replay::FundingRow lastFunding{};
};

runtime::EventSequenceIds nextIds(std::uint64_t& channelSeq, std::uint64_t& ingestSeq) noexcept {
    return runtime::EventSequenceIds{++channelSeq, ++ingestSeq};
}

bool requested(const ExternalCaptureChannels& channels, CaptureChannel channel) noexcept {
    switch (channel) {
        case CaptureChannel::Trades: return channels.trades;
        case CaptureChannel::Liquidations: return channels.liquidations;
        case CaptureChannel::BookTicker: return channels.bookTicker;
        case CaptureChannel::Orderbook: return channels.orderbook;
        case CaptureChannel::MarkPrice: return channels.markPrice;
        case CaptureChannel::IndexPrice: return channels.indexPrice;
        case CaptureChannel::Funding: return channels.funding;
        case CaptureChannel::PriceLimit: return channels.priceLimit;
    }
    return false;
}

PublicStream streamFor(CaptureChannel channel) noexcept {
    switch (channel) {
        case CaptureChannel::Trades: return PublicStream::Trades;
        case CaptureChannel::Liquidations: return PublicStream::Liquidations;
        case CaptureChannel::BookTicker: return PublicStream::BookTicker;
        case CaptureChannel::Orderbook: return PublicStream::Orderbook;
        case CaptureChannel::MarkPrice: return PublicStream::MarkPrice;
        case CaptureChannel::IndexPrice: return PublicStream::IndexPrice;
        case CaptureChannel::Funding: return PublicStream::Funding;
        case CaptureChannel::PriceLimit: return PublicStream::PriceLimit;
    }
    return PublicStream::BookTicker;
}

std::string_view healthChannelName(PublicStream stream) noexcept {
    switch (stream) {
        case PublicStream::Trades: return "trades";
        case PublicStream::Liquidations: return "liquidations";
        case PublicStream::BookTicker: return "bookticker";
        case PublicStream::Orderbook: return "depth";
        case PublicStream::MarkPrice: return "mark_price";
        case PublicStream::IndexPrice: return "index_price";
        case PublicStream::Funding: return "funding";
        case PublicStream::PriceLimit: return "price_limit";
        default: return {};
    }
}

ExternalCaptureChannels enabledChannelsFor(const ExternalCaptureChannels& requestedChannels,
                                           const std::array<bool, 8>& enabled) noexcept {
    ExternalCaptureChannels out{};
    out.trades = requestedChannels.trades && enabled[0];
    out.liquidations = requestedChannels.liquidations && enabled[1];
    out.bookTicker = requestedChannels.bookTicker && enabled[2];
    out.orderbook = requestedChannels.orderbook && enabled[3];
    out.markPrice = requestedChannels.markPrice && enabled[4];
    out.indexPrice = requestedChannels.indexPrice && enabled[5];
    out.funding = requestedChannels.funding && enabled[6];
    out.priceLimit = requestedChannels.priceLimit && enabled[7];
    return out;
}

std::uint64_t rows(const CaptureCoordinator& coordinator) noexcept {
    return coordinator.tradesCount() + coordinator.liquidationsCount() + coordinator.bookTickerCount() +
           coordinator.depthCount() + coordinator.markPriceCount() + coordinator.indexPriceCount() +
           coordinator.fundingCount() + coordinator.priceLimitCount();
}

Status aggregateStatus(Status current, Status next) noexcept {
    return isOk(current) ? next : current;
}

void appendRuntimeHealthWarning(std::string& out,
                                std::string_view symbol,
                                std::string_view channel,
                                const ChannelRuntimeHealth& health) {
    const bool degraded = (health.required && health.state != "live") ||
                          health.reconnectCount != 0u ||
                          health.droppedEventCount != 0u ||
                          health.unroutableEventCount != 0u ||
                          (!health.lastError.empty() && health.required);
    if (!degraded) return;
    if (!out.empty()) out += " | ";
    out += std::string{symbol} + " " + std::string{channel} + "=" + health.state;
    if (health.reconnectCount != 0u) out += " reconnects=" + std::to_string(health.reconnectCount);
    if (!health.lastError.empty()) out += " (" + health.lastError + ")";
}

void appendRuntimeHealthWarnings(std::string& out,
                                 std::string_view symbol,
                                 const SessionManifest& manifest) {
    appendRuntimeHealthWarning(out, symbol, "trades", manifest.tradesRuntime);
    appendRuntimeHealthWarning(out, symbol, "liquidations", manifest.liquidationsRuntime);
    appendRuntimeHealthWarning(out, symbol, "bookticker", manifest.bookTickerRuntime);
    appendRuntimeHealthWarning(out, symbol, "depth", manifest.depthRuntime);
    appendRuntimeHealthWarning(out, symbol, "mark_price", manifest.markPriceRuntime);
    appendRuntimeHealthWarning(out, symbol, "index_price", manifest.indexPriceRuntime);
    appendRuntimeHealthWarning(out, symbol, "funding", manifest.fundingRuntime);
    appendRuntimeHealthWarning(out, symbol, "price_limit", manifest.priceLimitRuntime);
}

}  // namespace

struct VenueMultiplexCapture::Impl {
    std::vector<SinkState> sinks{};
    std::unique_ptr<hft_trader::runtime::MarketDataRuntime> runtime{};
    std::array<bool, 8> enabledStreams{};
    std::vector<std::uint8_t> connectionSeen{};
    std::vector<std::uint8_t> connectionState{};
    std::vector<SinkState*> channelSinks{};
    bool running{false};
    bool finalized{false};
    std::uint64_t finalRows{0u};
    std::string error{};
    std::chrono::steady_clock::time_point nextLifecycle{};
    std::chrono::steady_clock::time_point nextManifestFlush{};

    SinkState* sinkFor(std::string_view symbol) noexcept {
        for (auto& sink : sinks) {
            if (!sink.job.config.symbols.empty() && sink.job.config.symbols.front() == symbol) return &sink;
        }
        return nullptr;
    }

    void noteWriteError(SinkState& sink, std::string_view channel) {
        const std::string detail = sink.coordinator ? sink.coordinator->lastError() : std::string{};
        error = sink.job.config.exchange + "/" + sink.job.config.market + " " +
                (sink.job.config.symbols.empty() ? std::string{} : sink.job.config.symbols.front()) +
                " " + std::string{channel} + " write failed";
        if (!detail.empty()) error += ": " + detail;
    }

    bool append(RuntimeEvent& event) noexcept {
        if (event.status != cxet::api::market::PublicMarketDataStatus::Parsed) return false;
        const auto meta = runtime::streamMetaFromTraderEvent(event, {});
        SinkState* sink = event.channelIndex < channelSinks.size() ? channelSinks[event.channelIndex] : nullptr;
        if (!sink || !sink->coordinator) {
            error = "venue multiplex: parsed event has unknown symbol";
            const std::string_view channel = healthChannelName(event.stream);
            for (auto& candidate : sinks) {
                if (candidate.coordinator) candidate.coordinator->noteExternalUnroutableEvent(channel, error);
            }
            return false;
        }

        Status status = Status::Ok;
        if (event.stream == PublicStream::Trades) {
            const auto captured = cxet_bridge::CxetCaptureBridge::captureTrade(event.trade, meta);
            const auto row = runtime::makeTradeRow(captured,
                                                   sink->job.config.exchange,
                                                   sink->job.config.market,
                                                   nextIds(sink->tradesSeq, sink->ingestSeq));
            status = sink->coordinator->appendExternalTrade(row);
        } else if (event.stream == PublicStream::Liquidations) {
            const auto captured = cxet_bridge::CxetCaptureBridge::captureLiquidation(event.liquidation);
            auto row = runtime::makeLiquidationRow(captured,
                                                   sink->job.config.exchange,
                                                   sink->job.config.market,
                                                   nextIds(sink->liquidationsSeq, sink->ingestSeq));
            row.symbol = sink->job.config.symbols.front();
            status = sink->coordinator->appendExternalLiquidation(row);
        } else if (event.stream == PublicStream::BookTicker) {
            const auto captured = cxet_bridge::CxetCaptureBridge::captureBookTicker(event.bookTicker, meta);
            const auto row = runtime::makeBookTickerRow(captured,
                                                        sink->job.config.exchange,
                                                        sink->job.config.market,
                                                        nextIds(sink->bookTickerSeq, sink->ingestSeq));
            status = sink->coordinator->appendExternalBookTicker(row);
        } else if (event.stream == PublicStream::Orderbook) {
            if (!event.depth || !event.depthSides) return false;
            const auto captured = cxet_bridge::CxetCaptureBridge::captureOrderBook(*event.depth, *event.depthSides, meta);
            auto row = runtime::makeDepthRow(captured);
            if (runtime::textEqualsAscii(sink->job.config.exchange, "bitget")) {
                runtime::normalizeFixedDepthSnapshotDelta(row, sink->previousBitgetDepth);
            }
            status = sink->coordinator->appendExternalDepth(row);
        } else if (event.stream == PublicStream::MarkPrice) {
            status = sink->coordinator->appendExternalMarkPrice(runtime::makeMarkPriceRow(event.markPrice));
        } else if (event.stream == PublicStream::IndexPrice) {
            status = sink->coordinator->appendExternalIndexPrice(runtime::makeIndexPriceRow(event.indexPrice));
        } else if (event.stream == PublicStream::Funding) {
            const auto row = runtime::makeFundingRow(event.funding);
            if (sink->haveFunding && runtime::sameFundingTuple(sink->lastFunding, row)) return true;
            status = sink->coordinator->appendExternalFunding(row);
            if (isOk(status)) {
                sink->lastFunding = row;
                sink->haveFunding = true;
            }
        } else if (event.stream == PublicStream::PriceLimit) {
            status = sink->coordinator->appendExternalPriceLimit(runtime::makePriceLimitRow(event.priceLimit));
        }
        if (!isOk(status)) noteWriteError(*sink, runtime::marketStreamName(event.stream));
        return isOk(status);
    }

    void seedInitialOrderbooks() noexcept {
        if (!runtime || !enabledStreams[3]) return;
        for (auto& sink : sinks) {
            if (!sink.coordinator || !sink.job.channels.orderbook ||
                !runtime::shouldFetchInitialOrderbookSnapshot(sink.job.config)) {
                continue;
            }
            Symbol symbol{};
            runtime::copySymbolFromText(symbol, sink.job.config.symbols.front());
            cxet::composite::OrderBookSnapshot snapshot{};
            MessageBuffer requestBuf{};
            MessageBuffer recvBuf{};
            if (!hft_trader::runtime::orderbook::loadOrderBookSnapshotForVenue(
                    runtime::makeTraderVenueConfig(sink.job.config), symbol, snapshot, requestBuf, recvBuf)) {
                const std::string warning = "orderbook: initial snapshot fetch failed; continuing with WS depth";
                sink.coordinator->noteExternalChannelError("depth", warning);
                if (!error.empty()) error += " | ";
                error += sink.job.config.symbols.front() + ": " + warning;
                continue;
            }
            bool seeded = false;
            for (std::size_t i = 0u; i < runtime->channelCount(); ++i) {
                const auto* channel = runtime->channelAt(i);
                if (channel && channel->stream == PublicStream::Orderbook &&
                    std::strcmp(channel->symbol.data, symbol.data) == 0) {
                    seeded = runtime->seedOrderBookSnapshot(i, snapshot);
                    break;
                }
            }
            if (!seeded) {
                const std::string warning = "orderbook: snapshot loaded but matching runtime channel was not seeded";
                sink.coordinator->noteExternalChannelError("depth", warning);
                if (!error.empty()) error += " | ";
                error += sink.job.config.symbols.front() + ": " + warning;
                continue;
            }
            auto row = runtime::makeDepthRow(cxet_bridge::CxetCaptureBridge::captureOrderBook(snapshot));
            if (row.tsNs <= 0) row.tsNs = internal::nowNs();
            if (!isOk(sink.coordinator->appendExternalDepth(row))) noteWriteError(sink, "depth snapshot");
        }
    }

    bool bindChannelSinks() noexcept {
        if (!runtime) return false;
        channelSinks.assign(runtime->channelCount(), nullptr);
        for (std::size_t i = 0u; i < runtime->channelCount(); ++i) {
            const auto* channel = runtime->channelAt(i);
            if (!channel) continue;
            channelSinks[i] = sinkFor(channel->symbol.data);
            if (!channelSinks[i]) {
                error = "venue multiplex: runtime channel could not be bound to symbol sink";
                return false;
            }
        }
        return true;
    }

    void flushManifests() noexcept {
        for (auto& sink : sinks) {
            if (sink.coordinator && !isOk(sink.coordinator->refreshExternalManifest())) {
                noteWriteError(sink, "manifest");
            }
        }
    }


    void sampleConnections() noexcept {
        if (!runtime) return;
        const std::size_t count = runtime->channelCount();
        if (connectionSeen.size() != count) {
            connectionSeen.assign(count, 0u);
            connectionState.assign(count, 0u);
        }
        for (std::size_t i = 0u; i < count; ++i) {
            const auto* channel = runtime->channelAt(i);
            if (!channel) continue;
            SinkState* sink = i < channelSinks.size() ? channelSinks[i] : nullptr;
            if (!sink || !sink->coordinator) continue;
            const bool connected = runtime->routeConnectedForChannel(*channel);
            const bool seen = connectionSeen[i] != 0u;
            const bool wasConnected = connectionState[i] != 0u;
            if (!seen || connected != wasConnected) {
                sink->coordinator->noteExternalChannelConnection(
                    healthChannelName(channel->stream), connected, seen && wasConnected && !connected);
                connectionSeen[i] = 1u;
                connectionState[i] = connected ? 1u : 0u;
            }
        }
    }
};

VenueMultiplexCapture::VenueMultiplexCapture() : impl_(std::make_unique<Impl>()) {}
VenueMultiplexCapture::~VenueMultiplexCapture() { (void)finalize(); }

Status VenueMultiplexCapture::start(std::vector<VenueMultiplexJob> jobs) noexcept {
    if (!impl_ || jobs.empty()) return Status::InvalidArgument;
    if (jobs.size() > 20u) {
        impl_->error = "venue multiplex supports at most 20 symbols";
        return Status::InvalidArgument;
    }
    const auto& first = jobs.front().config;
    if (first.symbols.size() != 1u) return Status::InvalidArgument;
    for (const auto& job : jobs) {
        if (job.config.exchange != first.exchange || job.config.market != first.market || job.config.symbols.size() != 1u) {
            impl_->error = "venue multiplex jobs must share exchange and market and contain one symbol each";
            return Status::InvalidArgument;
        }
    }

    static constexpr std::array<CaptureChannel, 8> kChannels{
        CaptureChannel::Trades, CaptureChannel::Liquidations, CaptureChannel::BookTicker, CaptureChannel::Orderbook,
        CaptureChannel::MarkPrice, CaptureChannel::IndexPrice, CaptureChannel::Funding, CaptureChannel::PriceLimit};
    std::vector<PublicStream> streams;
    streams.reserve(kChannels.size());
    for (std::size_t i = 0u; i < kChannels.size(); ++i) {
        bool wanted = false;
        for (const auto& job : jobs) wanted = wanted || requested(job.channels, kChannels[i]);
        if (!wanted) continue;
        std::string detail;
        if (!captureChannelRuntimeReady(first, kChannels[i], detail)) continue;
        impl_->enabledStreams[i] = true;
        streams.push_back(streamFor(kChannels[i]));
    }
    if (streams.empty()) {
        impl_->error = "venue multiplex: no supported market-data streams";
        return Status::Unimplemented;
    }

    impl_->sinks.reserve(jobs.size());
    for (auto& job : jobs) {
        SinkState sink{};
        sink.job = std::move(job);
        sink.coordinator = std::make_unique<CaptureCoordinator>();
        const auto status = sink.coordinator->startExternalCapture(
            sink.job.config, enabledChannelsFor(sink.job.channels, impl_->enabledStreams), sink.job.channels);
        if (!isOk(status)) {
            impl_->error = sink.coordinator->lastError();
            return status;
        }
        impl_->sinks.push_back(std::move(sink));
    }

    CaptureConfig aggregate = first;
    aggregate.symbols.clear();
    for (const auto& sink : impl_->sinks) aggregate.symbols.push_back(sink.job.config.symbols.front());
    impl_->runtime = std::make_unique<hft_trader::runtime::MarketDataRuntime>();
    std::string applyError;
    if (!runtime::applyTraderMarketDataConfig(*impl_->runtime,
                                              aggregate,
                                              Span<const PublicStream>(streams.data(), streams.size()),
                                              applyError)) {
        impl_->error = std::move(applyError);
        (void)finalize();
        return Status::Unknown;
    }
    if (!impl_->bindChannelSinks()) {
        (void)finalize();
        return Status::Unknown;
    }
    impl_->seedInitialOrderbooks();
    impl_->running = true;
    impl_->nextLifecycle = std::chrono::steady_clock::now();
    impl_->nextManifestFlush = impl_->nextLifecycle + std::chrono::seconds(5);
    return Status::Ok;
}

bool VenueMultiplexCapture::pollOnce() noexcept {
    if (!impl_ || !impl_->running || !impl_->runtime) return false;
    const auto now = std::chrono::steady_clock::now();
    if (now >= impl_->nextLifecycle) {
        (void)impl_->runtime->pollLifecycleOnce();
        impl_->sampleConnections();
        impl_->nextLifecycle = now + std::chrono::milliseconds(250);
    }
    if (now >= impl_->nextManifestFlush) {
        impl_->flushManifests();
        impl_->nextManifestFlush = now + std::chrono::seconds(5);
    }
    bool consumed = false;
    for (std::size_t i = 0u; i < 64u; ++i) {
        RuntimeEvent event{};
        if (!impl_->runtime->pollAvailableOne(event)) break;
        consumed = true;
        (void)impl_->append(event);
    }
    return consumed;
}

void VenueMultiplexCapture::requestStop() noexcept {
    if (impl_) impl_->running = false;
}

Status VenueMultiplexCapture::finalize() noexcept {
    if (!impl_ || impl_->finalized) return Status::Ok;
    impl_->running = false;
    if (impl_->runtime) impl_->runtime->closeAll();
    impl_->finalRows = 0u;
    Status status = Status::Ok;
    for (auto& sink : impl_->sinks) {
        if (!sink.coordinator) continue;
        impl_->finalRows += rows(*sink.coordinator);
        appendRuntimeHealthWarnings(
            impl_->error,
            sink.job.config.symbols.empty() ? std::string_view{} : std::string_view{sink.job.config.symbols.front()},
            sink.coordinator->manifestCopy());
        status = aggregateStatus(status, sink.coordinator->finalizeSession());
    }
    impl_->finalized = true;
    return status;
}

bool VenueMultiplexCapture::running() const noexcept { return impl_ && impl_->running; }

std::uint64_t VenueMultiplexCapture::totalRows() const noexcept {
    if (!impl_) return 0u;
    if (impl_->finalized) return impl_->finalRows;
    std::uint64_t total = 0u;
    for (const auto& sink : impl_->sinks) if (sink.coordinator) total += rows(*sink.coordinator);
    return total;
}

std::string VenueMultiplexCapture::lastError() const { return impl_ ? impl_->error : std::string{}; }

}  // namespace hftrec::capture
