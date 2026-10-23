#pragma once
#include "hftrec/CorpusContract/CorpusCut.hpp"
#include <atomic>
#include <algorithm>
#include <deque>
#include <mutex>

namespace hftrec::corpus {
inline constexpr std::size_t kCorpusMaximumReferenceBytes=128u*1024u*1024u;
class CorpusPublication final {
 public:
    explicit CorpusPublication(std::uint64_t capacity):capacity_(capacity) {}
    std::atomic<std::shared_ptr<const CorpusCut>> latest{};
    void retain(const CorpusBlockRange& range,std::span<const std::uint8_t> bytes) {
        if(bytes.size()>capacity_) return;
        std::lock_guard lock(mutex_);
        while(!blocks_.empty() && retained_>capacity_-bytes.size()) {
            retained_-=blocks_.front().bytes.size();blocks_.pop_front();
        }
        blocks_.push_back({range.segmentNumber,range.offset,{bytes.begin(),bytes.end()}});
        retained_+=bytes.size();
    }
    bool load(const CorpusBlockRange& range,std::span<std::uint8_t> output) {
        std::lock_guard lock(mutex_);
        for(const auto& block:blocks_) if(block.segment==range.segmentNumber && block.offset==range.offset) {
            if(output.size()!=block.bytes.size()) return false;
            std::copy(block.bytes.begin(),block.bytes.end(),output.begin());return true;
        }
        return false;
    }
 private:
    struct Block {std::uint32_t segment;std::uint64_t offset;std::vector<std::uint8_t> bytes;};
    std::mutex mutex_;
    std::deque<Block> blocks_;
    std::uint64_t capacity_{},retained_{};
};
[[nodiscard]] bool syncCorpusFile(const std::filesystem::path& path,bool directory=false) noexcept;
[[nodiscard]] Status writeCorpusCut(const std::filesystem::path& root,const CorpusCut& cut) noexcept;
} // namespace hftrec::corpus
