#include "CaptureCoordinator.hpp"
#include "CaptureChannelSupport.hpp"
#include "../Session/MarketData/NativeMarketCapture.hpp"
#include "CaptureCoordinatorRuntimeHelpers.hpp"
#include "cxet/Os/ClockSource.hpp"
#include <chrono>
#include <thread>

namespace hftrec::capture {
void CaptureCoordinator::marketDataManagerLoop_(CaptureConfig config) noexcept {
    std::unique_ptr<NativeMarketCapture> native;
    std::uint8_t activeMask=0u;
    while (!marketDataStop_.load(std::memory_order_acquire)) {
        const auto desired=static_cast<std::uint8_t>(
            (desiredTrades_.load(std::memory_order_acquire)?1u:0u)|
            (desiredBookTicker_.load(std::memory_order_acquire)?2u:0u)|
            (desiredOrderbook_.load(std::memory_order_acquire)?4u:0u));
        if (!desired) break;
        if (desired!=activeMask) {
            if (native) native->shutdown();native.reset();
            std::vector<NativeCaptureSource> sources;
            const CaptureChannel channels[]{CaptureChannel::Trades,CaptureChannel::BookTicker,CaptureChannel::Orderbook};
            std::string error;
            for (std::size_t i=0u;i<3u;++i) {
                if (!(desired&(1u<<i))) continue;
                cxet::runtime::market::ConfiguredMarketSource source{};
                if (!makeConfiguredCaptureSource(config,channels[i],source,error)) break;
                sources.push_back({source,&config,this});
            }
            native=std::make_unique<NativeMarketCapture>();
            const auto envPath=config.envPath.string();
            if (!error.empty() || !native->configure(sources,envPath.c_str(),error)) {
                if (!marketDataStop_.load(std::memory_order_acquire) && anyManagedMarketDataDesired_())
                    noteExternalCaptureTerminal(error.empty()?"native capture startup rejected":error);
                break;
            }
            activeMask=desired;
        }
        if (!native->iterate(cxet::os::nowMonotonicNs().raw)) {
            auto error=native->error();if (error.empty()) error="native capture owner terminal";
            // An explicit operator stop may end an in-flight iteration. Only
            // an unexpected owner terminal fences the durable session.
            if (!marketDataStop_.load(std::memory_order_acquire) && anyManagedMarketDataDesired_())
                noteExternalCaptureTerminal(error);
            break;
        }
    }
    if (native) native->shutdown();
    (void)refreshExternalManifest();
    tradesRunning_.store(false,std::memory_order_release);bookTickerRunning_.store(false,std::memory_order_release);
    orderbookRunning_.store(false,std::memory_order_release);marketDataRunning_.store(false,std::memory_order_release);
}
void CaptureCoordinator::reapStoppedThreads() noexcept {
    if (marketDataThread_.joinable() && !marketDataRunning_.load(std::memory_order_acquire)) marketDataThread_.join();
}
} // namespace hftrec::capture
