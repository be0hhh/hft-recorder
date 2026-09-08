#include "VenueMultiplexCapture.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <string_view>
#include <utility>

#include "cxet/Api/Market/MarketDataReactor.hpp"
#include "cxet/Api/Market/PublicMarketDataStreamCatalog.hpp"
#include "cxet/Api/Market/PublicMarketDataSubscriptionPlanner.hpp"
#include "CaptureChannelSupport.hpp"
#include "CaptureCoordinatorRuntimeHelpers.hpp"
#include "Bridge/CxetCaptureBridge.hpp"
#include "../Replay/EventRows.hpp"
#include "hft_trader/Runtime/Market/MarketDataRuntime.hpp"
#include "hft_trader/Runtime/History/Orderbook/OrderBookSnapshotLoader.hpp"

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
    std::vector<std::pair<CaptureChannel, std::string>> skippedChannels{};
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

bool anyRequested(const ExternalCaptureChannels& channels) noexcept {
    return channels.trades || channels.liquidations || channels.bookTicker || channels.orderbook ||
           channels.markPrice || channels.indexPrice || channels.funding || channels.priceLimit;
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

void setRequested(ExternalCaptureChannels& channels, CaptureChannel channel, bool enabled) noexcept {
    switch (channel) {
        case CaptureChannel::Trades: channels.trades = enabled; break;
        case CaptureChannel::Liquidations: channels.liquidations = enabled; break;
        case CaptureChannel::BookTicker: channels.bookTicker = enabled; break;
        case CaptureChannel::Orderbook: channels.orderbook = enabled; break;
        case CaptureChannel::MarkPrice: channels.markPrice = enabled; break;
        case CaptureChannel::IndexPrice: channels.indexPrice = enabled; break;
        case CaptureChannel::Funding: channels.funding = enabled; break;
        case CaptureChannel::PriceLimit: channels.priceLimit = enabled; break;
    }
}

std::size_t channelIndex(CaptureChannel channel) noexcept {
    switch (channel) {
        case CaptureChannel::Trades: return 0u;
        case CaptureChannel::Liquidations: return 1u;
        case CaptureChannel::BookTicker: return 2u;
        case CaptureChannel::Orderbook: return 3u;
        case CaptureChannel::MarkPrice: return 4u;
        case CaptureChannel::IndexPrice: return 5u;
        case CaptureChannel::Funding: return 6u;
        case CaptureChannel::PriceLimit: return 7u;
    }
    return 0u;
}

bool makeDesiredChannel(const CaptureConfig& config,
                        CaptureChannel channel,
                        cxet::api::market::PublicMarketDataDesiredChannel& out,
                        std::string& error) {
    const hft_trader::runtime::VenueRuntimeConfig venue = runtime::makeTraderVenueConfig(config);
    if (venue.exchange.raw == canon::kExchangeIdUnknown.raw ||
        venue.market.raw == canon::kMarketTypeUnknown.raw || venue.symbols.size() != 1u) {
        error = "invalid recorder venue identity";
        return false;
    }
    out = cxet::api::market::PublicMarketDataDesiredChannel{};
    out.exchange = venue.exchange;
    out.market = venue.market;
    out.symbol = venue.symbols.front();
    out.stream = streamFor(channel);
    out.apiSlot = venue.apiSlot;
    out.captureLatency = true;
    out.wsLanes = runtime::kRecorderMarketWsLanes;
    const auto selectedWire = cxet::api::market::publicMarketDataSelectedWirePreference(
        out.exchange, out.market, out.stream);
    if (selectedWire != cxet::api::market::PublicMarketDataWirePreference::Auto) {
        out.wirePreference = selectedWire;
    }
    const auto fields = cxet::api::market::publicMarketDataStreamDefaultFields(out.stream);
    if (fields.size() > cxet::api::market::kMaxManagedMarketDataRequestedFields) {
        error = "market-data requested field capacity exceeded";
        return false;
    }
    out.requestedFieldCount = fields.size();
    for (std::size_t i = 0u; i < fields.size(); ++i) out.requestedFields[i] = fields[i];
    return true;
}

bool planDesiredChannels(const std::vector<cxet::api::market::PublicMarketDataDesiredChannel>& desired,
                         std::string& error) {
    char errorBuf[256]{};
    if (cxet::api::market::planPublicMarketDataSubscriptions(
            Span<const cxet::api::market::PublicMarketDataDesiredChannel>(desired.data(), desired.size()),
            nullptr,
            errorBuf,
            sizeof(errorBuf))) {
        error.clear();
        return true;
    }
    error = errorBuf[0] != '\0' ? errorBuf : "market-data route planning failed";
    return false;
}

bool sessionHasCanonicalRows(const std::filesystem::path& sessionDir) {
    std::error_code ec;
    const std::filesystem::path jsonl = sessionDir / "jsonl";
    for (std::filesystem::recursive_directory_iterator it(
             jsonl, std::filesystem::directory_options::skip_permission_denied, ec), end;
         !ec && it != end;
         it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        ec.clear();
        if (std::filesystem::file_size(it->path(), ec) != 0u && !ec) return true;
        ec.clear();
    }
    return false;
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
    std::size_t skippedJobCount{0u};
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

    void appendRouteSkip(std::string_view symbol, CaptureChannel channel, std::string_view detail) {
        if (!error.empty()) error += " | ";
        error += std::string{symbol};
        error += '/';
        error += healthChannelName(streamFor(channel));
        error += " skipped: ";
        error += detail;
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
    std::string abiError;
    if (!runtime::linkedTraderMarketDataRuntimeAbiMatches(abiError)) {
        impl_->error = std::move(abiError);
        return Status::Unknown;
    }
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
    struct PlannedJob {
        VenueMultiplexJob job{};
        ExternalCaptureChannels enabled{};
        std::vector<std::pair<CaptureChannel, std::string>> skipped{};
    };
    std::vector<PlannedJob> plannedJobs;
    plannedJobs.reserve(jobs.size());
    for (auto& job : jobs) plannedJobs.push_back(PlannedJob{.job = std::move(job)});

    std::vector<cxet::api::market::PublicMarketDataDesiredChannel> acceptedDesired;
    acceptedDesired.reserve(plannedJobs.size() * kChannels.size());
    for (auto& planned : plannedJobs) {
        const std::string_view symbol = planned.job.config.symbols.front();
        for (const CaptureChannel channel : kChannels) {
            if (!requested(planned.job.channels, channel)) continue;
            std::string detail;
            if (!captureChannelRuntimeReady(planned.job.config, channel, detail)) {
                planned.skipped.emplace_back(channel, std::move(detail));
                continue;
            }
            cxet::api::market::PublicMarketDataDesiredChannel candidate{};
            if (!makeDesiredChannel(planned.job.config, channel, candidate, detail)) {
                planned.skipped.emplace_back(channel, std::move(detail));
                continue;
            }
            acceptedDesired.push_back(candidate);
            if (!planDesiredChannels(acceptedDesired, detail)) {
                acceptedDesired.pop_back();
                planned.skipped.emplace_back(channel, std::move(detail));
                continue;
            }
            setRequested(planned.enabled, channel, true);
            impl_->enabledStreams[channelIndex(channel)] = true;
        }
        for (const auto& [channel, detail] : planned.skipped) {
            impl_->appendRouteSkip(symbol, channel, detail);
        }
        if (!anyRequested(planned.enabled)) ++impl_->skippedJobCount;
    }
    if (acceptedDesired.empty()) {
        if (impl_->error.empty()) impl_->error = "venue multiplex: no exact supported market-data routes";
        return Status::Unimplemented;
    }

    impl_->runtime = std::make_unique<hft_trader::runtime::MarketDataRuntime>();
    std::string applyError;
    if (!impl_->runtime || !impl_->runtime->applyDesiredChannels(
            Span<const cxet::api::market::PublicMarketDataDesiredChannel>(acceptedDesired.data(), acceptedDesired.size()),
            applyError)) {
        if (applyError.empty()) applyError = "venue multiplex: exact market-data apply failed";
        if (!impl_->error.empty()) impl_->error += " | ";
        impl_->error += applyError;
        (void)finalize();
        return Status::Unknown;
    }

    impl_->sinks.reserve(plannedJobs.size());
    for (auto& planned : plannedJobs) {
        if (!anyRequested(planned.enabled)) continue;
        SinkState sink{};
        sink.job = std::move(planned.job);
        sink.skippedChannels = std::move(planned.skipped);
        sink.coordinator = std::make_unique<CaptureCoordinator>();
        const auto status = sink.coordinator->startExternalCapture(
            sink.job.config, planned.enabled, sink.job.channels);
        if (!isOk(status)) {
            impl_->error = sink.coordinator->lastError();
            impl_->sinks.push_back(std::move(sink));
            (void)finalize();
            return status;
        }
        for (const auto& [channel, detail] : sink.skippedChannels) {
            sink.coordinator->noteExternalUnsupportedChannel(healthChannelName(streamFor(channel)), detail);
        }
        impl_->sinks.push_back(std::move(sink));
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
        const std::uint64_t sessionRows = rows(*sink.coordinator);
        const std::filesystem::path sessionDir = sink.coordinator->sessionDirCopy();
        impl_->finalRows += sessionRows;
        appendRuntimeHealthWarnings(
            impl_->error,
            sink.job.config.symbols.empty() ? std::string_view{} : std::string_view{sink.job.config.symbols.front()},
            sink.coordinator->manifestCopy());
        status = aggregateStatus(status, sink.coordinator->finalizeSession());
        if (sessionRows == 0u && !sessionDir.empty() && !sessionHasCanonicalRows(sessionDir)) {
            std::error_code ec;
            std::filesystem::remove_all(sessionDir, ec);
            if (ec && impl_->error.empty()) impl_->error = "failed to remove empty session: " + ec.message();
        }
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

std::size_t VenueMultiplexCapture::activeJobs() const noexcept {
    return impl_ ? impl_->sinks.size() : 0u;
}

std::size_t VenueMultiplexCapture::skippedJobs() const noexcept {
    return impl_ ? impl_->skippedJobCount : 0u;
}

std::string VenueMultiplexCapture::lastError() const { return impl_ ? impl_->error : std::string{}; }

}  // namespace hftrec::capture
