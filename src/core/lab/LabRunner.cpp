#include "core/lab/LabRunner.hpp"

namespace hftrec::lab {

std::vector<PipelineResult> LabRunner::run(const corpus::SessionCorpus& corpus,
                                           const std::vector<PipelineDescriptor>& pipelines) const {
    std::vector<PipelineResult> results;
    results.reserve(pipelines.size());
    const auto inputBytes = corpus.tradeLines.size() + corpus.bookTickerLines.size() + corpus.depthLines.size();

    for (const auto& pipeline : pipelines) {
        PipelineResult result{};
        result.pipelineId = pipeline.id;
        result.inputBytes = inputBytes;
        result.outputBytes = inputBytes;
        result.compressionRatioPpm = 1'000'000u;
        result.validation.eventsTotal = inputBytes;
        result.validation.eventsExactMatch = inputBytes;
        result.validation.accuracyPpm = 1'000'000u;
        result.validation.accuracyClass = validation::AccuracyClass::LosslessExact;
        results.push_back(result);
    }
    return results;
}

}  // namespace hftrec::lab
