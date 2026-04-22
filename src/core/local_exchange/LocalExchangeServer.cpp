#include "core/local_exchange/LocalExchangeServer.hpp"

#include "core/local_exchange/LocalOrderEngine.hpp"
#include "network/local/hftrecorder/Protocol.hpp"

#include <cerrno>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace hftrec::local_exchange {
namespace {

const char* configuredSocketPath() noexcept {
    const char* fromEnv = std::getenv(cxet::network::local::hftrecorder::kSocketEnvName);
    if (fromEnv != nullptr && fromEnv[0] != '\0') return fromEnv;
    return cxet::network::local::hftrecorder::kDefaultSocketPath;
}

bool readAll(int fd, void* data, std::size_t size) noexcept {
    auto* bytes = static_cast<char*>(data);
    std::size_t read = 0;
    while (read < size) {
        const ssize_t n = ::recv(fd, bytes + read, size - read, 0);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        read += static_cast<std::size_t>(n);
    }
    return true;
}

bool writeAll(int fd, const void* data, std::size_t size) noexcept {
    const auto* bytes = static_cast<const char*>(data);
    std::size_t written = 0;
    while (written < size) {
        const ssize_t n = ::send(fd, bytes + written, size - written, MSG_NOSIGNAL);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;
        written += static_cast<std::size_t>(n);
    }
    return true;
}

}  // namespace

LocalExchangeServer::~LocalExchangeServer() {
    stop();
}

bool LocalExchangeServer::start() noexcept {
    if (running()) return true;
    globalLocalOrderEngine().reset();
    socketPath_ = configuredSocketPath();
    stopRequested_.store(false, std::memory_order_release);
    try {
        thread_ = std::thread([this]() { run_(); });
    } catch (...) {
        running_.store(false, std::memory_order_release);
        return false;
    }
    return true;
}

void LocalExchangeServer::stop() noexcept {
    stopRequested_.store(true, std::memory_order_release);
    closeSocket_();
    if (thread_.joinable()) thread_.join();
    if (!socketPath_.empty()) ::unlink(socketPath_.c_str());
    running_.store(false, std::memory_order_release);
    globalLocalOrderEngine().reset();
}

bool LocalExchangeServer::openSocket_() noexcept {
    sockaddr_un addr{};
    if (socketPath_.empty() || socketPath_.size() >= sizeof(addr.sun_path)) return false;
    const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;
    listenFd_.store(fd, std::memory_order_release);

    ::unlink(socketPath_.c_str());
    addr.sun_family = AF_UNIX;
    std::memcpy(addr.sun_path, socketPath_.c_str(), socketPath_.size() + 1u);
    if (::bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        closeSocket_();
        return false;
    }
    if (::listen(fd, 32) != 0) {
        closeSocket_();
        return false;
    }
    return true;
}

void LocalExchangeServer::closeSocket_() noexcept {
    const int fd = listenFd_.exchange(-1, std::memory_order_acq_rel);
    if (fd >= 0) {
        ::shutdown(fd, SHUT_RDWR);
        ::close(fd);
    }
}

void LocalExchangeServer::run_() noexcept {
    if (!openSocket_()) {
        running_.store(false, std::memory_order_release);
        return;
    }
    running_.store(true, std::memory_order_release);
    while (!stopRequested_.load(std::memory_order_acquire)) {
        const int fd = listenFd_.load(std::memory_order_acquire);
        if (fd < 0) break;
        const int clientFd = ::accept(fd, nullptr, nullptr);
        if (clientFd < 0) {
            if (errno == EINTR) continue;
            if (stopRequested_.load(std::memory_order_acquire)) break;
            continue;
        }
        handleClient_(clientFd);
        ::close(clientFd);
    }
    closeSocket_();
    running_.store(false, std::memory_order_release);
}

void LocalExchangeServer::handleClient_(int clientFd) noexcept {
    using namespace cxet::network::local::hftrecorder;

    OrderRequestFrame request{};
    OrderAckFrame ack{};
    if (!readAll(clientFd, &request, sizeof(request)) || !isValidRequest(request)) {
        ack.success = 0u;
        ack.statusRaw = static_cast<std::uint8_t>(canon::OrderStatus::Rejected);
        ack.errorCode = static_cast<std::uint32_t>(LocalOrderErrorCode::InvalidRequest);
        writeAll(clientFd, &ack, sizeof(ack));
        return;
    }

    globalLocalOrderEngine().submitOrder(request, ack);
    writeAll(clientFd, &ack, sizeof(ack));
}

}  // namespace hftrec::local_exchange
