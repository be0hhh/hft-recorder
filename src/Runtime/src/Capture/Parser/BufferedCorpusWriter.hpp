#pragma once
#include "hftrec/CorpusContract/BinaryMarketCorpusWriter.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>

namespace hftrec::capture {
// Recorder-owned single-producer queue. Only its worker touches the writer
// until finish(); capture ingestion copies one record without waiting on disk.
class BufferedCorpusWriter final {
 public:
  explicit BufferedCorpusWriter(corpus::BinaryMarketCorpusWriter& writer) noexcept;
  ~BufferedCorpusWriter() noexcept;
  BufferedCorpusWriter(const BufferedCorpusWriter&)=delete;
  BufferedCorpusWriter& operator=(const BufferedCorpusWriter&)=delete;
  [[nodiscard]] Status start(std::uint32_t capacity) noexcept;
  [[nodiscard]] Status append(const corpus::BinaryMarketRecord& record) noexcept;
  [[nodiscard]] Status appendSource(const corpus::BinaryMarketSource& source) noexcept;
  [[nodiscard]] Status finish() noexcept;
  // After Parser publication is frozen and beginFinalDrain() succeeds, retry
  // only records rejected before a quota write. I/O failures are never retried.
  [[nodiscard]] Status drainFrozen() noexcept;
  [[nodiscard]] corpus::BinaryMarketWriterSnapshot snapshot() const noexcept;
  [[nodiscard]] Status status() const noexcept { return status_.load(std::memory_order_acquire); }
 private:
  void run() noexcept;
  [[nodiscard]] Status enqueue(const void* bytes,std::size_t size,bool source) noexcept;
  [[nodiscard]] Status consume(std::uint64_t index) noexcept;
  struct Item final {
    bool source{false};
    std::array<std::uint8_t,sizeof(corpus::BinaryMarketRecord)> bytes{};
  };
  corpus::BinaryMarketCorpusWriter& writer_;
  std::unique_ptr<Item[]> records_;
  std::uint32_t capacity_{};
  alignas(64) std::atomic<std::uint64_t> written_{};
  alignas(64) std::atomic<std::uint64_t> read_{};
  std::atomic<std::uint64_t> wakeEpoch_{};
  std::atomic<Status> status_{Status::Ok};
  std::atomic<bool> stop_{};
  std::thread thread_;
  mutable std::mutex snapshotMutex_;
  corpus::BinaryMarketWriterSnapshot snapshot_{};
};
}
