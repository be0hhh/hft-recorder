#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

#include "core/common/Status.hpp"
#include "core/replay/EventRows.hpp"

namespace hftrec::storage {

struct EventBatch {
    std::vector<replay::TradeRow> trades{};
    std::vector<replay::BookTickerRow> bookTickers{};
    std::vector<replay::DepthRow> depths{};
    std::vector<replay::SnapshotDocument> snapshots{};
};

struct EventStoreStats {
    std::uint64_t tradesTotal{0};
    std::uint64_t bookTickersTotal{0};
    std::uint64_t depthsTotal{0};
    std::uint64_t snapshotsTotal{0};
    std::uint64_t version{0};
};

class IEventSink {
  public:
    IEventSink() = default;
    IEventSink(const IEventSink&) = delete;
    IEventSink& operator=(const IEventSink&) = delete;
    virtual ~IEventSink() = default;

    virtual Status appendTrade(const replay::TradeRow& row) noexcept = 0;
    virtual Status appendBookTicker(const replay::BookTickerRow& row) noexcept = 0;
    virtual Status appendDepth(const replay::DepthRow& row) noexcept = 0;
    virtual Status appendSnapshot(const replay::SnapshotDocument& snapshot,
                                  std::uint64_t snapshotIndex) noexcept = 0;
    virtual Status flush() noexcept = 0;
    virtual Status close() noexcept = 0;
};

class IEventSource {
  public:
    IEventSource() = default;
    IEventSource(const IEventSource&) = delete;
    IEventSource& operator=(const IEventSource&) = delete;
    virtual ~IEventSource() = default;

    virtual EventBatch readAll() const = 0;
    virtual EventBatch readRange(std::int64_t fromTsNs, std::int64_t toTsNs) const = 0;
    virtual EventBatch readSince(std::size_t tradeOffset,
                                 std::size_t bookTickerOffset,
                                 std::size_t depthOffset,
                                 std::size_t snapshotOffset) const;
};

class IHotEventCache : public IEventSink, public IEventSource {
  public:
    IHotEventCache() = default;
    IHotEventCache(const IHotEventCache&) = delete;
    IHotEventCache& operator=(const IHotEventCache&) = delete;
    ~IHotEventCache() override = default;

    virtual EventStoreStats stats() const noexcept = 0;
    virtual void clear() noexcept = 0;
};

class IStorageBackend : public IEventSink {
  public:
    IStorageBackend() = default;
    IStorageBackend(const IStorageBackend&) = delete;
    IStorageBackend& operator=(const IStorageBackend&) = delete;
    ~IStorageBackend() override = default;

    virtual const char* backendId() const noexcept = 0;
    virtual EventStoreStats stats() const noexcept = 0;
};

class LiveEventStore final : public IHotEventCache {
  public:
    Status appendTrade(const replay::TradeRow& row) noexcept override;
    Status appendBookTicker(const replay::BookTickerRow& row) noexcept override;
    Status appendDepth(const replay::DepthRow& row) noexcept override;
    Status appendSnapshot(const replay::SnapshotDocument& snapshot,
                          std::uint64_t snapshotIndex) noexcept override;
    Status flush() noexcept override;
    Status close() noexcept override;

    EventBatch readAll() const override;
    EventBatch readRange(std::int64_t fromTsNs, std::int64_t toTsNs) const override;
    EventBatch readSince(std::size_t tradeOffset,
                         std::size_t bookTickerOffset,
                         std::size_t depthOffset,
                         std::size_t snapshotOffset) const override;
    EventStoreStats stats() const noexcept override;
    void clear() noexcept override;

  private:
    mutable std::mutex mutex_{};
    EventBatch events_{};
    std::uint64_t version_{0};
};

class CompositeEventSink final : public IEventSink {
  public:
    void clearSinks() noexcept;
    void addSink(IEventSink* sink) noexcept;

    Status appendTrade(const replay::TradeRow& row) noexcept override;
    Status appendBookTicker(const replay::BookTickerRow& row) noexcept override;
    Status appendDepth(const replay::DepthRow& row) noexcept override;
    Status appendSnapshot(const replay::SnapshotDocument& snapshot,
                          std::uint64_t snapshotIndex) noexcept override;
    Status flush() noexcept override;
    Status close() noexcept override;

  private:
    std::vector<IEventSink*> sinks_{};
};

}  // namespace hftrec::storage
