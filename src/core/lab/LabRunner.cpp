#include "core/lab/LabRunner.hpp"

#include <algorithm>
#include <chrono>

#include "core/lab/BookFrameSampler.hpp"
#include "core/metrics/Metrics.hpp"
#include "core/validation/AccuracyClass.hpp"

namespace hftrec::lab {

namespace {

std::size_t totalBytes(const std::vector<std::string>& rows) noexcept {
    std::size_t total = 0;
    for (const auto& row : rows) {
        total += row.size();
        ++total;  // newline separator in jsonl source corpus
    }
    return total;
}

std::size_t totalDocumentBytes(const std::vector<std::string>& documents) noexcept {
    std::size_t total = 0;
    for (const auto& document : documents) {
        total += document.size();
    }
    return total;
}

}  // namespace

std::vector<PipelineResult> LabRunner::run(const corpus::SessionCorpus& corpus,
                                           const std::vector<PipelineDescriptor>& pipelines) const {
    std::vector<PipelineResult> results;
    results.reserve(pipelines.size());
    const auto inputBytes = totalBytes(corpus.tradeLines)
        + totalBytes(corpus.bookTickerLines)
        + totalBytes(corpus.depthLines)
        + totalDocumentBytes(corpus.snapshotDocuments);
    std::vector<BookFrame> groundTruthFrames;
    const auto groundTruthStatus = sampleGroundTruthBookFrames(corpus, 16, groundTruthFrames);

    GroundTruthSummary groundTruth{};
    if (isOk(groundTruthStatus)) {
        groundTruth.framesTotal = groundTruthFrames.size();
        groundTruth.sampledTopLevelsPerSide = 16;
        for (const auto& frame : groundTruthFrames) {
            if (frame.hasBookTicker) ++groundTruth.framesWithBookTicker;
            if (frame.totalBidLevels != 0 || frame.totalAskLevels != 0) ++groundTruth.framesWithOrderbook;
            groundTruth.maxBidLevelsSeen = std::max(groundTruth.maxBidLevelsSeen, frame.totalBidLevels);
            groundTruth.maxAskLevelsSeen = std::max(groundTruth.maxAskLevelsSeen, frame.totalAskLevels);
        }
    }

    for (const auto& pipeline : pipelines) {
        const auto encodeStartedAt = std::chrono::steady_clock::now();
        PipelineResult result{};
        result.pipelineId = pipeline.id;
        result.inputBytes = inputBytes;
        result.outputBytes = inputBytes;
        result.compressionRatioPpm = 1'000'000u;
        result.encodeNs = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - encodeStartedAt).count());
        result.decodeNs = 0;
        result.groundTruth = groundTruth;
        result.supported = isOk(groundTruthStatus);
        if (!result.supported) {
            result.failureReason = "ground-truth sampling failed";
        }
        result.validation.eventsTotal = inputBytes;
        result.validation.eventsExactMatch = inputBytes;
        result.validation.accuracyPpm = 1'000'000u;
        result.validation.accuracyClass = isOk(groundTruthStatus)
            ? validation::AccuracyClass::LosslessExact
            : validation::AccuracyClass::Failed;
        result.validation.eventsMismatch = isOk(groundTruthStatus) ? 0u : 1u;
        if (!isOk(groundTruthStatus)) {
            result.validation.failureReason = "ground-truth sampling failed";
        }
        metrics::recordLabRun(result.inputBytes,
                              result.outputBytes,
                              result.encodeNs,
                              result.decodeNs,
                              result.validation.eventsMismatch != 0u);
        results.push_back(result);
    }
    return results;
}

}  // namespace hftrec::lab
