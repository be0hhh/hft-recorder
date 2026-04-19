#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/validation/ValidationRunner.hpp"

namespace {

using hftrec::validation::ValidationRunner;
using hftrec::validation::AccuracyClass;

TEST(ValidationRunner, ExactCorpusKeepsLosslessStatus) {
    ValidationRunner runner{};
    const std::vector<std::string> original = {"a", "b"};
    const std::vector<std::string> decoded = {"a", "b"};

    const auto result = runner.compare(original, decoded);
    EXPECT_EQ(result.eventsTotal, 2u);
    EXPECT_EQ(result.eventsExactMatch, 2u);
    EXPECT_EQ(result.eventsMismatch, 0u);
    EXPECT_EQ(result.accuracyClass, AccuracyClass::LosslessExact);
    EXPECT_FALSE(result.hasFirstMismatch);
    EXPECT_TRUE(result.failureReason.empty());
}

TEST(ValidationRunner, FirstMismatchIsRecordedDeterministically) {
    ValidationRunner runner{};
    const std::vector<std::string> original = {"a", "b", "c"};
    const std::vector<std::string> decoded = {"a", "x", "c"};

    const auto result = runner.compare(original, decoded);
    EXPECT_EQ(result.eventsMismatch, 1u);
    EXPECT_TRUE(result.hasFirstMismatch);
    EXPECT_EQ(result.firstMismatchEventIndex, 1u);
    EXPECT_EQ(result.firstMismatchChannel, "unknown");
    EXPECT_EQ(result.accuracyClass, AccuracyClass::Failed);
    EXPECT_FALSE(result.failureReason.empty());
}

}  // namespace
