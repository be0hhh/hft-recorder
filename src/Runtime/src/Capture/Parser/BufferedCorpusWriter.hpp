#pragma once
#include "hftrec/CorpusContract/BinaryMarketCorpusWriter.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <vector>

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
  [[nodiscard]] Status appendProgress(const corpus::CorpusProducerProgress& progress) noexcept;
  [[nodiscard]] Status appendGap(const corpus::BinaryMarketGap& gap) noexcept;
  [[nodiscard]] Status appendReferencePayload(std::span<const std::uint8_t> payload) noexcept;
  [[nodiscard]] std::shared_ptr<const corpus::CorpusCut> latestCut() const noexcept {return writer_.latestCut();}
  [[nodiscard]] std::shared_ptr<corpus::CorpusPublication> publication() const noexcept {return writer_.publication();}
  [[nodiscard]] Status finish() noexcept;
  // After Parser publication is frozen and beginFinalDrain() succeeds, retry
  // only records rejected before a quota write. I/O failures are never retried.
  [[nodiscard]] Status drainFrozen() noexcept;
  [[nodiscard]] corpus::BinaryMarketWriterSnapshot snapshot() const noexcept;
  [[nodiscard]] Status status() const noexcept { return status_.load(std::memory_order_acquire); }
 private:
  void run() noexcept;
  [[nodiscard]] Status enqueue(const void* bytes,std::size_t size,std::uint8_t kind) noexcept;
  [[nodiscard]] Status consume(std::uint64_t index) noexcept;
  struct Item final {
    std::uint8_t kind{};
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
  std::mutex wakeMutex_;
  std::condition_variable wakeCondition_;
  std::mutex referenceMutex_;
  std::vector<std::uint8_t> referencePayload_;
  bool referencePending_{};
  mutable std::mutex snapshotMutex_;
  corpus::BinaryMarketWriterSnapshot snapshot_{};
};
}
