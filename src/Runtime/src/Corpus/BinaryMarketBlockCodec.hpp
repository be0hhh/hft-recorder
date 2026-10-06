#pragma once
#include "hftrec/CorpusContract/BinaryMarketCorpusFormat.hpp"
#include "hft_compressor/ByteBlock.hpp"
#include <cstddef>
#include <algorithm>

namespace hftrec::corpus {
inline void copyBinaryMarketRecord(BinaryMarketRecord& to, const BinaryMarketRecord& from) noexcept {
    std::copy_n(reinterpret_cast<const std::uint8_t*>(&from), sizeof(from),
                reinterpret_cast<std::uint8_t*>(&to));
}
inline hft_compressor::ByteBlockLayout binaryMarketBlockLayout(BinaryMarketChannel channel) noexcept {
    hft_compressor::ByteBlockLayout layout{};
    layout.recordBytes = sizeof(BinaryMarketRecord);
    layout.payloadOffset = offsetof(BinaryMarketRecord, payload);
    layout.timestampOffset = offsetof(BinaryMarketRecord, header) + offsetof(BinaryMarketRecordHeader, exchangeTimestampNs);
    switch (channel) {
        case BinaryMarketChannel::Trade: layout.streamType = hft_compressor::StreamType::Trades; break;
        case BinaryMarketChannel::BookTicker: layout.streamType = hft_compressor::StreamType::BookTicker; break;
        case BinaryMarketChannel::Depth:
            layout.streamType = hft_compressor::StreamType::Depth;
            layout.depthLevelsOffset = layout.payloadOffset + offsetof(BinaryMarketDepthChunkPayload, levels);
            layout.depthLevelStride = sizeof(BinaryMarketDepthLevel);
            layout.depthLevelCountOffset = layout.payloadOffset + offsetof(BinaryMarketDepthChunkPayload, levelCount);
            layout.depthLevelCapacity = kBinaryMarketDepthLevelsPerRecord;
            break;
        default: layout.streamType = hft_compressor::StreamType::Unknown; break;
    }
    return layout;
}
inline std::uint64_t binaryMarketPendingRecordBytes() noexcept {
    return sizeof(BinaryMarketBlockHeader) +
        hft_compressor::byteBlockEncodeBound(binaryMarketBlockLayout(BinaryMarketChannel::Depth), 1u);
}
} // namespace hftrec::corpus
