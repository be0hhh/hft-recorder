#pragma once

#include "core/common/Status.hpp"
#include "core/corpus/BinaryMarketCorpusFormat.hpp"

#include <cstdint>
#include <filesystem>
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
};

struct BinaryMarketSelection final {
    BinaryMarketManifest manifest{};
    BinaryMarketSource source{};
    std::vector<BinaryMarketRecord> records{};
    std::vector<BinaryMarketGap> gaps{};
    std::uint16_t presentChannelMask{0u};
    std::uint64_t sourceGeneration{0u};
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
