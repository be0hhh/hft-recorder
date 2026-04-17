#include "core/corpus/CorpusLoader.hpp"

#include <fstream>
#include <string>

namespace hftrec::corpus {

namespace {

Status loadLinesOptional(const std::filesystem::path& path, std::vector<std::string>& out) noexcept {
    if (!std::filesystem::exists(path)) return Status::Ok;
    std::ifstream stream(path);
    if (!stream.is_open()) return Status::IoError;
    std::string line;
    while (std::getline(stream, line)) {
        out.push_back(line);
    }
    return Status::Ok;
}

}  // namespace

Status CorpusLoader::load(const std::filesystem::path& sessionDir, SessionCorpus& out) noexcept {
    if (!std::filesystem::exists(sessionDir)) return Status::InvalidArgument;
    if (!isOk(loadLinesOptional(sessionDir / "trades.jsonl", out.tradeLines))) return Status::IoError;
    if (!isOk(loadLinesOptional(sessionDir / "bookticker.jsonl", out.bookTickerLines))) return Status::IoError;
    if (!isOk(loadLinesOptional(sessionDir / "depth.jsonl", out.depthLines))) return Status::IoError;

    for (const auto& entry : std::filesystem::directory_iterator(sessionDir)) {
        if (!entry.is_regular_file()) continue;
        const auto name = entry.path().filename().string();
        if (name.rfind("snapshot_", 0) != 0 || entry.path().extension() != ".json") continue;

        std::ifstream snapshotStream(entry.path());
        if (!snapshotStream.is_open()) return Status::IoError;
        std::string document((std::istreambuf_iterator<char>(snapshotStream)), std::istreambuf_iterator<char>());
        out.snapshotDocuments.push_back(document);
    }

    return Status::Ok;
}

}  // namespace hftrec::corpus
