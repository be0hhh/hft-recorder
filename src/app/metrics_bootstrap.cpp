#include "app/metrics_bootstrap.hpp"

#include "core/metrics/Metrics.hpp"

#include <atomic>
#include <cstring>
#include <cstdlib>
#include <memory>
#include <string>
#include <thread>

#if HFTREC_WITH_CXET
#include "metrics/MetricsControl.hpp"
#include "metrics/MetricsThread.hpp"
#include "metrics/ProbeRegistry.hpp"
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

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

bool metricsOff() noexcept {
    const char* raw = std::getenv("HFTREC_METRICS_MODE");
    return raw != nullptr && std::strcmp(raw, "off") == 0;
}

#if HFTREC_WITH_CXET
cxet::metrics::Mode metricsMode() noexcept {
    const char* raw = std::getenv("HFTREC_METRICS_MODE");
    if (raw == nullptr || raw[0] == '\0') return cxet::metrics::Mode::CountersOnly;
    if (std::strcmp(raw, "off") == 0) return cxet::metrics::Mode::Off;
    if (std::strcmp(raw, "counters") == 0) return cxet::metrics::Mode::CountersOnly;
    if (std::strcmp(raw, "sampled") == 0) return cxet::metrics::Mode::SampledLatency;
    return cxet::metrics::Mode::FullLatency;
}
#else
class SimpleMetricsThread {
  public:
    explicit SimpleMetricsThread(std::uint16_t port) noexcept : port_(port) {}
    ~SimpleMetricsThread() { stop(); }

    void start() noexcept {
        stopRequested_.store(false, std::memory_order_release);
        try {
            thread_ = std::thread([this]() { run_(); });
        } catch (...) {
        }
    }

    void stop() noexcept {
        stopRequested_.store(true, std::memory_order_release);
        const int fd = listenFd_.exchange(-1, std::memory_order_acq_rel);
        if (fd >= 0) {
            ::shutdown(fd, SHUT_RDWR);
            ::close(fd);
        }
        if (thread_.joinable()) thread_.join();
    }

  private:
    void run_() noexcept {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) return;
        listenFd_.store(fd, std::memory_order_release);
        int yes = 1;
        (void)::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port_);
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            ::close(fd);
            listenFd_.store(-1, std::memory_order_release);
            return;
        }
        if (::listen(fd, 16) != 0) {
            ::close(fd);
            listenFd_.store(-1, std::memory_order_release);
            return;
        }

        while (!stopRequested_.load(std::memory_order_acquire)) {
            const int client = ::accept(fd, nullptr, nullptr);
            if (client < 0) {
                if (errno == EINTR) continue;
                if (stopRequested_.load(std::memory_order_acquire)) break;
                continue;
            }
            handleClient_(client);
            ::close(client);
        }
        const int old = listenFd_.exchange(-1, std::memory_order_acq_rel);
        if (old >= 0) ::close(old);
    }

    static void handleClient_(int client) noexcept {
        char buffer[1024];
        const ssize_t n = ::recv(client, buffer, sizeof(buffer) - 1u, 0);
        if (n <= 0) return;
        buffer[n] = '\0';
        const std::string_view request{buffer, static_cast<std::size_t>(n)};
        std::string body;
        std::string status = "200 OK";
        std::string contentType = "text/plain; version=0.0.4; charset=utf-8";
        if (request.rfind("GET /metrics ", 0) == 0 || request.rfind("GET /metrics?", 0) == 0) {
            renderHftrecMetrics(body);
        } else if (request.rfind("GET /-/ready ", 0) == 0) {
            body = "hft-recorder metrics ready\n";
        } else {
            status = "404 Not Found";
            contentType = "text/plain; charset=utf-8";
            body = "not found\n";
        }
        const std::string header = "HTTP/1.1 " + status + "\r\nContent-Type: " + contentType
            + "\r\nContent-Length: " + std::to_string(body.size())
            + "\r\nConnection: close\r\n\r\n";
        (void)::send(client, header.data(), header.size(), MSG_NOSIGNAL);
        if (!body.empty()) (void)::send(client, body.data(), body.size(), MSG_NOSIGNAL);
    }

    std::uint16_t port_{8080};
    std::atomic<bool> stopRequested_{false};
    std::atomic<int> listenFd_{-1};
    std::thread thread_{};
};
#endif

}  // namespace

struct MetricsBootstrap::Impl {
#if HFTREC_WITH_CXET
    cxet::metrics::MetricsThread thread;
    Impl() noexcept : thread(metricsPort()) {}
#else
    SimpleMetricsThread thread;
    Impl() noexcept : thread(metricsPort()) {}
#endif
};

MetricsBootstrap::MetricsBootstrap() noexcept {
#if HFTREC_WITH_CXET
    if (metricsMode() == cxet::metrics::Mode::Off) {
        cxet::metrics::setMode(cxet::metrics::Mode::Off);
        return;
    }
#else
    if (metricsOff()) return;
#endif
    impl_ = new Impl();
    hftrec::metrics::init();
#if HFTREC_WITH_CXET
    cxet::metrics::ProbeRegistry::setExtraRenderHook(&renderHftrecMetrics);
    cxet::metrics::setMode(metricsMode());
#endif
    impl_->thread.start();
}

MetricsBootstrap::~MetricsBootstrap() noexcept {
    if (impl_ != nullptr) {
        impl_->thread.stop();
        delete impl_;
        impl_ = nullptr;
    }
#if HFTREC_WITH_CXET
    cxet::metrics::ProbeRegistry::setExtraRenderHook(nullptr);
#endif
    hftrec::metrics::shutdown();
}

}  // namespace hftrec::app