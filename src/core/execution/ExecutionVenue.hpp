#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace hftrec::execution {

enum class ExecutionEventKind : std::uint8_t {
    Ack = 0,
    Reject = 1,
    StateChange = 2,
    Fill = 3,
    PositionChange = 4,
    BalanceChange = 5,
    Fee = 6,
    Funding = 7,
};

struct ExecutionEvent {
    ExecutionEventKind kind{ExecutionEventKind::Ack};
    std::string symbol{};
    std::string orderId{};
    std::string clientOrderId{};
    std::string execId{};
    std::uint8_t exchangeRaw{0};
    std::uint8_t marketRaw{0};
    std::uint8_t sideRaw{0};
    std::uint8_t typeRaw{0};
    std::uint8_t timeInForceRaw{0};
    std::uint8_t reduceOnly{0};
    std::uint8_t statusRaw{0};
    std::uint32_t errorCode{0};
    std::int64_t quantityRaw{0};
    std::int64_t priceRaw{0};
    std::int64_t fillPriceE8{0};
    std::int64_t filledQtyRaw{0};
    std::int64_t feeRaw{0};
    std::int64_t realizedPnlRaw{0};
    std::int64_t positionQtyRaw{0};
    std::int64_t avgEntryPriceE8{0};
    std::int64_t walletBalanceRaw{0};
    std::int64_t availableBalanceRaw{0};
    std::int64_t equityRaw{0};
    std::uint64_t tsNs{0};
    bool success{false};
};

struct ExecutionEventBatch {
    std::vector<ExecutionEvent> events{};
};

class IExecutionEventSink {
  public:
    IExecutionEventSink() = default;
    IExecutionEventSink(const IExecutionEventSink&) = delete;
    IExecutionEventSink& operator=(const IExecutionEventSink&) = delete;
    virtual ~IExecutionEventSink() = default;

    virtual void onExecutionEvent(const ExecutionEvent& event) noexcept = 0;
};

class IExecutionEventSource {
  public:
    IExecutionEventSource() = default;
    IExecutionEventSource(const IExecutionEventSource&) = delete;
    IExecutionEventSource& operator=(const IExecutionEventSource&) = delete;
    virtual ~IExecutionEventSource() = default;

    virtual ExecutionEventBatch readAll() const = 0;
    virtual ExecutionEventBatch readRange(std::uint64_t fromTsNs, std::uint64_t toTsNs) const = 0;
};

class IExecutionVenue {
  public:
    IExecutionVenue() = default;
    IExecutionVenue(const IExecutionVenue&) = delete;
    IExecutionVenue& operator=(const IExecutionVenue&) = delete;
    virtual ~IExecutionVenue() = default;

    virtual void setEventSink(IExecutionEventSink* sink) noexcept = 0;
    virtual std::uint64_t acceptedCount() const noexcept = 0;
};

class LiveExecutionStore final : public IExecutionEventSink, public IExecutionEventSource {
  public:
    void onExecutionEvent(const ExecutionEvent& event) noexcept override;

    ExecutionEventBatch readAll() const override;
    ExecutionEventBatch readRange(std::uint64_t fromTsNs, std::uint64_t toTsNs) const override;
    void clear() noexcept;

  private:
    mutable std::mutex mutex_{};
    ExecutionEventBatch batch_{};
};

}  // namespace hftrec::execution
