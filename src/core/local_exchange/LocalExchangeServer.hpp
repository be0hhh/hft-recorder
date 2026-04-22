#pragma once

#include <core/execution/ExecutionVenue.hpp>

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

#include "core/local_exchange/LocalOrderEngine.hpp"

namespace hftrec::local_exchange {

class LocalExchangeServer {
  public:
    LocalExchangeServer() = default;
    LocalExchangeServer(const LocalExchangeServer&) = delete;
    LocalExchangeServer& operator=(const LocalExchangeServer&) = delete;
    ~LocalExchangeServer();

    bool start() noexcept;
    void stop() noexcept;
    void setEventSink(execution::IExecutionEventSink* sink) noexcept { globalLocalOrderEngine().setEventSink(sink); }

    bool running() const noexcept { return running_.load(std::memory_order_acquire); }
    std::uint64_t acceptedCount() const noexcept { return globalLocalOrderEngine().acceptedCount(); }

  private:
    void run_() noexcept;
    bool openSocket_() noexcept;
    void closeSocket_() noexcept;
    void handleClient_(int clientFd) noexcept;

    std::atomic<bool> stopRequested_{false};
    std::atomic<bool> running_{false};
    std::thread thread_{};
    std::string socketPath_{};
    std::atomic<int> listenFd_{-1};
};

}  // namespace hftrec::local_exchange
