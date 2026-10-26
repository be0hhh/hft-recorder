#pragma once
#include "Codec/Crc32c.hpp"
#include "hftrec/CorpusContract/BinaryMarketCorpusFormat.hpp"
#include "cxet/Primitives/Buf/AsmPrimitives.hpp"
#include <algorithm>
#include <array>
#include <limits>

namespace hftrec::corpus::metadata {
[[nodiscard]] inline std::uint32_t crcBytes(const void* data,std::size_t bytes) noexcept {
    return codec::crc32c(static_cast<const std::uint8_t*>(data),bytes);
}
template <typename Header>
[[nodiscard]] inline std::uint32_t headerCrc(Header value,std::uint32_t Header::* field) noexcept {
    value.*field=0u;return crcBytes(&value,sizeof(value));
}
[[nodiscard]] inline bool validSealedManifest(const BinaryMarketManifest& manifest) noexcept {
    const std::uint32_t storedCrc = manifest.manifestCrc32c;
    std::array<std::uint8_t,sizeof(BinaryMarketManifest)> bytes{};
    cxet::bytescan::hftMemcpy(bytes.data(),&manifest,sizeof(manifest));
    std::fill_n(bytes.begin()+offsetof(BinaryMarketManifest,manifestCrc32c),sizeof(storedCrc),0u);
    const std::uint32_t calculated = crcBytes(bytes.data(),bytes.size());
    const bool hasRecords = manifest.recordCount != 0u;
    const auto recordBytes = sizeof(BinaryMarketRecord);
    const std::uint64_t maximumGapCount =
        static_cast<std::uint64_t>(manifest.sourceCount) *
        kBinaryMarketStoredChannelCount;
    if (manifest.magic != kBinaryMarketManifestMagic ||
        manifest.headerBytes != sizeof(manifest) ||
        manifest.producerEpoch == 0u || manifest.startedReceiveNs <= 0 ||
        manifest.replayAnchorReceiveNs <= 0 ||
        manifest.replayAnchorMonotonicNs == 0u ||
        manifest.finalizedReceiveNs < manifest.startedReceiveNs ||
        manifest.replayAnchorReceiveNs < manifest.startedReceiveNs ||
        manifest.replayAnchorReceiveNs > manifest.finalizedReceiveNs ||
        manifest.maximumBytes == 0u ||
        manifest.segmentTargetBytes < sizeof(BinaryMarketSegmentHeader) +
            recordBytes + sizeof(BinaryMarketSegmentFooter) ||
        manifest.projectedBytes == 0u ||
        manifest.projectedBytes > manifest.maximumBytes ||
        manifest.sourceCount == 0u || manifest.sourceCount > kBinaryMarketMaximumSources ||
        manifest.shardCount == 0u || manifest.shardCount > kBinaryMarketMaximumShards || manifest.complete != 1u ||
        manifest.stopReason < BinaryMarketStopReason::Requested ||
        manifest.stopReason > BinaryMarketStopReason::ParserDisconnected ||
        manifest.gapCount > maximumGapCount ||
        manifest.indexCount > manifest.maximumBytes / sizeof(BinaryMarketIndexEntry) ||
        manifest.segmentCount > manifest.indexCount ||
        (hasRecords != (manifest.segmentCount != 0u)) ||
        (hasRecords != (manifest.indexCount != 0u)) ||
        (hasRecords ? manifest.firstReceiveNs <= 0
                    : manifest.firstReceiveNs != 0) ||
        (hasRecords ? manifest.lastReceiveNs <= 0
                    : manifest.lastReceiveNs != 0) ||
        (hasRecords &&
         manifest.lastReceiveNs < manifest.firstReceiveNs) ||
        manifest.reserved != decltype(manifest.reserved){} ||
        storedCrc != calculated) {
        return false;
    }
    return true;
}
template <typename Record>
[[nodiscard]] inline bool validTableHeader(const BinaryMarketTableHeader& header,
    std::uint32_t magic,std::uint32_t expectedCrc,std::uint64_t maximumBytes,
    std::uint64_t expectedRecords) noexcept {
    const auto recordsBytes=static_cast<std::uint64_t>(header.recordCount)*sizeof(Record);
    return header.magic==magic && header.headerBytes==sizeof(header) &&
        header.recordBytes==sizeof(Record) && header.recordCount==expectedRecords &&
        header.reserved==decltype(header.reserved){} &&
        header.headerCrc32c==headerCrc(header,&BinaryMarketTableHeader::headerCrc32c) &&
        header.recordsCrc32c==expectedCrc &&
        recordsBytes<=std::numeric_limits<std::size_t>::max() && recordsBytes<=maximumBytes;
}
} // namespace hftrec::corpus::metadata
