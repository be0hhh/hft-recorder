#pragma once


#include "hftrec/corpus_contract/BinaryMarketCorpusFormat.hpp"
#include "hftrec/status.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

namespace hftrec::corpus {

struct BinaryMarketCorpusWriterState;

struct BinaryMarketWriterConfig final {
    std::filesystem::path root{};
    std::span<const BinaryMarketSource> sources{};
    std::uint64_t producerEpoch{0u};
    std::uint64_t maximumBytes{0u};
    std::uint64_t segmentTargetBytes{64u * 1024u * 1024u};
    std::uint64_t targetDurationNs{0u};
    std::int64_t startedReceiveNs{0};
    std::uint64_t startedMonotonicNs{0u};
    std::uint32_t ringCapacity{0u};
    std::uint16_t shardCount{0u};
};

struct BinaryMarketWriterSnapshot final {
    bool active{false};
    bool quotaReached{false};
    std::uint64_t recordCount{0u};
    std::uint64_t gapCount{0u};
    std::uint64_t projectedBytes{0u};
    std::uint32_t segmentCount{0u};
    std::int64_t firstReceiveNs{0};
    std::int64_t lastReceiveNs{0};
};

class BinaryMarketCorpusWriter final {
  public:
    BinaryMarketCorpusWriter() noexcept;
    ~BinaryMarketCorpusWriter() noexcept;
    BinaryMarketCorpusWriter(const BinaryMarketCorpusWriter&) = delete;
    BinaryMarketCorpusWriter& operator=(const BinaryMarketCorpusWriter&) = delete;

    [[nodiscard]] Status start(const BinaryMarketWriterConfig& config) noexcept;
    [[nodiscard]] Status append(const BinaryMarketRecord& record) noexcept;
    [[nodiscard]] Status appendGap(const BinaryMarketGap& gap) noexcept;
    // Switches from the normal capture budget to the hard byte ceiling after
    // parser publication has frozen. start() reserves enough bounded capacity
    // to persist every record that can still be committed in the shard rings.
    [[nodiscard]] Status beginFinalDrain() noexcept;
    [[nodiscard]] Status finalize(BinaryMarketStopReason reason,
                                  std::int64_t finalizedReceiveNs) noexcept;
    [[nodiscard]] BinaryMarketWriterSnapshot snapshot() const noexcept;

  private:
    std::unique_ptr<BinaryMarketCorpusWriterState> state_{};
};

}  // namespace hftrec::corpus
