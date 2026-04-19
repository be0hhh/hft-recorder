#pragma once

#include <cstdint>
#include <cstddef>
#include <string>

#include "core/validation/AccuracyClass.hpp"

namespace hftrec::validation {

struct ValidationResult {
    std::size_t eventsTotal{0};
    std::size_t eventsExactMatch{0};
    std::size_t eventsMismatch{0};
    std::uint64_t accuracyPpm{0};
    AccuracyClass accuracyClass{AccuracyClass::Failed};
    bool hasFirstMismatch{false};
    std::size_t firstMismatchEventIndex{0};
    std::string firstMismatchChannel{};
    std::string failureReason{};
};

}  // namespace hftrec::validation
