#include <gtest/gtest.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "canon/Enums.hpp"
#include "canon/PositionAndExchange.hpp"
#include "canon/Subtypes.hpp"
#include "composite/level_0/SendWsObject.hpp"
#include "core/cxet_bridge/CxetCaptureBridge.hpp"
#include "core/local_exchange/LocalExchangeServer.hpp"
#include "network/local/hftrecorder/Protocol.hpp"

namespace fs = std::filesystem;

namespace {

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

bool connectUnixSocket(const char* path, int& fd) noexcept {
    fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return false;
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    const std::size_t pathLen = std::strlen(path);
    if (pathLen == 0 || pathLen >= sizeof(addr.sun_path)) {
        ::close(fd);
        fd = -1;
        return false;
    }
    std::memcpy(addr.sun_path, path, pathLen + 1u);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::close(fd);
        fd = -1;
        return false;
    }
    return true;
}

bool waitUntilServerAccepts(const char* path) noexcept {
    for (int attempt = 0; attempt < 100; ++attempt) {
        int fd{-1};
        if (connectUnixSocket(path, fd)) {
            ::close(fd);
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

class EnvGuard {
  public:
    EnvGuard(const char* name, const char* value) : name_(name) {
        const char* current = std::getenv(name);
        if (current != nullptr) {
            hadOld_ = true;
            old_ = current;
        }
        ::setenv(name, value, 1);
    }

    ~EnvGuard() {
        if (hadOld_) {
            ::setenv(name_.c_str(), old_.c_str(), 1);
        } else {
            ::unsetenv(name_.c_str());
        }
    }

  private:
    std::string name_{};
    std::string old_{};
    bool hadOld_{false};
};

void seedBookTicker(const char* symbol,
                    std::int64_t bidPrice,
                    std::int64_t askPrice,
                    std::uint64_t tsNs) {
    hftrec::cxet_bridge::CapturedBookTickerRow row{};
    row.symbol = symbol;
    row.bidPriceE8 = bidPrice;
    row.askPriceE8 = askPrice;
    row.bidQtyE8 = 100000000;
    row.askQtyE8 = 100000000;
    row.tsNs = tsNs;
    hftrec::local_exchange::globalLocalOrderEngine().onBookTicker(row);
}

}  // namespace

TEST(LocalExchangeServer, MarketOrderReturnsClosedAckWhenL1Available) {
    const fs::path socketPath = fs::temp_directory_path() /
        ("hftrec_local_exchange_test_" + std::to_string(::getpid()) + ".sock");
    std::error_code ec;
    fs::remove(socketPath, ec);

    EnvGuard env{cxet::network::local::hftrecorder::kSocketEnvName,
                 socketPath.string().c_str()};
    hftrec::local_exchange::LocalExchangeServer server{};
    ASSERT_TRUE(server.start());
    ASSERT_TRUE(waitUntilServerAccepts(socketPath.string().c_str()));

    seedBookTicker("btcusdt", 99000000, 101000000, 77u);

    cxet::network::local::hftrecorder::OrderRequestFrame request{};
    request.operationRaw = static_cast<std::uint8_t>(cxet::UnifiedRequestSpec::Operation::SendWs);
    request.objectRaw = static_cast<std::uint8_t>(cxet::composite::out::WsSendObject::Order);
    request.exchangeRaw = canon::kExchangeIdHftRecorderLocal.raw;
    request.marketRaw = canon::kMarketTypeFutures.raw;
    request.typeRaw = static_cast<std::uint8_t>(canon::OrderType::Market);
    request.sideSet = 1u;
    request.sideRaw = 1u;
    request.quantityRaw = 100000000;
    std::memcpy(request.symbol, "btcusdt", 8u);

    int fd{-1};
    ASSERT_TRUE(connectUnixSocket(socketPath.string().c_str(), fd));
    cxet::network::local::hftrecorder::OrderAckFrame ack{};
    ASSERT_TRUE(writeAll(fd, &request, sizeof(request)));
    ASSERT_TRUE(readAll(fd, &ack, sizeof(ack)));
    ::close(fd);

    EXPECT_TRUE(cxet::network::local::hftrecorder::isValidAck(ack));
    EXPECT_EQ(ack.success, 1u);
    EXPECT_EQ(ack.statusRaw, static_cast<std::uint8_t>(canon::OrderStatus::Closed));
    EXPECT_EQ(ack.exchangeRaw, canon::kExchangeIdHftRecorderLocal.raw);
    EXPECT_EQ(ack.marketRaw, canon::kMarketTypeFutures.raw);
    EXPECT_STREQ(ack.symbol, "btcusdt");
    EXPECT_NE(ack.orderId[0], '\0');
    EXPECT_EQ(server.acceptedCount(), 1u);

    server.stop();
    fs::remove(socketPath, ec);
}

TEST(LocalExchangeServer, RejectsMarketOrderWithoutL1) {
    const fs::path socketPath = fs::temp_directory_path() /
        ("hftrec_local_exchange_reject_test_" + std::to_string(::getpid()) + ".sock");
    std::error_code ec;
    fs::remove(socketPath, ec);

    EnvGuard env{cxet::network::local::hftrecorder::kSocketEnvName,
                 socketPath.string().c_str()};
    hftrec::local_exchange::LocalExchangeServer server{};
    ASSERT_TRUE(server.start());
    ASSERT_TRUE(waitUntilServerAccepts(socketPath.string().c_str()));

    cxet::network::local::hftrecorder::OrderRequestFrame request{};
    request.operationRaw = static_cast<std::uint8_t>(cxet::UnifiedRequestSpec::Operation::SendWs);
    request.objectRaw = static_cast<std::uint8_t>(cxet::composite::out::WsSendObject::Order);
    request.exchangeRaw = canon::kExchangeIdHftRecorderLocal.raw;
    request.marketRaw = canon::kMarketTypeFutures.raw;
    request.typeRaw = static_cast<std::uint8_t>(canon::OrderType::Market);
    request.sideSet = 1u;
    request.sideRaw = 0u;
    request.quantityRaw = 100000000;
    std::memcpy(request.symbol, "btcusdt", 8u);

    int fd{-1};
    ASSERT_TRUE(connectUnixSocket(socketPath.string().c_str(), fd));
    cxet::network::local::hftrecorder::OrderAckFrame ack{};
    ASSERT_TRUE(writeAll(fd, &request, sizeof(request)));
    ASSERT_TRUE(readAll(fd, &ack, sizeof(ack)));
    ::close(fd);

    EXPECT_EQ(ack.success, 0u);
    EXPECT_EQ(ack.statusRaw, static_cast<std::uint8_t>(canon::OrderStatus::Rejected));
    EXPECT_EQ(ack.errorCode, static_cast<std::uint32_t>(hftrec::local_exchange::LocalOrderErrorCode::MissingBookTicker));
    EXPECT_EQ(server.acceptedCount(), 0u);

    server.stop();
    fs::remove(socketPath, ec);
}
