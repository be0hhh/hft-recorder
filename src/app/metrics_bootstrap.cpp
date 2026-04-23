#include "app/metrics_bootstrap.hpp"

#include "core/metrics/Metrics.hpp"
#include "metrics/MetricsControl.hpp"
#include "metrics/MetricsThread.hpp"
#include "metrics/ProbeRegistry.hpp"

#include <cstring>
#include <cstdlib>
#include <memory>
#include <string>

namespace hftrec::app {

namespace {

void renderHftrecMetrics(std::string& out) {
    hftrec::metrics::renderPrometheus(out);
}

std::uint16_t metricsPort() noexcept {
    const char* raw = std::getenv("HFTREC_METRICS_PORT");
    if (raw == nullptr || raw[0] == '\0') return 8080u;
    const unsigned long parsed = std::strtoul(raw, nullptr, 10);
    if (parsed == 0u || parsed > 65535u) return 8080u;
    return static_cast<std::uint16_t>(parsed);
}

cxet::metrics::Mode metricsMode() noexcept {
    const char* raw = std::getenv("HFTREC_METRICS_MODE");
    if (raw == nullptr || raw[0] == '\0') return cxet::metrics::Mode::CountersOnly;
    if (std::strcmp(raw, "off") == 0) return cxet::metrics::Mode::Off;
    if (std::strcmp(raw, "counters") == 0) return cxet::metrics::Mode::CountersOnly;
    if (std::strcmp(raw, "sampled") == 0) return cxet::metrics::Mode::SampledLatency;
    return cxet::metrics::Mode::FullLatency;
}

}  // namespace

struct MetricsBootstrap::Impl {
    cxet::metrics::MetricsThread thread;

    Impl() noexcept : thread(metricsPort()) {}
};

MetricsBootstrap::MetricsBootstrap() noexcept {
    if (metricsMode() == cxet::metrics::Mode::Off) {
        cxet::metrics::setMode(cxet::metrics::Mode::Off);
        return;
    }
    impl_ = new Impl();
    hftrec::metrics::init();
    cxet::metrics::ProbeRegistry::setExtraRenderHook(&renderHftrecMetrics);
    cxet::metrics::setMode(metricsMode());
    impl_->thread.start();
}

MetricsBootstrap::~MetricsBootstrap() noexcept {
    if (impl_ != nullptr) {
        impl_->thread.stop();
        delete impl_;
        impl_ = nullptr;
    }
    cxet::metrics::ProbeRegistry::setExtraRenderHook(nullptr);
    hftrec::metrics::shutdown();
}

}  // namespace hftrec::app
