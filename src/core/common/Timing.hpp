#pragma once

#include <chrono>
#include <cstdint>

#if HFTREC_WITH_CXET
#include "probes/TimeDelta.hpp"
#endif

namespace hftrec::timing {

#if HFTREC_WITH_CXET
using Tick = ::TscTick;
using Duration = ::DurationNs;

inline Tick captureTick() noexcept {
    return cxet::probes::captureTsc();
}

inline Duration deltaNs(Tick start, Tick end) noexcept {
    return cxet::probes::deltaNs(start, end);
}

inline void ensureCalibrated() noexcept {
    (void)cxet::probes::ensureCalibrated();
}
#else
struct Tick {
    std::uint64_t raw{0};
};

struct Duration {
    std::uint64_t raw{0};
};

inline Tick captureTick() noexcept {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return Tick{static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(now).count())};
}

inline Duration deltaNs(Tick start, Tick end) noexcept {
    return Duration{end.raw > start.raw ? end.raw - start.raw : 0u};
}

inline void ensureCalibrated() noexcept {}
#endif

}  // namespace hftrec::timing