#pragma once
#include "../Parser/BufferedCorpusWriter.hpp"
#include <atomic>
#include <filesystem>
#include <span>
#include <thread>

namespace hftrec::capture {
// Cold reference owner. Neither consumer control nor reference file acquisition
// runs on the Parser capture drain thread.
class RecorderReferenceCapture final {
 public:
    RecorderReferenceCapture() noexcept=default;
    ~RecorderReferenceCapture() noexcept;
    [[nodiscard]] Status start(const std::filesystem::path& runtime,
        std::span<const corpus::BinaryMarketSource> sources,BufferedCorpusWriter& writer,
        std::uint64_t maximumBytes) noexcept;
    void stop() noexcept;
    [[nodiscard]] Status status() const noexcept {return status_.load(std::memory_order_acquire);}
 private:
    std::atomic<bool> stop_{};
    std::atomic<Status> status_{Status::Ok};
    std::thread worker_{};
};
} // namespace hftrec::capture
