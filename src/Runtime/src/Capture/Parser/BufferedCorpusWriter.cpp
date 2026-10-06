#include "BufferedCorpusWriter.hpp"
#include <algorithm>
#include <new>

namespace hftrec::capture {
BufferedCorpusWriter::BufferedCorpusWriter(corpus::BinaryMarketCorpusWriter& writer) noexcept:writer_(writer) {}
BufferedCorpusWriter::~BufferedCorpusWriter() noexcept { (void)finish(); }
Status BufferedCorpusWriter::start(std::uint32_t capacity) noexcept {
  if(thread_.joinable() || capacity==0 || capacity>65536 || !writer_.snapshot().active)
    return Status::InvalidArgument;
  records_.reset(new(std::nothrow) Item[capacity]);
  if(!records_)return Status::IoError;
  capacity_=capacity;written_.store(0);read_.store(0);stop_.store(false);
  status_.store(Status::Ok);snapshot_=writer_.snapshot();
  try { thread_=std::thread(&BufferedCorpusWriter::run,this); }
  catch(...) { records_.reset();capacity_=0;return Status::IoError; }
  return Status::Ok;
}
Status BufferedCorpusWriter::append(const corpus::BinaryMarketRecord& record) noexcept {
  return enqueue(&record,sizeof(record),false);
}
Status BufferedCorpusWriter::appendSource(const corpus::BinaryMarketSource& source) noexcept {
  return enqueue(&source,sizeof(source),true);
}
Status BufferedCorpusWriter::enqueue(const void* bytes,std::size_t size,bool source) noexcept {
  const auto status=status_.load(std::memory_order_acquire);
  if(!isOk(status))return status;
  if(capacity_==0 || stop_.load(std::memory_order_acquire))return Status::InvalidArgument;
  const auto write=written_.load(std::memory_order_relaxed);
  const auto read=read_.load(std::memory_order_acquire);
  if(write-read>=capacity_ || write==UINT64_MAX)return Status::Unknown;
  auto& item=records_[write%capacity_];
  item.source=source;
  std::copy_n(static_cast<const std::uint8_t*>(bytes),size,item.bytes.begin());
  written_.store(write+1,std::memory_order_release);
  wakeEpoch_.fetch_add(1,std::memory_order_release);wakeEpoch_.notify_one();
  return Status::Ok;
}
Status BufferedCorpusWriter::consume(std::uint64_t index) noexcept {
  const auto& item=records_[index%capacity_];
  if(item.source) {
    corpus::BinaryMarketSource source{};
    std::copy_n(item.bytes.begin(),sizeof(source),reinterpret_cast<std::uint8_t*>(&source));
    return writer_.registerSources(std::span<const corpus::BinaryMarketSource>(&source,1));
  }
  corpus::BinaryMarketRecord record{};
  std::copy_n(item.bytes.begin(),sizeof(record),reinterpret_cast<std::uint8_t*>(&record));
  return writer_.append(record);
}
void BufferedCorpusWriter::run() noexcept {
  std::uint64_t read=0;
  for(;;) {
    const auto wake=wakeEpoch_.load(std::memory_order_acquire);
    const auto write=written_.load(std::memory_order_acquire);
    if(read==write) {
      if(stop_.load(std::memory_order_acquire))break;
      wakeEpoch_.wait(wake,std::memory_order_acquire);continue;
    }
    const auto status=consume(read);
    if(!isOk(status)) { status_.store(status,std::memory_order_release);break; }
    read_.store(++read,std::memory_order_release);
    if((read&31u)==0u) { std::lock_guard lock(snapshotMutex_);snapshot_=writer_.snapshot(); }
  }
  std::lock_guard lock(snapshotMutex_);snapshot_=writer_.snapshot();
}
Status BufferedCorpusWriter::finish() noexcept {
  if(thread_.joinable()) {
    stop_.store(true,std::memory_order_release);
    wakeEpoch_.fetch_add(1,std::memory_order_release);wakeEpoch_.notify_one();
    thread_.join();
  }
  return status_.load(std::memory_order_acquire);
}
Status BufferedCorpusWriter::drainFrozen() noexcept {
  if(thread_.joinable() || !stop_.load(std::memory_order_acquire))return Status::InvalidArgument;
  const auto status=status_.load(std::memory_order_acquire);
  if(status!=Status::Ok && status!=Status::OutOfRange)return status;
  auto read=read_.load(std::memory_order_acquire);
  const auto write=written_.load(std::memory_order_acquire);
  for(;read<write;++read) {
    const auto result=consume(read);
    if(!isOk(result))return result;
    read_.store(read+1,std::memory_order_release);
  }
  status_.store(Status::Ok,std::memory_order_release);
  std::lock_guard lock(snapshotMutex_);snapshot_=writer_.snapshot();return Status::Ok;
}
corpus::BinaryMarketWriterSnapshot BufferedCorpusWriter::snapshot() const noexcept {
  std::lock_guard lock(snapshotMutex_);return snapshot_;
}
}
