#pragma once
#include "hftrec/CorpusContract/BinaryMarketCorpusFormat.hpp"
#include "hftrec/CorpusContract/BinaryMarketCorpusWriter.hpp"
#include "hftrec/Status.hpp"
#include <array>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <span>
#include <vector>

namespace hftrec::capture {
struct RecorderCaptureVenue final {
    std::string exchange{};
    std::string market{};
    std::vector<std::string> instruments{};
    bool fullUniverse{false};
};
struct RecorderCaptureSessionConfig final {
    std::filesystem::path workspaceRoot{};
    std::filesystem::path parserTemplate{};
    std::filesystem::path envPath{};
    std::filesystem::path outputRoot{};
    std::vector<RecorderCaptureVenue> venues{};
    std::uint16_t channelMask{0u};
    std::uint64_t durationSec{3600u};
    std::uint64_t maximumBytes{1024u*1024u*1024u};
    std::uint64_t segmentBytes{64u*1024u*1024u};
};
// Pure, secret-free launch preparation. Selected routes come exclusively from
// existing Parser template sections; unsupported selections fail closed.
[[nodiscard]] Status renderRecorderParserConfig(std::string_view parserTemplate,
    const RecorderCaptureSessionConfig& config, std::string& output,
    std::string& error) noexcept;
struct RecorderSubscriptionChange final {
    std::uint32_t sourceId{0u};
    corpus::BinaryMarketChannel channel{corpus::BinaryMarketChannel::BookTicker};
    bool add{false};
};
[[nodiscard]] Status planRecorderSubscriptionChanges(const RecorderCaptureSessionConfig& desired,
    std::span<const corpus::BinaryMarketSource> directory,
    std::vector<RecorderSubscriptionChange>& changes,std::string& error) noexcept;
[[nodiscard]] std::filesystem::path findRecorderWorkspaceRoot() noexcept;
struct RecorderCaptureSessionSnapshot final {
    bool active{false};
    bool connected{false};
    bool complete{false};
    bool selectionPending{false};
    bool producerRetained{false};
    std::int64_t producerPid{0};
    std::uint64_t appliedSelectionRevision{0u};
    std::uint64_t sourceMetadataRevision{0u};
    Status status{Status::Ok};
    std::uint16_t channelMask{0u};
    std::uint32_t sourceCount{0u};
    std::uint64_t producerEpoch{0u};
    std::array<std::uint64_t,corpus::kBinaryMarketChannelCount> channelRecords{};
    corpus::BinaryMarketWriterSnapshot writer{};
    std::filesystem::path sessionPath{};
    std::filesystem::path runtimePath{};
    std::string error{};
};
struct RecorderCaptureSessionState;
// One Recorder-owned Parser process and one corpus for the complete batch.
// Supported selection changes use producer subscription control on the owner thread.
class RecorderCaptureSession final {
 public:
    RecorderCaptureSession() noexcept;
    ~RecorderCaptureSession() noexcept;
    RecorderCaptureSession(const RecorderCaptureSession&)=delete;
    RecorderCaptureSession& operator=(const RecorderCaptureSession&)=delete;
    [[nodiscard]] Status start(const RecorderCaptureSessionConfig& config,
                               std::string& error) noexcept;
    [[nodiscard]] Status updateSelection(const RecorderCaptureSessionConfig& config, std::string& error) noexcept;
    void requestStop() noexcept;
    [[nodiscard]] Status stop() noexcept;
    [[nodiscard]] RecorderCaptureSessionSnapshot snapshot() const noexcept;
    [[nodiscard]] std::vector<corpus::BinaryMarketSource> sourceDirectory() const noexcept;
 private:
    std::unique_ptr<RecorderCaptureSessionState> state_{};
};
} // namespace hftrec::capture
