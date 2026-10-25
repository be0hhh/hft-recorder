#pragma once
#include "hftrec/CorpusContract/CorpusCut.hpp"
#include "hftrec/Status.hpp"
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace hftrec::service {
inline constexpr std::uint32_t kRecorderServiceMaximumMessage = 256u * 1024u;
inline constexpr std::uint32_t kRecorderServiceMaximumBlockChunk = 64u * 1024u;
inline constexpr char kRecorderServiceSocket[] = "recorder-control.sock";
struct RecorderVenue final {
    std::string exchange{}, market{};
    std::vector<std::string> instruments{};
    bool fullUniverse{false};
};
// Cold configuration. Parser remains the sole native route/credential owner.
// Paths select existing Parser profiles; no credential material crosses IPC.
struct RecorderServiceConfig final {
    std::filesystem::path workspaceRoot{}, parserTemplate{}, envPath{}, outputRoot{};
    std::vector<RecorderVenue> venues{};
    std::uint16_t channelMask{0x07u};
    std::uint64_t durationSec{3600u};
    std::uint64_t maximumBytes{1024u * 1024u * 1024u};
    std::uint64_t segmentBytes{64u * 1024u * 1024u};
    std::uint64_t retainedCompressedBytes{16u * 1024u * 1024u};
    // Serialized reference history ceiling; cold acquisition/commit has bounded
    // temporary copies. Exhaustion appends missing-capacity fences, never evicts
    // earlier observations or makes old metadata visible at an earlier time.
    std::uint64_t maximumReferenceBytes{64u * 1024u * 1024u};
    std::uint32_t writerQueueRecords{8192u};
};
// Bounded line-based profile: paths and integer fields use their member names;
// repeated venue=exchange|market|* (universe) or comma-separated canonical symbols.
[[nodiscard]] Status loadRecorderServiceConfig(const std::filesystem::path&,
    RecorderServiceConfig&, std::string& error) noexcept;
struct RecorderServiceStatus final {
    bool configured{}, active{}, connected{}, complete{}, stopping{}, selectionPending{};
    Status captureStatus{Status::Ok};
    std::uint64_t producerEpoch{}, lossEpoch{}, cutGeneration{}, safeMonotonicNs{};
    std::uint64_t records{}, gaps{}, projectedBytes{};
    std::uint32_t sourceCount{};
    std::int64_t startedReceiveNs{};
    std::uint64_t startedMonotonicNs{};
    std::filesystem::path sessionPath{};
    std::string sourceIdentity{};
    std::string error{};
};
struct RecorderCatalog final {
    std::uint64_t cutGeneration{};
    std::uint32_t totalSources{}, nextOffset{};
    std::vector<corpus::BinaryMarketSource> sources{};
};
struct RecorderSessionEntry final {
    std::filesystem::path sourcePath{};
    std::string sourceIdentity{};
    std::uint64_t producerEpoch{}, cutGeneration{};
    std::int64_t startedReceiveNs{}, endedReceiveNs{};
    std::uint32_t sourceCount{};
    bool active{}, complete{};
};
struct RecorderSessionCatalog final {
    std::uint32_t totalSessions{},nextOffset{};
    std::vector<RecorderSessionEntry> sessions{};
};
struct RecorderBlockChunk final {
    std::uint64_t cutGeneration{}, totalBytes{};
    std::uint32_t offset{};
    // Subscriber lag is a cursor condition, never Parser capture loss.
    bool archiveFallback{};
    bool available{}, subscriberLag{};
    corpus::CorpusBlockRange range{};
    std::vector<std::uint8_t> bytes{};
};
struct RecorderServiceClientState;
class RecorderServiceClient final {
 public:
    RecorderServiceClient() noexcept;
    ~RecorderServiceClient() noexcept;
    RecorderServiceClient(const RecorderServiceClient&) = delete;
    RecorderServiceClient& operator=(const RecorderServiceClient&) = delete;
    [[nodiscard]] Status connect(const std::filesystem::path& runtimeDirectory, std::string& error) noexcept;
    void disconnect() noexcept;
    [[nodiscard]] Status configure(const RecorderServiceConfig&, std::string& error) noexcept;
    [[nodiscard]] Status start(std::string& error) noexcept;
    // Acknowledges the stop request. Poll status until active becomes false.
    [[nodiscard]] Status requestStop(std::string& error) noexcept;
    [[nodiscard]] Status status(RecorderServiceStatus&, std::string& error) noexcept;
    [[nodiscard]] Status catalog(std::uint32_t offset, std::uint32_t limit,
        RecorderCatalog&, std::string& error) noexcept;
    [[nodiscard]] Status sessions(std::uint32_t offset, std::uint32_t limit,
        RecorderSessionCatalog&, std::string& error) noexcept;
    [[nodiscard]] Status sessionSources(const std::filesystem::path& sourcePath,
        std::uint32_t offset, std::uint32_t limit, RecorderCatalog&, std::string& error) noexcept;
    // generation=0 selects the current cut. An old unavailable generation fails
    // closed; callers refresh their cut before resuming at a durable block.
    [[nodiscard]] Status readBlock(std::uint64_t generation, std::uint32_t blockIndex,
        std::uint32_t offset, std::uint32_t maximumBytes, RecorderBlockChunk&,
        std::string& error) noexcept;
 private:
    std::unique_ptr<RecorderServiceClientState> state_{};
};
struct RecorderServiceServerState;
class RecorderServiceServer final {
 public:
    RecorderServiceServer() noexcept;
    ~RecorderServiceServer() noexcept;
    RecorderServiceServer(const RecorderServiceServer&) = delete;
    RecorderServiceServer& operator=(const RecorderServiceServer&) = delete;
    [[nodiscard]] Status open(const std::filesystem::path& runtimeDirectory, std::string& error) noexcept;
    [[nodiscard]] Status configure(const RecorderServiceConfig&, std::string& error) noexcept;
    [[nodiscard]] Status start(std::string& error) noexcept;
    // Bounded control work only; capture and compression have independent owners.
    [[nodiscard]] Status poll(std::uint32_t timeoutMs, std::string& error) noexcept;
    void requestStop() noexcept;
    void close() noexcept;
 private:
    std::unique_ptr<RecorderServiceServerState> state_{};
};
} // namespace hftrec::service
