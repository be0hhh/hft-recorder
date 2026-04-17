#pragma once

#include <cstdint>

// Primitive aliases re-exported from CXETCPP where possible, or defined here
// as int64 scaled 1e8 per the project rule (no float/double in hot path).
// When CXETCPP public headers expose Price/Amount/TimeNs, prefer those — do
// not redefine. This header is the single inclusion point used by hftrec core.

#if __has_include("cxet.hpp")
    #include "cxet.hpp"
#endif

namespace hftrec {

// Fallback aliases if cxet.hpp did not define them in the expected namespace.
// Consumers must never mix these with raw int64_t in signatures.
using Price  = std::int64_t;  // scaled by 1e8
using Amount = std::int64_t;  // scaled by 1e8
using TimeNs = std::int64_t;  // nanoseconds since epoch
using EventId = std::int64_t;

}  // namespace hftrec
