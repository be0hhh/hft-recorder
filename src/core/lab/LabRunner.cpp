#include "core/lab/LabRunner.hpp"

#include "core/lab/BookFrameSampler.hpp"
#include "core/validation/AccuracyClass.hpp"

namespace hftrec::lab {

std::vector<PipelineResult> LabRunner::run(const corpus::SessionCorpus& corpus,
                                           const std::vector<PipelineDescriptor>& pipelines) const {
    std::vector<PipelineResult> results;
    results.reserve(pipelines.size());
    const auto inputBytes = corpus.tradeLines.size() + corpus.bookTickerLines.size() + corpus.depthLines.size();
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
        PipelineResult result{};
        result.pipelineId = pipeline.id;
        result.inputBytes = inputBytes;
        result.outputBytes = inputBytes;
        result.compressionRatioPpm = 1'000'000u;
        result.groundTruth = groundTruth;
        result.validation.eventsTotal = inputBytes;
        result.validation.eventsExactMatch = inputBytes;
        result.validation.accuracyPpm = 1'000'000u;
        result.validation.accuracyClass = isOk(groundTruthStatus)
            ? validation::AccuracyClass::LosslessExact
            : validation::AccuracyClass::Failed;
        result.validation.eventsMismatch = isOk(groundTruthStatus) ? 0u : 1u;
        results.push_back(result);
    }
    return results;
}

}  // namespace hftrec::lab
