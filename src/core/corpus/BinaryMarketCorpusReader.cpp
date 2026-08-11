#include "core/corpus/BinaryMarketCorpusReader.hpp"

#include "core/codec/Crc32c.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <fstream>
#include <limits>
#include <string_view>
#include <system_error>
#include <unordered_set>

namespace hftrec::corpus {
namespace {

[[nodiscard]] std::uint32_t crcBytes(const void* data,
                                     std::size_t bytes) noexcept {
    return codec::crc32c(static_cast<const std::uint8_t*>(data), bytes);
}

template <typename Header>
[[nodiscard]] std::uint32_t headerCrc(Header value,
                                      std::uint32_t Header::* field) noexcept {
    value.*field = 0u;
    return crcBytes(&value, sizeof(value));
}

[[nodiscard]] bool readBytes(std::ifstream& stream,
                             void* data,
                             std::size_t bytes) noexcept {
    stream.read(static_cast<char*>(data), static_cast<std::streamsize>(bytes));
    return stream.good();
}

[[nodiscard]] bool exactFileBytes(const std::filesystem::path& path,
                                  std::uint64_t expected) noexcept {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    return !error && size == expected;
}

template <typename Record>
[[nodiscard]] bool readTable(const std::filesystem::path& path,
                             std::uint32_t magic,
                             std::vector<Record>& records,
                             std::uint32_t expectedCrc,
                             std::string& error) noexcept {
    std::ifstream stream(path, std::ios::binary);
    BinaryMarketTableHeader header{};
    if (!stream.is_open() || !readBytes(stream, &header, sizeof(header))) {
        error = "binary corpus table is unavailable: " + path.string();
        return false;
    }
    const std::uint64_t recordsBytes =
        static_cast<std::uint64_t>(header.recordCount) * sizeof(Record);
    if (header.magic != magic ||
        header.schemaVersion != kBinaryMarketCorpusSchemaVersion ||
        header.headerBytes != sizeof(header) ||
        header.recordBytes != sizeof(Record) ||
        header.reserved != decltype(header.reserved){} ||
        header.headerCrc32c !=
            headerCrc(header, &BinaryMarketTableHeader::headerCrc32c) ||
        header.recordsCrc32c != expectedCrc ||
        recordsBytes > std::numeric_limits<std::size_t>::max() ||
        !exactFileBytes(path, sizeof(header) + recordsBytes)) {
        error = "binary corpus table contract mismatch: " + path.string();
        return false;
    }
    try {
        records.resize(header.recordCount);
    } catch (...) {
        error = "binary corpus table allocation failed";
        return false;
    }
    if (recordsBytes != 0u &&
        !readBytes(stream, records.data(), static_cast<std::size_t>(recordsBytes))) {
        error = "binary corpus table is truncated: " + path.string();
        return false;
    }
    if (crcBytes(records.data(), static_cast<std::size_t>(recordsBytes)) !=
        header.recordsCrc32c) {
        error = "binary corpus table CRC mismatch: " + path.string();
        return false;
    }
    return true;
}

[[nodiscard]] bool readManifest(const std::filesystem::path& root,
                                BinaryMarketManifest& manifest,
                                std::string& error) noexcept {
    const auto path = root / "manifest.bin";
    if (!exactFileBytes(path, sizeof(manifest))) {
        error = "sealed binary corpus manifest is missing";
        return false;
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream.is_open() || !readBytes(stream, &manifest, sizeof(manifest))) {
        error = "binary corpus manifest is unreadable";
        return false;
    }
    const std::uint32_t storedCrc = manifest.manifestCrc32c;
    manifest.manifestCrc32c = 0u;
    const std::uint32_t calculated = crcBytes(&manifest, sizeof(manifest));
    manifest.manifestCrc32c = storedCrc;
    const bool hasRecords = manifest.recordCount != 0u;
    const std::uint64_t maximumGapCount =
        static_cast<std::uint64_t>(manifest.sourceCount) *
        kBinaryMarketChannelCount;
    if (manifest.magic != kBinaryMarketManifestMagic ||
        manifest.schemaVersion != kBinaryMarketCorpusSchemaVersion ||
        manifest.headerBytes != sizeof(manifest) ||
        manifest.writerAbiFingerprint != binaryMarketCorpusAbiFingerprint() ||
        manifest.producerEpoch == 0u || manifest.startedReceiveNs <= 0 ||
        manifest.replayAnchorReceiveNs <= 0 ||
        manifest.replayAnchorMonotonicNs == 0u ||
        manifest.finalizedReceiveNs < manifest.startedReceiveNs ||
        manifest.replayAnchorReceiveNs < manifest.startedReceiveNs ||
        manifest.replayAnchorReceiveNs > manifest.finalizedReceiveNs ||
        manifest.maximumBytes == 0u ||
        manifest.segmentTargetBytes < sizeof(BinaryMarketSegmentHeader) +
            sizeof(BinaryMarketRecord) + sizeof(BinaryMarketSegmentFooter) ||
        manifest.projectedBytes == 0u ||
        manifest.projectedBytes > manifest.maximumBytes ||
        manifest.sourceCount == 0u ||
        manifest.shardCount == 0u || manifest.complete != 1u ||
        manifest.stopReason < BinaryMarketStopReason::Requested ||
        manifest.stopReason > BinaryMarketStopReason::ParserDisconnected ||
        manifest.gapCount > maximumGapCount ||
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
        error = "binary corpus manifest contract mismatch";
        return false;
    }
    return true;
}

[[nodiscard]] std::string_view fixedText(const char* data,
                                         std::size_t capacity,
                                         std::uint8_t bytes) noexcept {
    return data && bytes != 0u && bytes <= capacity
        ? std::string_view(data, bytes) : std::string_view{};
}

[[nodiscard]] std::filesystem::path segmentPath(
    const std::filesystem::path& root,
    std::uint16_t shard,
    std::uint32_t segment) {
    char name[96]{};
    (void)std::snprintf(name, sizeof(name),
                        "shard-%04u-segment-%06u.cxm",
                        static_cast<unsigned>(shard),
                        static_cast<unsigned>(segment));
    return root / "segments" / name;
}

struct SegmentKey final {
    std::uint32_t number{0u};
    std::uint16_t shard{0u};

    [[nodiscard]] bool operator==(const SegmentKey&) const noexcept = default;
};

struct SegmentKeyHash final {
    [[nodiscard]] std::size_t operator()(const SegmentKey& value) const noexcept {
        return (static_cast<std::size_t>(value.number) << 16u) ^ value.shard;
    }
};

struct IndexIdentity final {
    SegmentKey segment{};
    std::uint32_t sourceId{0u};
    std::uint64_t sourceGeneration{0u};
    BinaryMarketChannel channel{BinaryMarketChannel::BookTicker};

    [[nodiscard]] bool operator==(const IndexIdentity&) const noexcept =
        default;
};

struct IndexIdentityHash final {
    [[nodiscard]] std::size_t operator()(
        const IndexIdentity& value) const noexcept {
        std::size_t hash = SegmentKeyHash{}(value.segment);
        hash ^= static_cast<std::size_t>(value.sourceId) *
            std::size_t{0x9e3779b1u};
        hash ^= static_cast<std::size_t>(value.sourceGeneration ^
                                        (value.sourceGeneration >> 32u));
        hash ^= static_cast<std::size_t>(value.channel) << 9u;
        return hash;
    }
};

[[nodiscard]] bool rangesOverlap(std::int64_t first,
                                 std::int64_t last,
                                 std::int64_t begin,
                                 std::int64_t end) noexcept {
    return first != 0 && last >= first && last >= begin && first < end;
}

[[nodiscard]] bool readSelectedSegment(
    const std::filesystem::path& root,
    SegmentKey key,
    std::uint64_t producerEpoch,
    const std::vector<BinaryMarketSource>& sources,
    std::uint32_t sourceId,
    std::uint16_t channelMask,
    std::int64_t beginReceiveNs,
    std::int64_t endReceiveNs,
    std::vector<BinaryMarketRecord>& output,
    std::string& error) noexcept {
    const auto path = segmentPath(root, key.shard, key.number);
    std::ifstream stream(path, std::ios::binary);
    BinaryMarketSegmentHeader header{};
    if (!stream.is_open() || !readBytes(stream, &header, sizeof(header))) {
        error = "binary corpus segment is unavailable: " + path.string();
        return false;
    }
    if (header.recordCount >
        std::numeric_limits<std::uint64_t>::max() /
            sizeof(BinaryMarketRecord)) {
        error = "binary corpus segment record count overflow: " + path.string();
        return false;
    }
    const std::uint64_t recordBytes =
        header.recordCount * sizeof(BinaryMarketRecord);
    const std::uint64_t expectedBytes = sizeof(header) + recordBytes +
        sizeof(BinaryMarketSegmentFooter);
    if (header.magic != kBinaryMarketSegmentMagic ||
        header.schemaVersion != kBinaryMarketCorpusSchemaVersion ||
        header.headerBytes != sizeof(header) ||
        header.recordBytes != sizeof(BinaryMarketRecord) ||
        header.recordCount == 0u || header.firstReceiveNs <= 0 ||
        header.lastReceiveNs < header.firstReceiveNs ||
        header.segmentNumber != key.number || header.shardIndex != key.shard ||
        header.producerEpoch != producerEpoch || header.reserved16 != 0u ||
        header.reserved != decltype(header.reserved){} ||
        header.headerCrc32c !=
            headerCrc(header, &BinaryMarketSegmentHeader::headerCrc32c) ||
        !exactFileBytes(path, expectedBytes)) {
        error = "binary corpus segment header mismatch: " + path.string();
        return false;
    }
    std::uint32_t recordsCrc = 0u;
    std::int64_t observedFirstReceiveNs = 0;
    std::int64_t observedLastReceiveNs = 0;
    std::uint64_t previousShardSequence = 0u;
    for (std::uint64_t index = 0u; index < header.recordCount; ++index) {
        BinaryMarketRecord record{};
        if (!readBytes(stream, &record, sizeof(record)) ||
            !validBinaryMarketRecord(record) ||
            record.header.shardIndex != key.shard ||
            record.header.sourceId > sources.size() ||
            record.header.sourceGeneration !=
                sources[record.header.sourceId - 1u].initialSourceGeneration ||
            (sources[record.header.sourceId - 1u].availableChannelMask &
             binaryMarketChannelBit(record.header.channel)) == 0u ||
            (previousShardSequence != 0u &&
             record.header.shardSequence <= previousShardSequence) ||
            !std::all_of(
                record.payload.begin() + record.header.payloadBytes,
                record.payload.end(),
                [](std::byte value) noexcept {
                    return value == std::byte{};
                })) {
            error = "binary corpus segment record mismatch: " + path.string();
            return false;
        }
        const auto recordCompatibility =
            sources[record.header.sourceId - 1u].compatibility[
                binaryMarketChannelIndex(record.header.channel)];
        if (recordCompatibility == BinaryMarketCompatibility::Unavailable ||
            (((record.header.flags &
               BinaryMarketRecordTraderReplayCompatible) != 0u) &&
             recordCompatibility !=
                 BinaryMarketCompatibility::ExactTraderReplay)) {
            error = "binary corpus record contradicts source compatibility: " +
                path.string();
            return false;
        }
        previousShardSequence = record.header.shardSequence;
        if (observedFirstReceiveNs == 0 ||
            record.header.receiveRealtimeNs < observedFirstReceiveNs) {
            observedFirstReceiveNs = record.header.receiveRealtimeNs;
        }
        if (record.header.receiveRealtimeNs > observedLastReceiveNs) {
            observedLastReceiveNs = record.header.receiveRealtimeNs;
        }
        recordsCrc = codec::crc32cUpdate(
            recordsCrc, reinterpret_cast<const std::uint8_t*>(&record),
            sizeof(record));
        if (record.header.sourceId == sourceId &&
            (channelMask & binaryMarketChannelBit(record.header.channel)) != 0u &&
            record.header.receiveRealtimeNs >= beginReceiveNs &&
            record.header.receiveRealtimeNs < endReceiveNs) {
            try {
                output.push_back(record);
            } catch (...) {
                error = "binary corpus selection allocation failed";
                return false;
            }
        }
    }
    BinaryMarketSegmentFooter footer{};
    if (!readBytes(stream, &footer, sizeof(footer)) ||
        footer.magic != kBinaryMarketSegmentFooterMagic ||
        footer.schemaVersion != kBinaryMarketCorpusSchemaVersion ||
        footer.footerBytes != sizeof(footer) ||
        footer.segmentNumber != header.segmentNumber ||
        footer.recordCount != header.recordCount ||
        footer.recordsCrc32c != header.recordsCrc32c ||
        footer.firstReceiveNs != header.firstReceiveNs ||
        footer.lastReceiveNs != header.lastReceiveNs ||
        observedFirstReceiveNs != header.firstReceiveNs ||
        observedLastReceiveNs != header.lastReceiveNs ||
        footer.reserved != decltype(footer.reserved){} ||
        footer.footerCrc32c !=
            headerCrc(footer, &BinaryMarketSegmentFooter::footerCrc32c) ||
        recordsCrc != header.recordsCrc32c) {
        error = "binary corpus segment footer or CRC mismatch: " + path.string();
        return false;
    }
    return true;
}

}  // namespace

Status BinaryMarketCorpusReader::catalog(
    const std::filesystem::path& root,
    BinaryMarketManifest& manifest,
    std::vector<BinaryMarketSource>& sources,
    std::string& error) const noexcept {
    error.clear();
    manifest = {};
    sources.clear();
    try {
        if (root.empty() || !readManifest(root, manifest, error))
            return Status::CorruptData;
        if (!readTable(root / "sources.bin", kBinaryMarketSourcesMagic,
                       sources, manifest.sourcesCrc32c, error) ||
            sources.size() != manifest.sourceCount) return Status::CorruptData;
        for (std::size_t index = 0u; index < sources.size(); ++index) {
            if (!validBinaryMarketSource(sources[index], index)) {
                error = "binary corpus source directory mismatch";
                return Status::CorruptData;
            }
        }
        return Status::Ok;
    } catch (...) {
        error = "binary corpus catalog read failed";
        return Status::IoError;
    }
}

Status BinaryMarketCorpusReader::select(
    const BinaryMarketSelectionRequest& request,
    BinaryMarketSelection& output,
    std::string& error) const noexcept {
    output = {};
    error.clear();
    if (request.root.empty() || request.exchange.empty() ||
        request.market.empty() || request.canonicalSymbol.empty() ||
        request.beginReceiveNs <= 0 ||
        request.endReceiveNs <= request.beginReceiveNs ||
        request.channelMask == 0u ||
        (request.channelMask & ~std::uint16_t{0x00ffu}) != 0u ||
        (request.requiredChannelMask & ~request.channelMask) != 0u) {
        error = "invalid binary corpus selection";
        return Status::InvalidArgument;
    }
    try {
        std::vector<BinaryMarketSource> sources;
        Status status = catalog(request.root, output.manifest, sources, error);
        if (!isOk(status)) return status;
        if (request.beginReceiveNs < output.manifest.startedReceiveNs ||
            request.endReceiveNs > output.manifest.finalizedReceiveNs) {
            error = "selected receive interval is outside sealed capture coverage";
            return Status::OutOfRange;
        }
        const BinaryMarketSource* selected = nullptr;
        for (const auto& source : sources) {
            if (fixedText(source.venue.data(), source.venue.size(),
                          source.venueBytes) == request.exchange &&
                fixedText(source.market.data(), source.market.size(),
                          source.marketBytes) == request.market &&
                fixedText(source.canonicalSymbol.data(),
                          source.canonicalSymbol.size(),
                          source.canonicalSymbolBytes) ==
                    request.canonicalSymbol) {
                if (selected) {
                    error = "binary corpus source selection is ambiguous";
                    return Status::InvalidArgument;
                }
                selected = &source;
            }
        }
        if (!selected) {
            error = "binary corpus source was not found";
            return Status::OutOfRange;
        }
        output.source = *selected;
        if ((request.channelMask & ~selected->availableChannelMask) != 0u) {
            error = "selected binary corpus channels were not captured";
            return Status::OutOfRange;
        }
        if (request.requireTraderReplayCompatibility &&
            (request.requiredChannelMask &
             ~selected->traderReplayChannelMask) != 0u) {
            error = "required binary corpus channel is not trader-replay compatible";
            return Status::Unimplemented;
        }

        std::vector<BinaryMarketIndexEntry> index;
        std::vector<BinaryMarketGap> gaps;
        if (!readTable(request.root / "index.bin", kBinaryMarketIndexMagic,
                       index, output.manifest.indexCrc32c, error) ||
            index.size() != output.manifest.indexCount ||
            !readTable(request.root / "gaps.bin", kBinaryMarketGapsMagic,
                       gaps, output.manifest.gapsCrc32c, error)) {
            return Status::CorruptData;
        }
        if (gaps.size() != output.manifest.gapCount) {
            error = "binary corpus gap count does not match manifest";
            return Status::CorruptData;
        }
        std::unordered_set<SegmentKey, SegmentKeyHash> allSegments;
        std::unordered_set<IndexIdentity, IndexIdentityHash> indexIdentities;
        std::unordered_set<SegmentKey, SegmentKeyHash> segments;
        std::vector<std::uint16_t> segmentShards(
            static_cast<std::size_t>(output.manifest.segmentCount) + 1u,
            std::numeric_limits<std::uint16_t>::max());
        std::uint64_t indexedRecordCount = 0u;
        for (const auto& entry : index) {
            const std::uint32_t knownIndexFlags =
                BinaryMarketIndexHasGap |
                BinaryMarketIndexHasRecordedOnly;
            const bool hasRecordedOnly =
                (entry.flags & BinaryMarketIndexHasRecordedOnly) != 0u;
            const bool hasGap =
                (entry.flags & BinaryMarketIndexHasGap) != 0u;
            if (entry.segmentNumber == 0u ||
                entry.segmentNumber > output.manifest.segmentCount ||
                entry.sourceId == 0u ||
                entry.sourceId > sources.size() ||
                entry.sourceGeneration == 0u ||
                entry.recordCount == 0u ||
                entry.firstEventSequence == 0u ||
                entry.lastEventSequence == 0u ||
                entry.firstReceiveNs <= 0 ||
                entry.lastReceiveNs < entry.firstReceiveNs ||
                entry.shardIndex >= output.manifest.shardCount ||
                !validBinaryMarketChannel(entry.channel) ||
                (entry.flags & ~knownIndexFlags) != 0u ||
                (hasGap != (entry.gapCount != 0u)) ||
                !validBinaryMarketCompatibility(entry.compatibility) ||
                entry.compatibility ==
                    BinaryMarketCompatibility::Unavailable ||
                (hasRecordedOnly !=
                 (entry.compatibility ==
                  BinaryMarketCompatibility::RecordedOnly))) {
                error = "binary corpus index entry mismatch";
                return Status::CorruptData;
            }
            const auto& indexedSource = sources[entry.sourceId - 1u];
            const std::uint16_t channelBit =
                binaryMarketChannelBit(entry.channel);
            const auto declaredCompatibility =
                indexedSource.compatibility[
                    binaryMarketChannelIndex(entry.channel)];
            if (entry.sourceGeneration !=
                    indexedSource.initialSourceGeneration ||
                (indexedSource.availableChannelMask & channelBit) == 0u ||
                declaredCompatibility ==
                    BinaryMarketCompatibility::Unavailable ||
                (entry.compatibility ==
                     BinaryMarketCompatibility::ExactTraderReplay &&
                 declaredCompatibility !=
                     BinaryMarketCompatibility::ExactTraderReplay)) {
                error = "binary corpus index contradicts source directory";
                return Status::CorruptData;
            }
            const SegmentKey segmentKey{entry.segmentNumber,
                                        entry.shardIndex};
            auto& segmentShard = segmentShards[entry.segmentNumber];
            if (segmentShard == std::numeric_limits<std::uint16_t>::max()) {
                segmentShard = entry.shardIndex;
            } else if (segmentShard != entry.shardIndex) {
                error = "binary corpus segment number crosses shards";
                return Status::CorruptData;
            }
            if (!indexIdentities.insert(IndexIdentity{
                    segmentKey, entry.sourceId, entry.sourceGeneration,
                    entry.channel}).second ||
                indexedRecordCount >
                    std::numeric_limits<std::uint64_t>::max() -
                        entry.recordCount) {
                error = "binary corpus index contains duplicates or overflow";
                return Status::CorruptData;
            }
            indexedRecordCount += entry.recordCount;
            allSegments.insert(segmentKey);
            if (entry.sourceId != selected->sourceId ||
                (request.channelMask &
                 binaryMarketChannelBit(entry.channel)) == 0u ||
                !rangesOverlap(entry.firstReceiveNs, entry.lastReceiveNs,
                               request.beginReceiveNs,
                               request.endReceiveNs)) continue;
            segments.insert(segmentKey);
        }
        if (indexedRecordCount != output.manifest.recordCount ||
            allSegments.size() != output.manifest.segmentCount) {
            error = "binary corpus index totals do not match manifest";
            return Status::CorruptData;
        }
        for (const auto& segment : segments) {
            if (!readSelectedSegment(
                    request.root, segment, output.manifest.producerEpoch,
                    sources,
                    selected->sourceId, request.channelMask,
                    request.beginReceiveNs, request.endReceiveNs,
                    output.records, error)) return Status::CorruptData;
        }
        for (const auto& gap : gaps) {
            if (gap.sourceId == 0u || gap.sourceId > sources.size() ||
                gap.sourceGeneration == 0u || gap.gapEpoch == 0u ||
                gap.droppedRecords == 0u || gap.observedReceiveNs <= 0 ||
                !((gap.minimumDroppedReceiveNs > 0 &&
                   gap.maximumDroppedReceiveNs >=
                       gap.minimumDroppedReceiveNs) ||
                  (gap.minimumDroppedReceiveNs == 0 &&
                   gap.maximumDroppedReceiveNs == 0)) ||
                gap.firstDroppedEventSequence == 0u ||
                gap.lastDroppedEventSequence == 0u ||
                gap.gapEpoch != gap.droppedRecords ||
                gap.shardIndex >= output.manifest.shardCount ||
                !validBinaryMarketChannel(gap.channel) || gap.reserved32 != 0u ||
                gap.reserved8 != 0u) {
                error = "binary corpus gap entry mismatch";
                return Status::CorruptData;
            }
            if (gap.sourceGeneration !=
                    sources[gap.sourceId - 1u].initialSourceGeneration ||
                (sources[gap.sourceId - 1u].availableChannelMask &
                 binaryMarketChannelBit(gap.channel)) == 0u) {
                error = "binary corpus gap contradicts source directory";
                return Status::CorruptData;
            }
            const bool arrivalRangeUnavailable =
                gap.minimumDroppedReceiveNs == 0 &&
                gap.maximumDroppedReceiveNs == 0;
            if (gap.sourceId == selected->sourceId &&
                (request.channelMask & binaryMarketChannelBit(gap.channel)) != 0u &&
                (arrivalRangeUnavailable ||
                 rangesOverlap(gap.minimumDroppedReceiveNs,
                               gap.maximumDroppedReceiveNs,
                               request.beginReceiveNs,
                               request.endReceiveNs))) {
                output.gaps.push_back(gap);
            }
        }
        if (!output.gaps.empty()) {
            error = "selected binary corpus interval contains capture gaps";
            return Status::CorruptData;
        }
        std::sort(output.records.begin(), output.records.end(),
                  [](const BinaryMarketRecord& left,
                     const BinaryMarketRecord& right) noexcept {
            if (left.header.receiveMonotonicNs != right.header.receiveMonotonicNs)
                return left.header.receiveMonotonicNs < right.header.receiveMonotonicNs;
            if (left.header.shardIndex != right.header.shardIndex)
                return left.header.shardIndex < right.header.shardIndex;
            if (left.header.shardSequence != right.header.shardSequence)
                return left.header.shardSequence < right.header.shardSequence;
            if (left.header.receiveRealtimeNs != right.header.receiveRealtimeNs)
                return left.header.receiveRealtimeNs < right.header.receiveRealtimeNs;
            if (left.header.frameSequence != right.header.frameSequence)
                return left.header.frameSequence < right.header.frameSequence;
            if (left.header.eventOrdinal != right.header.eventOrdinal)
                return left.header.eventOrdinal < right.header.eventOrdinal;
            return left.header.eventSequence < right.header.eventSequence;
        });
        std::uint64_t generation = 0u;
        for (const auto& record : output.records) {
            const std::uint16_t bit = binaryMarketChannelBit(record.header.channel);
            output.presentChannelMask |= bit;
            if ((record.header.sourceFlags &
                 BinaryMarketSourceSequenceGap) != 0u) {
                error = "selected binary corpus interval contains a source sequence gap";
                return Status::CorruptData;
            }
            if ((record.header.sourceFlags &
                 (BinaryMarketSourceStale |
                  BinaryMarketSourceDegraded)) != 0u) {
                error = "selected binary corpus interval is stale or degraded";
                return Status::CorruptData;
            }
            if (generation == 0u) generation = record.header.sourceGeneration;
            else if (generation != record.header.sourceGeneration) {
                error = "selected binary corpus interval crosses source generations";
                return Status::CorruptData;
            }
            if (request.requireTraderReplayCompatibility &&
                (record.header.flags &
                 BinaryMarketRecordTraderReplayCompatible) == 0u) {
                error = "selected binary corpus record is not trader-replay compatible";
                return Status::Unimplemented;
            }
        }
        if ((request.requiredChannelMask & ~output.presentChannelMask) != 0u) {
            error = "selected binary corpus interval lacks required channel data";
            return Status::OutOfRange;
        }
        output.sourceGeneration = generation;
        return Status::Ok;
    } catch (...) {
        error = "binary corpus selection failed";
        return Status::IoError;
    }
}

}  // namespace hftrec::corpus
