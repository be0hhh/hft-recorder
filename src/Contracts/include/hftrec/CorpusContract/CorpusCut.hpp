#pragma once
#include "hftrec/CorpusContract/BinaryMarketCorpusFormat.hpp"
#include "hftrec/Status.hpp"
#include <array>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace hftrec::corpus {
// Parser-owned committed prefix. Zero safe time means no authoritative prefix.
struct CorpusProducerProgress final {
    std::uint64_t producerEpoch{};
    std::uint64_t safeMonotonicNs{};
    std::uint64_t lossEpoch{};
    std::uint64_t directoryRevision{};
    std::array<std::uint64_t,kBinaryMarketMaximumShards> shardSequences{};
    std::uint16_t shardCount{};
};
struct CorpusBlockRange final {
    std::uint64_t offset{};
    std::uint64_t firstMonotonicNs{},lastMonotonicNs{};
    std::uint32_t segmentNumber{};
    std::uint16_t shardIndex{};
    std::uint16_t reserved{};
    BinaryMarketBlockHeader header{};
};
// One durable Recorder commit, immutable after publication. Blocks may extend
// beyond safeMonotonicNs; consumers must obey the authoritative global prefix.
struct CorpusCut final {
    std::uint64_t generation{};
    std::uint64_t committedMonotonicNs{};
    CorpusProducerProgress progress{};
    BinaryMarketManifest manifest{};
    std::vector<BinaryMarketSource> sources{};
    std::vector<BinaryMarketGap> gaps{};
    std::vector<CorpusBlockRange> blocks{};
    // Opaque cold reference snapshot; visibility begins at this Recorder commit.
    std::vector<std::uint8_t> referencePayload{};
    bool finalized{};
};
struct CorpusSummary final {
    BinaryMarketManifest manifest{};
    std::uint64_t cutGeneration{};
    std::uint64_t cutBytes{};
    bool finalized{};
};
// Stable provenance namespace: cxm:<producerEpoch decimal>.
[[nodiscard]] std::string corpusIdentity(const BinaryMarketManifest& manifest);
// Fixed header only; never decodes rows or scans reference payloads.
[[nodiscard]] Status readCorpusSummary(const std::filesystem::path& root,
    CorpusSummary& summary,std::string& error) noexcept;
// Reads bounded source metadata only, including an active durable cut.
[[nodiscard]] Status readCorpusSourceCatalog(const std::filesystem::path& root,
    std::vector<BinaryMarketSource>& sources,std::string& error) noexcept;
class CorpusPublication;
[[nodiscard]] Status readCorpusCut(const std::filesystem::path& root,
    std::shared_ptr<const CorpusCut>& cut,std::string& error,
    std::uint64_t maximumMetadataBytes=128u*1024u*1024u) noexcept;
[[nodiscard]] Status readCorpusCompressedBlock(const std::filesystem::path& root,
    const CorpusCut& cut,std::size_t blockIndex,std::vector<std::uint8_t>& bytes,
    std::string& error,std::shared_ptr<CorpusPublication> publication={}) noexcept;
[[nodiscard]] Status readCorpusRetainedCompressedBlock(std::shared_ptr<CorpusPublication> publication,
    const CorpusCut& cut,std::size_t blockIndex,std::vector<std::uint8_t>& bytes,
    bool& available,std::string& error) noexcept;
enum class CorpusCursorResult : std::uint8_t { Record,WouldBlock,End };
struct CorpusCompressedBlockSource final {
    void* context{};
    // Recorder acknowledgement of its latest successfully durable publication.
    // Cross-process active feeds must supply this gate before reading cut.bin.
    Status (*approvedGeneration)(void*,std::uint64_t&,std::string&) noexcept{};
    // Ok + available=false explicitly requests archive catch-up (subscriber lag).
    // Other failures propagate; corrupt transport data never falls back silently.
    Status (*fetch)(void*,std::uint64_t,std::size_t,std::vector<std::uint8_t>&,
        bool& available,std::string&) noexcept{};
};
struct CorpusActiveCursorState;
// Bounded compressed cache is optional. A lagging cursor falls back to archive
// ranges; it never reserves cache entries or holds capture progress back.
class CorpusActiveCursor final {
 public:
    CorpusActiveCursor() noexcept;
    ~CorpusActiveCursor() noexcept;
    CorpusActiveCursor(CorpusActiveCursor&&) noexcept;
    CorpusActiveCursor& operator=(CorpusActiveCursor&&) noexcept;
    [[nodiscard]] Status open(const std::filesystem::path& root,
        std::uint64_t beginMonotonicNs,std::string& error,
        std::shared_ptr<CorpusPublication> publication={},
        CorpusCompressedBlockSource source={},
        std::uint64_t maximumResidentBytes=256u*1024u*1024u) noexcept;
    [[nodiscard]] Status refresh(std::string& error) noexcept;
    [[nodiscard]] Status next(BinaryMarketRecord& record,CorpusCursorResult& result,
        std::string& error) noexcept;
    [[nodiscard]] std::shared_ptr<const CorpusCut> cut() const noexcept;
    [[nodiscard]] bool usingArchive() const noexcept;
 private:
    std::unique_ptr<CorpusActiveCursorState> state_{};
};
} // namespace hftrec::corpus
