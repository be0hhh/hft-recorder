#include <cstdio>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "hft_compressor/compressor.hpp"
#include "hftrec/version.hpp"

namespace fs = std::filesystem;

namespace {

void printHelp() {
    std::puts("hft-recorder-bench - support CLI for corpus-based compression experiments");
    std::puts("");
    std::puts("Usage:");
    std::puts("  hft-recorder-bench <session_dir> [--pipelines all|id1,id2,...]");
    std::puts("");
    std::puts("Runs hft-compressor C++ pipelines against jsonl/trades.jsonl,");
    std::puts("jsonl/bookticker.jsonl, and jsonl/depth.jsonl and writes artifacts under");
    std::puts("<session_dir>/compressed/<pipeline-slug>/sessions/<session>/<channel>.<ext>.");
}

std::vector<std::string> splitPipelines(std::string_view raw) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= raw.size()) {
        const std::size_t comma = raw.find(',', start);
        const std::size_t end = comma == std::string_view::npos ? raw.size() : comma;
        if (end > start) out.emplace_back(raw.substr(start, end - start));
        if (comma == std::string_view::npos) break;
        start = comma + 1u;
    }
    return out;
}

bool pipelineMatchesStream(const hft_compressor::PipelineDescriptor& pipeline,
                           hft_compressor::StreamType stream) noexcept {
    const std::string_view scope = pipeline.streamScope;
    if (scope == "all") return true;
    return (stream == hft_compressor::StreamType::Trades && scope == "trades")
        || (stream == hft_compressor::StreamType::BookTicker && scope == "bookticker")
        || (stream == hft_compressor::StreamType::Depth && scope == "depth");
}

std::vector<std::string> selectedPipelines(std::string_view raw,
                                           hft_compressor::StreamType stream) {
    if (!raw.empty() && raw != "all") {
        std::vector<std::string> out;
        for (const auto& id : splitPipelines(raw)) {
            const auto* pipeline = hft_compressor::findPipeline(id);
            if (pipeline != nullptr && pipelineMatchesStream(*pipeline, stream)) out.push_back(id);
        }
        return out;
    }
    std::vector<std::string> out;
    for (const auto& pipeline : hft_compressor::listPipelines()) {
        if (pipeline.implementationKind != std::string_view{"c++"}) continue;
        if (pipeline.availability != hft_compressor::PipelineAvailability::Available) continue;
        if (!pipelineMatchesStream(pipeline, stream)) continue;
        out.emplace_back(pipeline.id);
    }
    return out;
}

int runOne(const fs::path& sessionDir,
           const fs::path& input,
           hft_compressor::StreamType stream,
           std::string_view pipelineId) {
    hft_compressor::CompressionRequest request{};
    request.inputPath = input;
    request.outputRoot = sessionDir / "compressed";
    request.pipelineId = std::string{pipelineId};
    const auto result = hft_compressor::compress(request);
    std::printf("%s %s status=%s ratio=%.4f encode=%.2fMB/s decode=%.2fMB/s output=%s\n",
                hft_compressor::streamTypeToString(stream).data(),
                std::string{pipelineId}.c_str(),
                hft_compressor::statusToString(result.status).data(),
                hft_compressor::ratio(result),
                hft_compressor::encodeMbPerSec(result),
                hft_compressor::decodeMbPerSec(result),
                result.outputPath.string().c_str());
    if (!hft_compressor::isOk(result.status)) return 1;

    hft_compressor::DecodeVerifyRequest verify{};
    verify.compressedPath = result.outputPath;
    verify.canonicalPath = input;
    verify.pipelineId = std::string{pipelineId};
    verify.verifyMode = hft_compressor::VerifyMode::Both;
    const auto verified = hft_compressor::decodeAndVerify(verify);
    std::printf("verify %s %s status=%s byte=%s record=%s decode=%.2fMB/s\n",
                hft_compressor::streamTypeToString(stream).data(),
                std::string{pipelineId}.c_str(),
                hft_compressor::statusToString(verified.status).data(),
                verified.byteExact ? "true" : "false",
                verified.recordExact ? "true" : "false",
                hft_compressor::decodeMbPerSec(verified));
    return hft_compressor::isOk(verified.status) ? 0 : 1;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) { printHelp(); return 0; }
    const std::string_view a1{argv[1]};
    if (a1 == "--help" || a1 == "-h") { printHelp(); return 0; }
    if (a1 == "--version" || a1 == "-v") {
        std::printf("hft-recorder-bench %s\n", hftrec::kHftRecorderVersion);
        return 0;
    }

    std::string_view pipelineArg{"all"};
    for (int i = 2; i + 1 < argc; i += 2) {
        if (std::string_view{argv[i]} == "--pipelines") pipelineArg = argv[i + 1];
    }

    const fs::path sessionDir{argv[1]};
    const struct Channel { const char* file; hft_compressor::StreamType stream; } channels[] = {
        {"trades.jsonl", hft_compressor::StreamType::Trades},
        {"bookticker.jsonl", hft_compressor::StreamType::BookTicker},
        {"depth.jsonl", hft_compressor::StreamType::Depth},
    };

    int failures = 0;
    for (const auto& channel : channels) {
        const fs::path input = sessionDir / "jsonl" / channel.file;
        if (!fs::is_regular_file(input)) continue;
        for (const auto& pipeline : selectedPipelines(pipelineArg, channel.stream)) {
            failures += runOne(sessionDir, input, channel.stream, pipeline);
        }
    }
    return failures == 0 ? 0 : 1;
}

