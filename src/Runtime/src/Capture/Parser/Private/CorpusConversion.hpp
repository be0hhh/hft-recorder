#pragma once

#include "../../../Corpus/BinaryMarketCorpusWriter.hpp"
#include "hft_parser/Ipc/MarketCaptureProtocol.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace hftrec::capture::detail {

[[nodiscard]] bool validSourceDescriptor(
    const hft_parser::ipc::MarketCaptureSourceDescriptor& source,
    std::size_t index) noexcept;
[[nodiscard]] corpus::BinaryMarketSource convertSource(
    const hft_parser::ipc::MarketCaptureSourceDescriptor& input) noexcept;
[[nodiscard]] bool convertRecord(
    const hft_parser::ipc::MarketCaptureRecord& input,
    std::uint16_t expectedShard,
    std::span<const hft_parser::ipc::MarketCaptureSourceDescriptor> directory,
    corpus::BinaryMarketRecord& output) noexcept;

}  // namespace hftrec::capture::detail
