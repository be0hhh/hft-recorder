#include "core/metrics/Metrics.hpp"

// Phase 1 stub: no prometheus-cpp link yet. Functions are no-ops; later phases
// wire up prometheus::Registry + Exposer / Gateway per doc/LOGGING_AND_METRICS.md.

namespace hftrec::metrics {

void init() noexcept {}
void shutdown() noexcept {}

void incEventsCaptured(std::string_view) noexcept {}
void incEventsDropped(std::string_view, std::string_view) noexcept {}
void addBytesWritten(std::string_view, std::uint64_t) noexcept {}
void incBlocksFlushed(std::string_view, std::string_view) noexcept {}

}  // namespace hftrec::metrics
