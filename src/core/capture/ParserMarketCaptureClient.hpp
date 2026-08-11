#pragma once

#include "core/common/Status.hpp"
#include "core/corpus/BinaryMarketCorpusFormat.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace hftrec::corpus {
class BinaryMarketCorpusWriter;
}

namespace hftrec::capture {

struct ParserMarketCaptureClientState;

struct ParserMarketCaptureSnapshot final {
    bool connected{false};
    bool stopped{false};
    bool producerDisconnected{false};
    std::uint64_t producerEpoch{0u};
    std::uint64_t directoryGeneration{0u};
    std::uint64_t lossEpoch{0u};
    std::int64_t captureStartedReceiveNs{0};
    std::uint64_t captureStartedMonotonicNs{0u};
    std::uint64_t recordsDrained{0u};
    std::uint64_t gapsWritten{0u};
    std::uint64_t pendingRecords{0u};
    std::uint32_t sourceCount{0u};
    std::uint32_t ringCapacity{0u};
    std::uint16_t shardCount{0u};
};

// Recorder-owned consumer for parserd's dedicated, same-UID capture plane.
// The mapped arena is an app-to-app protocol boundary: this client never
// reaches into parserd runtime objects and never launches parserd itself.
class ParserMarketCaptureClient final {
  public:
    ParserMarketCaptureClient() noexcept;
    ~ParserMarketCaptureClient() noexcept;
    ParserMarketCaptureClient(const ParserMarketCaptureClient&) = delete;
    ParserMarketCaptureClient& operator=(
        const ParserMarketCaptureClient&) = delete;

    [[nodiscard]] Status connect(
        const std::filesystem::path& runtimeDirectory,
        std::string& error) noexcept;
    [[nodiscard]] Status loadSourceDirectory(
        std::vector<corpus::BinaryMarketSource>& sources,
        std::string& error) noexcept;
    [[nodiscard]] Status drain(
        corpus::BinaryMarketCorpusWriter& writer,
        std::uint64_t maximumRecords,
        std::uint64_t& drained,
        std::string& error) noexcept;
    [[nodiscard]] Status stop(std::string& error) noexcept;
    [[nodiscard]] Status freezeDisconnected(std::string& error) noexcept;
    [[nodiscard]] Status appendFrozenLosses(
        corpus::BinaryMarketCorpusWriter& writer,
        std::string& error) noexcept;

    [[nodiscard]] bool producerDisconnected() const noexcept;
    [[nodiscard]] ParserMarketCaptureSnapshot snapshot() const noexcept;
    void disconnect() noexcept;

  private:
    std::unique_ptr<ParserMarketCaptureClientState> state_{};
};

}  // namespace hftrec::capture
