#include "RecorderReplaySelection.hpp"

#include "../Capture/SessionManifest.hpp"

#include <fstream>
#include <string_view>
#include <system_error>

namespace hftrec::detail {
namespace {

bool fileExists(const std::filesystem::path& path) noexcept {
    std::error_code ec;
    return std::filesystem::is_regular_file(path, ec);
}

bool directoryExists(const std::filesystem::path& path) noexcept {
    std::error_code ec;
    return std::filesystem::is_directory(path, ec);
}

bool readWholeFile(const std::filesystem::path& path, std::string& out) noexcept {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    std::error_code ec;
    const auto size = std::filesystem::file_size(path, ec);
    if (ec) return false;
    out.resize(static_cast<std::size_t>(size));
    if (out.empty()) return true;
    in.read(out.data(), static_cast<std::streamsize>(out.size()));
    return in.good() || in.eof();
}

Status loadManifest(const std::filesystem::path& sessionPath,
                    capture::SessionManifest& manifest,
                    std::string& error) noexcept {
    const auto manifestPath = sessionPath / "manifest.json";
    if (!fileExists(manifestPath)) {
        error = "current captured-arrival manifest.json is required";
        return Status::CorruptData;
    }

    std::string document;
    if (!readWholeFile(manifestPath, document)) {
        error = "failed to read manifest.json";
        return Status::IoError;
    }

    const Status status = capture::parseManifestJson(document, manifest);
    if (!isOk(status)) {
        error = "failed to parse manifest.json";
        return status;
    }
    if (!manifest.structurallyLoadable ||
        manifest.manifestSchemaVersion != capture::kManifestSchemaVersionCurrent ||
        manifest.corpusSchemaVersion != capture::kCorpusSchemaVersionCurrent ||
        manifest.captureContractVersion != capture::kCaptureContractVersionCurrent) {
        error = "manifest.json does not declare the current captured-arrival contract";
        return Status::CorruptData;
    }
    return Status::Ok;
}

std::filesystem::path declaredChannelPath(
    const std::filesystem::path& sessionPath,
    const std::string& manifestPath) {
    const std::filesystem::path relative{manifestPath};
    if (relative.empty() || relative.is_absolute()) return {};
    for (const auto& component : relative) {
        if (component == "..") return {};
    }
    return sessionPath / relative;
}

}  // namespace

Status openSelectedReplay(const std::filesystem::path& sessionPath,
                          RecorderChannelMask channels,
                          replay::SessionReplay& replay,
                          std::string& error) noexcept {
    replay.reset();
    if (sessionPath.empty()) {
        error = "session path is empty";
        return Status::InvalidArgument;
    }
    if (!directoryExists(sessionPath)) {
        error = "session directory does not exist";
        return Status::InvalidArgument;
    }

    capture::SessionManifest manifest{};
    const Status manifestStatus = loadManifest(sessionPath, manifest, error);
    if (!isOk(manifestStatus)) return manifestStatus;

    const auto addChannel = [&](RecorderChannelMask channel,
                                bool enabled,
                                std::string_view label,
                                const std::filesystem::path& path,
                                auto addFile) noexcept -> Status {
        if (!wants(channels, channel) || !enabled) return Status::Ok;
        if (path.empty() || !fileExists(path)) {
            error = "manifest-declared ";
            error += label;
            error += " artifact is missing or unsafe";
            return Status::CorruptData;
        }
        return (replay.*addFile)(path, 0u);
    };

    Status status = addChannel(RecorderChannel_Trades,
                               manifest.tradesEnabled,
                               "trades",
                               declaredChannelPath(sessionPath, manifest.tradesPath),
                               &replay::SessionReplay::addTradesFile);
    if (!isOk(status)) return status;

    status = addChannel(RecorderChannel_Liquidations,
                        manifest.liquidationsEnabled,
                        "liquidations",
                        declaredChannelPath(sessionPath, manifest.liquidationsPath),
                        &replay::SessionReplay::addLiquidationsFile);
    if (!isOk(status)) return status;

    status = addChannel(RecorderChannel_BookTicker,
                        manifest.bookTickerEnabled,
                        "bookticker",
                        declaredChannelPath(sessionPath, manifest.bookTickerPath),
                        &replay::SessionReplay::addBookTickerFile);
    if (!isOk(status)) return status;

    if (wants(channels, RecorderChannel_Candles)) {
        const auto candlesPath = declaredChannelPath(sessionPath, manifest.candlesPath);
        const auto candles2Path = declaredChannelPath(sessionPath, manifest.candles2Path);
        if (manifest.candlesEnabled) {
            if (candlesPath.empty() || !fileExists(candlesPath)) {
                error = "manifest-declared candles artifact is missing or unsafe";
                return Status::CorruptData;
            }
            status = replay.addCandlesFile(candlesPath);
            if (!isOk(status)) return status;
        }
        if (manifest.candles2Enabled) {
            if (candles2Path.empty() || !fileExists(candles2Path)) {
                error = "manifest-declared candles2 artifact is missing or unsafe";
                return Status::CorruptData;
            }
            status = replay.addCandles2File(candles2Path);
            if (!isOk(status)) return status;
        }
    }

    status = addChannel(RecorderChannel_Depth,
                        manifest.orderbookEnabled,
                        "depth",
                        declaredChannelPath(sessionPath, manifest.depthPath),
                        &replay::SessionReplay::addDepthFile);
    if (!isOk(status)) return status;

    const auto countMatches = [&](RecorderChannelMask channel,
                                  bool enabled,
                                  std::uint64_t declared,
                                  std::size_t actual,
                                  std::string_view label) noexcept {
        if (!wants(channels, channel) || !enabled ||
            declared == static_cast<std::uint64_t>(actual)) return true;
        error = "manifest declared_event_count mismatch for ";
        error += label;
        return false;
    };
    if (!countMatches(RecorderChannel_Trades, manifest.tradesEnabled,
                      manifest.tradesCount, replay.trades().size(), "trades") ||
        !countMatches(RecorderChannel_Liquidations, manifest.liquidationsEnabled,
                      manifest.liquidationsCount, replay.liquidations().size(), "liquidations") ||
        !countMatches(RecorderChannel_BookTicker, manifest.bookTickerEnabled,
                      manifest.bookTickerCount, replay.bookTickers().size(), "bookticker") ||
        !countMatches(RecorderChannel_Depth, manifest.orderbookEnabled,
                      manifest.depthCount, replay.depths().size(), "depth")) {
        return Status::CorruptData;
    }
    if (wants(channels, RecorderChannel_Candles) &&
        ((manifest.candlesEnabled &&
          manifest.candlesCount != static_cast<std::uint64_t>(replay.candles().size())) ||
         (manifest.candles2Enabled &&
          manifest.candles2Count != static_cast<std::uint64_t>(replay.candles2().size())))) {
        error = "manifest declared_event_count mismatch for candles";
        return Status::CorruptData;
    }

    replay.finalize();
    return replay.status();
}

}  // namespace hftrec::detail
