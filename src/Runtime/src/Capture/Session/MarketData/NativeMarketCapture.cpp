#include "NativeMarketCapture.hpp"
#include "../Bridge/CxetCaptureBridge.hpp"
#include "../../Coordinator/CaptureCoordinatorRuntimeHelpers.hpp"
#include "cxet/Os/ClockSource.hpp"
#include <limits>
#include <chrono>
#include <algorithm>
#include <new>

namespace hftrec::capture {
bool NativeCaptureHooks::observeTransport(std::uint64_t now) noexcept {
    if (!owner || !owner->owner_ || !owner->owner_->prepared()) return false;
    bool noTerminals = true;
    const bool retired = owner->core_.transport().retireTerminals(now,
        [&noTerminals](std::uint32_t, std::uint64_t) noexcept {
            noTerminals = false;
            return true;
        });
    return retired && noTerminals && owner->core_.transport().snapshotsHealthy() &&
        owner->owner_->credentialsHealthy(now);
}
void NativeCaptureHooks::retainBbo(const cxet::runtime::market::BboCommit& value) noexcept {if (owner) owner->onBbo(value);}
void NativeCaptureHooks::retainTrade(const cxet::runtime::market::TradeCommit& value) noexcept {if (owner) owner->onTrade(value);}
void NativeCaptureHooks::onDepth(const cxet::runtime::market::CommitMetadata& meta,
    const cxet::api::market::PublicMarketDepthFrame& value) noexcept {if (owner) owner->onDepth(meta,value);}

bool NativeMarketCapture::configure(std::span<const NativeCaptureSource> sources,const char* envPath,std::string& error) noexcept {
    if (owner_ || sources.empty() || sources.size()>60u) {error="invalid capture topology";return false;}
    sources_.assign(sources.begin(),sources.end());
    std::vector<cxet::runtime::market::ConfiguredMarketSource> native;
    native.reserve(sources.size());
    for (const auto& row:sources) {
        if (!row.sink || !row.config) {error="capture source sink missing";return false;}
        native.push_back(row.native);
    }
    // One flat row arena sized by the selected native level envelope, rather
    // than a maximum-depth allocation per queued event.
    auto registry=std::unique_ptr<cxet::api::market::PublicMarketRegistry>(new(std::nothrow) cxet::api::market::PublicMarketRegistry{});
    if (!registry || cxet::api::market::buildStagedPublicMarketCatalog(registry.get())!=cxet::api::market::StagedPublicMarketCatalogStatus::Ready) {
        error="capture native registry unavailable";return false;
    }
    for (const auto& row:native) {
        const auto* descriptor=registry->select(row.exchange.raw,row.market.raw,row.object,0u,row.apiProtocolProfile.raw);
        if (!descriptor) {error="capture native descriptor unavailable";return false;}
        if (row.object==cxet::api::market::PublicMarketObject::Depth)
            rowCapacity_=std::max(rowCapacity_,static_cast<std::size_t>(descriptor->maximumDepthLevels)*2u);
    }
    queue_.reset(new(std::nothrow) hftrec::SpscRing<Event,128u>{});
    if (rowCapacity_) rows_.reset(new(std::nothrow) cxet::market::DepthMutation[rowCapacity_]);
    if (!queue_ || (rowCapacity_ && !rows_)) {error="capture bounded handoff allocation failed";shutdown();return false;}
    owner_.reset(new(std::nothrow) cxet::runtime::market::ConfiguredMarketOwner(core_.transport(),core_.consumerBinding()));
    if (!owner_ || !owner_->configure(native,{.envPath=envPath,.maximumSources=60u,.maximumLanes=60u,.websocketRaceLanes=1u},error)) {
        shutdown();return false;
    }
    producerEpoch_=cxet::os::nowMonotonicNs().raw;shardSequence_=0u;
    if (!producerEpoch_) {error="capture run identity unavailable";shutdown();return false;}
    try {storageThread_.reset(new std::thread([this]() noexcept {drainWorker();}));}
    catch (...) {error="capture storage worker unavailable";shutdown();return false;}
    return true;
}
bool NativeMarketCapture::iterate(std::uint64_t now) noexcept {
    if (!owner_ || failed_.load(std::memory_order_acquire)) return false;
    const bool ready=core_.iterate(now);
    return ready && !failed_.load(std::memory_order_acquire);
}
void NativeMarketCapture::shutdown() noexcept {
    if (owner_) owner_->shutdown();owner_.reset();
    stopStorage_.store(true,std::memory_order_release);
    if (storageThread_ && storageThread_->joinable()) storageThread_->join();
    storageThread_.reset();
    for (std::size_t i=0u;i<sources_.size();++i) {
        const auto count=lostBySource_[i].exchange(0u,std::memory_order_acq_rel);
        if (!count) continue;
        const auto object=sources_[i].native.object;
        const auto* name=object==cxet::api::market::PublicMarketObject::Trade?"trades":
            object==cxet::api::market::PublicMarketObject::Depth?"depth":"bookticker";
        sources_[i].sink->noteExternalCaptureLoss(name,count);
    }
    queue_.reset();rows_.reset();sources_.clear();
}
std::string NativeMarketCapture::error() const noexcept {
    std::lock_guard lock(errorMutex_);
    if (!error_.empty()) return error_;
    if (lostEvents_.load(std::memory_order_acquire)) return "capture handoff overflow; recording stopped with lost events";
    return {};
}
bool NativeMarketCapture::connected(std::size_t source) const noexcept {
    if (!owner_) return false;
    const auto* binding=owner_->sourceBinding(source);
    if (!binding || !binding->laneCount) return false;
    for (std::size_t race=0u;race<binding->laneCount;++race) {
        const auto* state=core_.kernel().laneState(binding->firstLane+race);
        if (!state || !state->acceptingFrames || !state->acceptedFrames ||
            state->driverTerminal!=cxet::runtime::market::PublicMarketDriverTerminal::None) return false;
    }
    return true;
}
const NativeCaptureSource* NativeMarketCapture::source(std::uint32_t id) noexcept {
    if (!id || id>sources_.size()) {std::lock_guard lock(errorMutex_);error_="capture commit source outside configured membership";failed_.store(true,std::memory_order_release);return nullptr;}
    return &sources_[id-1u];
}
replay::EventArrival NativeMarketCapture::arrival(const cxet::runtime::market::CommitMetadata& meta,std::uint64_t ts) noexcept {
    replay::EventArrival value{};
    if (meta.origin==cxet::runtime::market::MarketCommitOrigin::RestDepthSnapshot) {
        value.flags=replay::EventArrivalHistoricalBackfill;
    } else {
        if (meta.receiveTimestampNs>static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()) ||
            shardSequence_==std::numeric_limits<std::uint64_t>::max()) return {};
        value.receiveRealtimeNs=static_cast<std::int64_t>(meta.receiveTimestampNs);
        value.receiveMonotonicNs=meta.receivedMonotonicNs;value.producerEpoch=producerEpoch_;
        value.sourceGeneration=meta.sourceGeneration;value.sessionEpoch=meta.sessionEpoch;
        value.frameSequence=meta.frameSequence;value.shardSequence=++shardSequence_;
        value.sourceId=meta.sourceId;value.eventOrdinal=meta.nativeIdentity.eventOrdinal;
        value.flags=replay::EventArrivalApplicationFrame;
        if (ts>meta.receiveTimestampNs) value.flags|=replay::EventArrivalExchangeAheadOfReceive;
    }
    if (!ts) value.flags|=replay::EventArrivalExchangeTimestampMissing;
    return value;
}
void NativeMarketCapture::account(Status status,const NativeCaptureSource& src,std::string_view channel) noexcept {
    if (isOk(status)) {committedRows_.fetch_add(1u,std::memory_order_release);return;}
    auto detail=src.sink->lastError();if (detail.empty()) detail="capture row append rejected";
    {std::lock_guard lock(errorMutex_);error_=detail;}
    failed_.store(true,std::memory_order_release);src.sink->noteExternalChannelError(channel,detail);
}
void NativeMarketCapture::lost(std::uint32_t id) noexcept {
    lostEvents_.fetch_add(1u,std::memory_order_relaxed);
    if (id && id<=lostBySource_.size()) lostBySource_[id-1u].fetch_add(1u,std::memory_order_relaxed);
    failed_.store(true,std::memory_order_release);
}
void NativeMarketCapture::publish(const Event& event) noexcept {
    if (failed_.load(std::memory_order_relaxed) || !queue_ || !queue_->tryPush(event)) {
        lost(event.metadata.sourceId);
    }
}
void NativeMarketCapture::onTrade(const cxet::runtime::market::TradeCommit& value) noexcept {
    Event event{};event.metadata=value.metadata;event.object=cxet::api::market::PublicMarketObject::Trade;event.trade=value.value;publish(event);
}
void NativeMarketCapture::onBbo(const cxet::runtime::market::BboCommit& value) noexcept {
    Event event{};event.metadata=value.metadata;event.object=cxet::api::market::PublicMarketObject::BookTicker;
    event.bbo=value.value;event.bidPresent=value.bidPresent;event.askPresent=value.askPresent;publish(event);
}
void NativeMarketCapture::onDepth(const cxet::runtime::market::CommitMetadata& metadata,
    const cxet::api::market::PublicMarketDepthFrame& value) noexcept {
    Event event{};event.metadata=metadata;event.object=cxet::api::market::PublicMarketObject::Depth;event.depth=value;
    const auto count=static_cast<std::uint64_t>(value.levelCount);
    if (failed_.load(std::memory_order_relaxed) || !rows_ || !rowCapacity_ || count>rowCapacity_ ||
        (count && !value.levels) || rowHead_>std::numeric_limits<std::uint64_t>::max()-rowCapacity_-count) {
        lost(metadata.sourceId);return;
    }
    auto begin=rowHead_;const auto index=begin%rowCapacity_;
    if (index+count>rowCapacity_) begin+=rowCapacity_-index;
    const auto end=begin+count;const auto tail=rowTail_.load(std::memory_order_acquire);
    if (end-tail>rowCapacity_) {lost(metadata.sourceId);return;}
    auto* destination=rows_.get()+begin%rowCapacity_;
    for (std::uint32_t i=0u;i<value.levelCount;++i) destination[i]=value.levels[i];
    event.depth.levels=destination;event.depth.levelCapacity=value.levelCount;event.rowEnd=end;
    rowHead_=end;publish(event);
}
void NativeMarketCapture::drainWorker() noexcept {
    Event event{};
    auto nextManifest=std::chrono::steady_clock::now()+std::chrono::seconds(5);
    for (;;) {
        const auto now=std::chrono::steady_clock::now();
        if (now>=nextManifest) {
            for (const auto& source:sources_) (void)source.sink->refreshExternalManifest();
            nextManifest=now+std::chrono::seconds(5);
        }
        if (queue_ && queue_->tryPop(event)) {
            if (!failed_.load(std::memory_order_acquire)) consume(event);
            else lost(event.metadata.sourceId);
            if (event.object==cxet::api::market::PublicMarketObject::Depth) rowTail_.store(event.rowEnd,std::memory_order_release);
            continue;
        }
        if (stopStorage_.load(std::memory_order_acquire)) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
void NativeMarketCapture::consume(const Event& event) noexcept {
    const auto* src=source(event.metadata.sourceId);if (!src) return;
    if (event.object==cxet::api::market::PublicMarketObject::Trade) {
        if (event.trade.initiatorSide() == cxet::composite::TradeInitiatorSide::Unknown) {
            const std::string detail="canonical JSON trade corpus cannot represent an unknown initiator side";
            {std::lock_guard lock(errorMutex_);error_=detail;}
            lost(event.metadata.sourceId);src->sink->noteExternalChannelError("trades",detail);return;
        }
        const cxet::runtime::market::TradeCommit value{event.metadata,event.trade};
        const auto captured=cxet_bridge::CxetCaptureBridge::captureTrade(value,src->config->symbols.front());
        auto row=runtime::makeTradeRow(captured,src->config->exchange,src->config->market,{});
        row.arrival=arrival(event.metadata,event.trade.ts.raw);
        row.captureSeq=static_cast<std::int64_t>(row.arrival.shardSequence);row.ingestSeq=row.captureSeq;
        account(src->sink->appendExternalTrade(row),*src,"trades");return;
    }
    if (event.object==cxet::api::market::PublicMarketObject::BookTicker) {
        const cxet::runtime::market::BboCommit value{event.metadata,event.bbo,event.bidPresent,event.askPresent};
        const auto captured=cxet_bridge::CxetCaptureBridge::captureBookTicker(value,src->config->symbols.front());
        auto row=runtime::makeBookTickerRow(captured,src->config->exchange,src->config->market,{});
        row.arrival=arrival(event.metadata,event.bbo.ts.raw);
        row.captureSeq=static_cast<std::int64_t>(row.arrival.shardSequence);row.ingestSeq=row.captureSeq;
        account(src->sink->appendExternalBookTicker(row),*src,"bookticker");return;
    }
    if (event.depth.kind!=cxet::api::market::PublicMarketDepthFrameKind::Delta ||
        event.metadata.origin!=cxet::runtime::market::MarketCommitOrigin::NativeStream) {
        const std::string detail="canonical JSON depth cannot represent native snapshot/rebase metadata; recording stopped";
        {std::lock_guard lock(errorMutex_);error_=detail;}
        lost(event.metadata.sourceId);src->sink->noteExternalChannelError("depth",detail);return;
    }
    auto row=runtime::makeDepthRow(cxet_bridge::CxetCaptureBridge::captureOrderBook(event.metadata,event.depth));
    row.arrival=arrival(event.metadata,event.depth.exchangeTimestampNs);
    row.captureSeq=static_cast<std::int64_t>(row.arrival.shardSequence);row.ingestSeq=row.captureSeq;
    account(src->sink->appendExternalDepth(row),*src,"depth");
}
} // namespace hftrec::capture
