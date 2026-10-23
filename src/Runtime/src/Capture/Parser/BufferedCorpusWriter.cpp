#include "BufferedCorpusWriter.hpp"
#include <algorithm>
#include <new>
#include <chrono>

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
  return enqueue(&record,sizeof(record),0);
}
Status BufferedCorpusWriter::appendSource(const corpus::BinaryMarketSource& source) noexcept {
  return enqueue(&source,sizeof(source),1);
}
Status BufferedCorpusWriter::enqueue(const void* bytes,std::size_t size,std::uint8_t kind) noexcept {
  const auto status=status_.load(std::memory_order_acquire);
  if(!isOk(status))return status;
  if(capacity_==0 || stop_.load(std::memory_order_acquire))return Status::InvalidArgument;
  const auto write=written_.load(std::memory_order_relaxed);
  const auto read=read_.load(std::memory_order_acquire);
  if(write-read>=capacity_ || write==UINT64_MAX)return Status::Unknown;
  auto& item=records_[write%capacity_];
  item.kind=kind;
  std::copy_n(static_cast<const std::uint8_t*>(bytes),size,item.bytes.begin());
  written_.store(write+1,std::memory_order_release);
  wakeEpoch_.fetch_add(1,std::memory_order_release);wakeCondition_.notify_one();
  return Status::Ok;
}
Status BufferedCorpusWriter::consume(std::uint64_t index) noexcept {
  const auto& item=records_[index%capacity_];
  if(item.kind==1) {
    corpus::BinaryMarketSource source{};
    std::copy_n(item.bytes.begin(),sizeof(source),reinterpret_cast<std::uint8_t*>(&source));
    return writer_.registerSources(std::span<const corpus::BinaryMarketSource>(&source,1));
  }
  if(item.kind==2) {
    corpus::CorpusProducerProgress progress{};
    std::copy_n(item.bytes.begin(),sizeof(progress),reinterpret_cast<std::uint8_t*>(&progress));
    return writer_.updateProgress(progress);
  }
  if(item.kind==3) {
    corpus::BinaryMarketGap gap{};
    std::copy_n(item.bytes.begin(),sizeof(gap),reinterpret_cast<std::uint8_t*>(&gap));
    return writer_.appendGap(gap);
  }
  corpus::BinaryMarketRecord record{};
  std::copy_n(item.bytes.begin(),sizeof(record),reinterpret_cast<std::uint8_t*>(&record));
  return writer_.append(record);
}
Status BufferedCorpusWriter::appendProgress(const corpus::CorpusProducerProgress& progress) noexcept {
  return enqueue(&progress,sizeof(progress),2);
}
Status BufferedCorpusWriter::appendGap(const corpus::BinaryMarketGap& gap) noexcept {
  return enqueue(&gap,sizeof(gap),3);
}
Status BufferedCorpusWriter::appendReferencePayload(std::span<const std::uint8_t> payload) noexcept {
  if(payload.size()>128u*1024u*1024u || !capacity_ || stop_.load(std::memory_order_acquire))return Status::InvalidArgument;
  try {
    std::lock_guard lock(referenceMutex_);referencePayload_.assign(payload.begin(),payload.end());referencePending_=true;
    wakeCondition_.notify_one();return Status::Ok;
  } catch(...) {return Status::IoError;}
}
void BufferedCorpusWriter::run() noexcept {
  std::uint64_t read=0;
  auto nextCommit=std::chrono::steady_clock::now();
  bool dirty=true;
  for(;;) {
    const auto write=written_.load(std::memory_order_acquire);
    if(read<write) {
      const auto status=consume(read);
      if(!isOk(status)) {status_.store(status,std::memory_order_release);break;}
      read_.store(++read,std::memory_order_release);dirty=true;
    }
    {
      std::lock_guard lock(referenceMutex_);
      if(referencePending_) {
        const auto status=writer_.setReferencePayload(referencePayload_);
        if(!isOk(status)) {status_.store(status,std::memory_order_release);break;}
        referencePending_=false;dirty=true;
      }
    }
    const bool stop=stop_.load(std::memory_order_acquire);
    const auto now=std::chrono::steady_clock::now();
    if(dirty && (now>=nextCommit || (stop && read==written_.load(std::memory_order_acquire)))) {
      const auto status=writer_.publishCut();
      if(!isOk(status)) {status_.store(status,std::memory_order_release);break;}
      nextCommit=now+std::chrono::milliseconds(500);dirty=false;
      std::lock_guard lock(snapshotMutex_);snapshot_=writer_.snapshot();
    }
    if(read==written_.load(std::memory_order_acquire)) {
      if(stop)break;
      std::unique_lock lock(wakeMutex_);
      wakeCondition_.wait_for(lock,std::chrono::milliseconds(100));
    }
  }
  std::lock_guard lock(snapshotMutex_);snapshot_=writer_.snapshot();
}
Status BufferedCorpusWriter::finish() noexcept {
  if(thread_.joinable()) {
    stop_.store(true,std::memory_order_release);
    wakeEpoch_.fetch_add(1,std::memory_order_release);wakeCondition_.notify_one();
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
