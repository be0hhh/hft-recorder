#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "CaptureCoordinator.hpp"
#include "../Common/Status.hpp"

namespace hftrec::capture {

struct VenueMultiplexJob {
    CaptureConfig config{};
    ExternalCaptureChannels channels{};
};

class VenueMultiplexCapture {
  public:
    VenueMultiplexCapture();
    ~VenueMultiplexCapture();
    VenueMultiplexCapture(const VenueMultiplexCapture&) = delete;
    VenueMultiplexCapture& operator=(const VenueMultiplexCapture&) = delete;

    Status start(std::vector<VenueMultiplexJob> jobs) noexcept;
    bool pollOnce() noexcept;
    void requestStop() noexcept;
    Status finalize() noexcept;

    [[nodiscard]] bool running() const noexcept;
    [[nodiscard]] std::uint64_t totalRows() const noexcept;
    [[nodiscard]] std::size_t activeJobs() const noexcept;
    [[nodiscard]] std::size_t skippedJobs() const noexcept;
    [[nodiscard]] std::string lastError() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_{};
};

}  // namespace hftrec::capture
