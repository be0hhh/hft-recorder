#pragma once

#include "hftrec/CorpusContract/BinaryMarketCorpusFormat.hpp"
#include "hft_compressor/ByteBlock.hpp"
#include "cxet/Primitives/Buf/AsmPrimitives.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace hftrec::corpus {

static_assert(static_cast<std::uint8_t>(ByteBlockCodecMode::Opaque) ==
              static_cast<std::uint8_t>(hft_compressor::ByteBlockCodecMode::Opaque));
static_assert(static_cast<std::uint8_t>(ByteBlockCodecMode::TransformOnly) ==
              static_cast<std::uint8_t>(hft_compressor::ByteBlockCodecMode::TransformOnly));
static_assert(static_cast<std::uint8_t>(ByteBlockCodecMode::Lz4Fast) ==
              static_cast<std::uint8_t>(hft_compressor::ByteBlockCodecMode::Lz4Fast));
static_assert(static_cast<std::uint8_t>(ByteBlockCodecMode::ZstdFast) ==
              static_cast<std::uint8_t>(hft_compressor::ByteBlockCodecMode::ZstdFast));
static_assert(static_cast<std::uint8_t>(ByteBlockCodecMode::ZstdMax) ==
              static_cast<std::uint8_t>(hft_compressor::ByteBlockCodecMode::ZstdMax));
static_assert(static_cast<std::uint8_t>(ByteBlockCodecMode::Brotli) ==
              static_cast<std::uint8_t>(hft_compressor::ByteBlockCodecMode::Brotli));
static_assert(static_cast<std::uint8_t>(ByteBlockCodecMode::XzLzma) ==
              static_cast<std::uint8_t>(hft_compressor::ByteBlockCodecMode::XzLzma));
static_assert(static_cast<std::uint8_t>(ByteBlockCodecMode::ArithmeticShift) ==
              static_cast<std::uint8_t>(hft_compressor::ByteBlockCodecMode::ArithmeticShift));
static_assert(static_cast<std::uint8_t>(ByteBlockCodecMode::RansByteStatic) ==
              static_cast<std::uint8_t>(hft_compressor::ByteBlockCodecMode::RansByteStatic));

[[nodiscard]] inline constexpr hft_compressor::ByteBlockCodecMode toCompressorCodecMode(
    ByteBlockCodecMode mode) noexcept {
    return static_cast<hft_compressor::ByteBlockCodecMode>(
        static_cast<std::uint8_t>(mode));
}

inline void copyBinaryMarketRecord(BinaryMarketRecord& to,
                                   const BinaryMarketRecord& from) noexcept {
    cxet::bytescan::hftMemcpy(&to, &from, sizeof(from));
}

[[nodiscard]] inline hft_compressor::ByteBlockLayout binaryMarketStreamBlockLayout(
    BinaryMarketBlockStream stream) noexcept {
    hft_compressor::ByteBlockLayout layout{};
    layout.recordBytes = binaryMarketCompactRecordBytes(stream);
    layout.payloadOffset = offsetof(BinaryMarketRecord, payload);
    layout.timestampOffset = offsetof(BinaryMarketRecord, header) +
        offsetof(BinaryMarketRecordHeader, exchangeTimestampNs);
    switch (stream) {
        case BinaryMarketBlockStream::Trade:
            layout.streamType = hft_compressor::StreamType::Trades;
            break;
        case BinaryMarketBlockStream::BookTicker:
            layout.streamType = hft_compressor::StreamType::BookTicker;
            break;
        case BinaryMarketBlockStream::Depth:
            layout.streamType = hft_compressor::StreamType::Depth;
            layout.depthLevelCountOffset = offsetof(BinaryMarketRecord, payload) +
                offsetof(BinaryMarketDepthChunkPayload, levelCount);
            layout.depthLevelsOffset = offsetof(BinaryMarketRecord, payload) +
                offsetof(BinaryMarketDepthChunkPayload, levels);
            layout.depthLevelStride = sizeof(BinaryMarketDepthLevel);
            layout.depthLevelCapacity = kBinaryMarketDepthLevelsPerRecord;
            break;
        case BinaryMarketBlockStream::DepthCoverage:
            layout.streamType = hft_compressor::StreamType::DepthCoverage;
            break;
        default:
            layout.streamType = hft_compressor::StreamType::Unknown;
            break;
    }
    return layout;
}

[[nodiscard]] inline hft_compressor::ByteBlockLayout binaryMarketBlockLayout(
    BinaryMarketChannel channel) noexcept {
    return binaryMarketStreamBlockLayout(
        static_cast<BinaryMarketBlockStream>(static_cast<std::uint8_t>(channel)));
}

inline void packCompactBinaryMarketRecord(
    std::uint8_t* dst,
    const BinaryMarketRecord& record,
    BinaryMarketBlockStream stream) noexcept {
    const std::size_t compactBytes = binaryMarketCompactRecordBytes(stream);
    cxet::bytescan::hftMemcpy(dst, &record, compactBytes);
}

inline void unpackCompactBinaryMarketRecord(
    BinaryMarketRecord& dst,
    const std::uint8_t* src,
    BinaryMarketBlockStream stream) noexcept {
    const std::size_t compactBytes = binaryMarketCompactRecordBytes(stream);
    cxet::bytescan::hftMemcpy(&dst, src, compactBytes);
    auto* raw = reinterpret_cast<std::uint8_t*>(&dst);
    std::fill_n(raw + compactBytes, sizeof(BinaryMarketRecord) - compactBytes, std::uint8_t{0u});
}

[[nodiscard]] inline std::uint64_t binaryMarketPendingStreamRecordBytes(
    BinaryMarketBlockStream stream) noexcept {
    return sizeof(BinaryMarketBlockHeader) +
        sizeof(BinaryMarketBlockDirectoryEntry) +
        hft_compressor::byteBlockEncodeBound(
            binaryMarketStreamBlockLayout(stream), 1u);
}

[[nodiscard]] inline std::uint64_t binaryMarketPendingRecordBytes() noexcept {
    return binaryMarketPendingStreamRecordBytes(BinaryMarketBlockStream::Depth);
}

}  // namespace hftrec::corpus
