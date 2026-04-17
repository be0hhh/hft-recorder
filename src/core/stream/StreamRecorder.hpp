#pragma once

#include <atomic>
#include <cstddef>
#include <string_view>

#include "core/common/Status.hpp"
#include "core/dataset/StreamFamily.hpp"

// Producer-side interface for a single stream. Concrete implementations live in
// src/app/ (capture_cli.cpp) or in variants that need a bespoke producer.
// Phase 1: declarations only; bodies land in Phase 2.

namespace hftrec {

class IStreamRecorder {
  public:
    IStreamRecorder() = default;
    IStreamRecorder(const IStreamRecorder&) = delete;
    IStreamRecorder& operator=(const IStreamRecorder&) = delete;
    virtual ~IStreamRecorder() = default;

    // Start the producer. Non-blocking — spawns its own thread (CPU pinned).
    virtual Status start() noexcept = 0;

    // Ask the producer to stop. Blocks until the thread has joined.
    virtual Status stop() noexcept = 0;

    virtual StreamFamily family() const noexcept = 0;
    virtual std::string_view symbol() const noexcept = 0;
};

}  // namespace hftrec
