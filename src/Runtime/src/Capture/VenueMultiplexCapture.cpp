#include "VenueMultiplexCapture.hpp"
#include "CaptureChannelSupport.hpp"
#include "MarketData/NativeMarketCapture.hpp"
#include "cxet/Os/ClockSource.hpp"
#include <array>
#include <chrono>
#include <utility>

namespace hftrec::capture {
namespace {
constexpr std::array<CaptureChannel,3u> channels{CaptureChannel::Trades,CaptureChannel::BookTicker,CaptureChannel::Orderbook};
bool requested(const ExternalCaptureChannels& row,CaptureChannel channel) noexcept {
    switch (channel) {
        case CaptureChannel::Trades:return row.trades;
        case CaptureChannel::BookTicker:return row.bookTicker;
        case CaptureChannel::Orderbook:return row.orderbook;
    }
    return false;
}
void enable(ExternalCaptureChannels& row,CaptureChannel channel) noexcept {
    switch (channel) {
        case CaptureChannel::Trades:row.trades=true;break;
        case CaptureChannel::BookTicker:row.bookTicker=true;break;
        case CaptureChannel::Orderbook:row.orderbook=true;break;
    }
}
const char* healthName(CaptureChannel channel) noexcept {return channel==CaptureChannel::Orderbook?"depth":captureChannelName(channel);}
std::uint64_t rows(const CaptureCoordinator& value) noexcept {return value.tradesCount()+value.bookTickerCount()+value.depthCount();}
struct Sink final {
    VenueMultiplexJob job{};
    std::unique_ptr<CaptureCoordinator> coordinator{};
};
}
struct VenueMultiplexCapture::Impl final {
    std::vector<Sink> sinks{};
    std::unique_ptr<NativeMarketCapture> native{};
    std::vector<NativeCaptureSource> sources{};
    std::vector<CaptureChannel> sourceChannels{};
    std::vector<std::uint8_t> connectionSeen{},connectionState{};
    bool running{false};bool finalized{false};
    std::uint64_t finalRows{0u};std::size_t skippedJobCount{0u};
    std::string error{};
    std::chrono::steady_clock::time_point nextManifest{};
    void sampleConnections() noexcept {
        for (std::size_t i=0u;i<sources.size();++i) {
            const bool connected=native->connected(i);
            const bool prior=connectionState[i]!=0u;
            if (!connectionSeen[i] || connected!=prior) {
                sources[i].sink->noteExternalChannelConnection(healthName(sourceChannels[i]),connected,
                    connectionSeen[i] && prior && !connected);
                connectionSeen[i]=1u;connectionState[i]=connected?1u:0u;
            }
        }
    }
};
VenueMultiplexCapture::VenueMultiplexCapture():impl_(std::make_unique<Impl>()) {}
VenueMultiplexCapture::~VenueMultiplexCapture(){(void)finalize();}
Status VenueMultiplexCapture::start(std::vector<VenueMultiplexJob> jobs) noexcept {
    if (!impl_ || jobs.empty() || jobs.size()>20u || impl_->native || impl_->finalized) return Status::InvalidArgument;
    const auto& first=jobs.front().config;
    if (first.symbols.size()!=1u) return Status::InvalidArgument;
    for (const auto& job:jobs) {
        if (job.config.exchange!=first.exchange || job.config.market!=first.market || job.config.symbols.size()!=1u) {
            impl_->error="venue capture requires one exchange/product and one exact symbol per job";return Status::InvalidArgument;
        }
    }
    impl_->sinks.reserve(jobs.size());impl_->sources.reserve(jobs.size()*3u);impl_->sourceChannels.reserve(jobs.size()*3u);
    for (auto& job:jobs) {
        ExternalCaptureChannels enabled{};
        struct Selected {CaptureChannel channel{};cxet::runtime::market::ConfiguredMarketSource source{};};
        std::vector<Selected> selected;
        for (const auto channel:channels) {
            if (!requested(job.channels,channel)) continue;
            cxet::runtime::market::ConfiguredMarketSource source{};std::string detail;
            if (!makeConfiguredCaptureSource(job.config,channel,source,detail)) {
                if (!impl_->error.empty()) impl_->error+=" | ";
                impl_->error+=job.config.symbols.front()+"/"+captureChannelName(channel)+": "+detail;continue;
            }
            selected.push_back({channel,source});enable(enabled,channel);
        }
        if (selected.empty()) {++impl_->skippedJobCount;continue;}
        Sink sink{};sink.job=std::move(job);sink.coordinator=std::make_unique<CaptureCoordinator>();
        const auto status=sink.coordinator->startExternalCapture(sink.job.config,enabled,sink.job.channels);
        if (!isOk(status)) {impl_->error=sink.coordinator->lastError();impl_->sinks.push_back(std::move(sink));(void)finalize();return status;}
        impl_->sinks.push_back(std::move(sink));auto& stored=impl_->sinks.back();
        for (const auto& row:selected) {
            impl_->sources.push_back({row.source,&stored.job.config,stored.coordinator.get()});impl_->sourceChannels.push_back(row.channel);
        }
    }
    if (impl_->sources.empty()) {if (impl_->error.empty()) impl_->error="no registered retained capture route";return Status::Unimplemented;}
    impl_->connectionSeen.assign(impl_->sources.size(),0u);impl_->connectionState.assign(impl_->sources.size(),0u);
    impl_->native=std::make_unique<NativeMarketCapture>();
    const auto envPath=impl_->sinks.front().job.config.envPath.string();std::string detail;
    if (!impl_->native->configure(impl_->sources,envPath.c_str(),detail)) {
        impl_->error=std::move(detail);(void)finalize();return Status::Unknown;
    }
    impl_->running=true;impl_->nextManifest=std::chrono::steady_clock::now()+std::chrono::seconds(5);return Status::Ok;
}
bool VenueMultiplexCapture::pollOnce() noexcept {
    if (!impl_ || !impl_->running || !impl_->native) return false;
    const auto prior=impl_->native->committedRows();
    if (!impl_->native->iterate(cxet::os::nowMonotonicNs().raw)) {
        impl_->error=impl_->native->error();if (impl_->error.empty()) impl_->error="native capture owner terminal";
        impl_->running=false;return false;
    }
    impl_->sampleConnections();
    return impl_->native->committedRows()!=prior;
}
void VenueMultiplexCapture::requestStop() noexcept {if (impl_) impl_->running=false;}
Status VenueMultiplexCapture::finalize() noexcept {
    if (!impl_ || impl_->finalized) return Status::Ok;
    impl_->running=false;
    if (impl_->native) {impl_->native->shutdown();const auto detail=impl_->native->error();if (!detail.empty()) impl_->error=detail;}
    Status status=Status::Ok;impl_->finalRows=0u;
    for (auto& sink:impl_->sinks) {impl_->finalRows+=rows(*sink.coordinator);status=aggregateStatus(status,sink.coordinator->finalizeSession());}
    impl_->finalized=true;return status;
}
bool VenueMultiplexCapture::running() const noexcept {return impl_ && impl_->running;}
std::uint64_t VenueMultiplexCapture::totalRows() const noexcept {
    if (!impl_) return 0u;if (impl_->finalized) return impl_->finalRows;
    std::uint64_t total=0u;for (const auto& sink:impl_->sinks) total+=rows(*sink.coordinator);return total;
}
std::size_t VenueMultiplexCapture::activeJobs() const noexcept {return impl_?impl_->sinks.size():0u;}
std::size_t VenueMultiplexCapture::skippedJobs() const noexcept {return impl_?impl_->skippedJobCount:0u;}
std::string VenueMultiplexCapture::lastError() const {return impl_?impl_->error:std::string{};}
} // namespace hftrec::capture
