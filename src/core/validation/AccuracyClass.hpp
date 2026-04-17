#pragma once

#include <string_view>

namespace hftrec::validation {

enum class AccuracyClass {
    LosslessExact = 1,
    NearLossless = 2,
    LossyExperimental = 3,
    Failed = 4,
};

inline constexpr std::string_view accuracyClassToString(AccuracyClass value) noexcept {
    switch (value) {
        case AccuracyClass::LosslessExact: return "lossless_exact";
        case AccuracyClass::NearLossless: return "near_lossless";
        case AccuracyClass::LossyExperimental: return "lossy_experimental";
        case AccuracyClass::Failed: return "failed";
    }
    return "failed";
}

}  // namespace hftrec::validation
