#pragma once

#include <cstdint>
#include <string_view>

// Prometheus metric surface — Phase 1 stub.
// Full registration lands once prometheus-cpp is linked (Phase 3+).

namespace hftrec::metrics {

void init() noexcept;
void shutdown() noexcept;

void incEventsCaptured(std::string_view stream) noexcept;
void incEventsDropped(std::string_view stream, std::string_view reason) noexcept;
void addBytesWritten(std::string_view stream, std::uint64_t n) noexcept;
void incBlocksFlushed(std::string_view stream, std::string_view reason) noexcept;

}  // namespace hftrec::metrics
