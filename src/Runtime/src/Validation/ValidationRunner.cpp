#include "ValidationRunner.hpp"

#include <chrono>


namespace hftrec::validation {

ValidationResult ValidationRunner::compare(const std::vector<std::string>& original,
                                           const std::vector<std::string>& decoded) const {
    ValidationResult result{};
    result.eventsTotal = original.size();
    const auto common = original.size() < decoded.size() ? original.size() : decoded.size();
    for (std::size_t i = 0; i < common; ++i) {
        if (original[i] == decoded[i]) {
            ++result.eventsExactMatch;
        } else {
            ++result.eventsMismatch;
            if (!result.hasFirstMismatch) {
                result.hasFirstMismatch = true;
                result.firstMismatchEventIndex = i;
                result.firstMismatchChannel = "unknown";
                result.failureReason = "decoded event differs from canonical corpus";
            }
        }
    }
    const auto sizeMismatch = (original.size() > decoded.size()) ? (original.size() - decoded.size()) : (decoded.size() - original.size());
    result.eventsMismatch += sizeMismatch;
    if (sizeMismatch != 0u && !result.hasFirstMismatch) {
        result.hasFirstMismatch = true;
        result.firstMismatchEventIndex = common;
        result.firstMismatchChannel = "unknown";
        result.failureReason = "decoded corpus length differs from canonical corpus";
    }
    result.accuracyPpm = result.eventsTotal == 0
        ? 1'000'000u
        : static_cast<std::uint64_t>((result.eventsExactMatch * 1'000'000ull) / result.eventsTotal);
    result.accuracyClass = result.eventsMismatch == 0 ? AccuracyClass::LosslessExact : AccuracyClass::Failed;
    return result;
}

}  // namespace hftrec::validation
