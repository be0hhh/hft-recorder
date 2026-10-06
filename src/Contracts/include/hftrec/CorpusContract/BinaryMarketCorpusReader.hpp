#pragma once


#include "hftrec/CorpusContract/BinaryMarketCorpusFormat.hpp"
#include "hftrec/Status.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace hftrec::corpus {

struct BinaryMarketSelectionRequest final {
    std::filesystem::path root{};
    std::string exchange{};
    std::string market{};
    std::string canonicalSymbol{};
    std::int64_t beginReceiveNs{0};
    std::int64_t endReceiveNs{0};
    std::uint16_t channelMask{0u};
    std::uint16_t requiredChannelMask{0u};
    bool requireTraderReplayCompatibility{true};
    // Explicit multi-source cursor; identity strings must be empty.
    bool allSources{false};
    // Optional explicit source subset for allSources; empty selects the catalog.
    std::vector<std::uint32_t> sourceIds{};
};

struct BinaryMarketSelection final {
    BinaryMarketManifest manifest{};
    BinaryMarketSource source{};
    std::vector<BinaryMarketRecord> records{};
    std::vector<BinaryMarketGap> gaps{};
    std::uint16_t presentChannelMask{0u};
    std::uint64_t sourceGeneration{0u};
    // Exact replay-compatible healthy rows in the selected interval, indexed sourceId-1.
    std::vector<std::uint16_t> sourcePresentChannelMasks{};
    // Actual compatible market or typed native health rows in the interval.
    // Presence alone does not establish healthy usable market coverage.
    std::vector<std::uint16_t> sourceObservedChannelMasks{};
    // Effective producer state strictly before beginReceiveNs; the source
    // generation field is current here, unlike the immutable initial catalog.
    std::vector<BinaryMarketSource> sourcesAtBegin{};
    std::vector<std::uint64_t> sourceLifecycleRevisionsAtBegin{};
    // Captured native health fences strictly before beginReceiveNs, sourceId-1.
    std::vector<std::uint16_t> sourceUnhealthyChannelMasksAtBegin{};
};

struct BinaryMarketCorpusCursorState;

// A cursor owns bounded decode buffers and cold source/segment metadata only.
// open validates the entire selection before any row can be emitted.
class BinaryMarketCorpusCursor final {
  public:
    BinaryMarketCorpusCursor() noexcept;
    ~BinaryMarketCorpusCursor() noexcept;
    BinaryMarketCorpusCursor(BinaryMarketCorpusCursor&&) noexcept;
    BinaryMarketCorpusCursor& operator=(BinaryMarketCorpusCursor&&) noexcept;
    BinaryMarketCorpusCursor(const BinaryMarketCorpusCursor&) = delete;
    BinaryMarketCorpusCursor& operator=(const BinaryMarketCorpusCursor&) = delete;
    [[nodiscard]] Status open(const BinaryMarketSelectionRequest& request,
                              BinaryMarketSelection& metadata,
                              std::string& error) noexcept;
    [[nodiscard]] Status next(BinaryMarketRecord& output, bool& available,
                              std::string& error) noexcept;
  private:
    std::unique_ptr<BinaryMarketCorpusCursorState> state_{};
};

class BinaryMarketCorpusReader final {
  public:
    [[nodiscard]] Status catalog(const std::filesystem::path& root,
                                 BinaryMarketManifest& manifest,
                                 std::vector<BinaryMarketSource>& sources,
                                 std::string& error) const noexcept;
    [[nodiscard]] Status select(const BinaryMarketSelectionRequest& request,
                                BinaryMarketSelection& output,
                                std::string& error) const noexcept;
};

}  // namespace hftrec::corpus
