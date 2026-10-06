#include "BinaryMarketCorpusReader.hpp"

#include "Codec/Crc32c.hpp"
#include "BinaryMarketBlockCodec.hpp"
#include "BinaryMarketSourceState.hpp"

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
                             std::string& error,
                             std::uint64_t maximumBytes,
                             std::uint64_t expectedRecords) noexcept {
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
        header.recordBytes != sizeof(Record) || header.recordCount != expectedRecords ||
        header.reserved != decltype(header.reserved){} ||
        header.headerCrc32c !=
            headerCrc(header, &BinaryMarketTableHeader::headerCrc32c) ||
        header.recordsCrc32c != expectedCrc ||
        recordsBytes > std::numeric_limits<std::size_t>::max() ||
        recordsBytes > maximumBytes ||
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
        kBinaryMarketStoredChannelCount;
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

struct SegmentDecoder final {
    std::ifstream stream{};
    BinaryMarketSegmentHeader header{};
    std::vector<std::uint8_t> encoded{};
    std::vector<BinaryMarketRecord> decoded{};
    std::size_t position{0u}, count{0u};
    std::uint64_t recordsSeen{0u}, bodyBytes{0u}, previousSequence{0u}, previousMonotonic{0u};
    std::uint32_t blocksSeen{0u}, recordsCrc{0u};
    std::int64_t firstReceive{0}, lastReceive{0};
    bool finished{false};

    bool open(const std::filesystem::path& root, SegmentKey key,
              std::uint64_t epoch, std::string& error) {
        stream.open(segmentPath(root,key.shard,key.number), std::ios::binary);
        if (!stream.is_open() || !readBytes(stream,&header,sizeof(header)) ||
            header.magic != kBinaryMarketSegmentMagic ||
            header.schemaVersion != kBinaryMarketCorpusSchemaVersion ||
            header.headerBytes != sizeof(header) || header.recordBytes != sizeof(BinaryMarketRecord) ||
            header.recordCount == 0u || header.blockCount == 0u ||
            header.blockCount > header.recordCount || header.codecId != 1u ||
            header.firstReceiveNs <= 0 || header.lastReceiveNs < header.firstReceiveNs ||
            header.segmentNumber != key.number || header.shardIndex != key.shard ||
            header.producerEpoch != epoch || header.reserved16 != 0u ||
            header.reserved != decltype(header.reserved){} ||
            header.headerCrc32c != headerCrc(header,&BinaryMarketSegmentHeader::headerCrc32c) ||
            header.storedBlockBytes > std::numeric_limits<std::uint64_t>::max()-sizeof(header)-sizeof(BinaryMarketSegmentFooter) ||
            !exactFileBytes(segmentPath(root,key.shard,key.number),
                sizeof(header)+header.storedBlockBytes+sizeof(BinaryMarketSegmentFooter))) {
            error="binary corpus compressed segment header mismatch"; return false;
        }
        decoded.resize(kBinaryMarketBlockRecords);
        encoded.resize(hft_compressor::byteBlockEncodeBound(
            binaryMarketBlockLayout(BinaryMarketChannel::Depth), kBinaryMarketBlockRecords));
        return true;
    }
    bool next(BinaryMarketRecord& record, bool& available,
              const std::vector<BinaryMarketSource>& sources, std::string& error) {
        available=false;
        if(finished) return true;
        if(position==count) {
            if(blocksSeen==header.blockCount) {
                BinaryMarketSegmentFooter footer{};
                if(!readBytes(stream,&footer,sizeof(footer)) ||
                    footer.magic!=kBinaryMarketSegmentFooterMagic ||
                    footer.schemaVersion!=kBinaryMarketCorpusSchemaVersion || footer.footerBytes!=sizeof(footer) ||
                    footer.segmentNumber!=header.segmentNumber || footer.recordCount!=header.recordCount ||
                    footer.recordsCrc32c!=header.recordsCrc32c || footer.firstReceiveNs!=header.firstReceiveNs ||
                    footer.lastReceiveNs!=header.lastReceiveNs || footer.reserved!=decltype(footer.reserved){} ||
                    footer.footerCrc32c!=headerCrc(footer,&BinaryMarketSegmentFooter::footerCrc32c) ||
                    recordsSeen!=header.recordCount || bodyBytes!=header.storedBlockBytes ||
                    recordsCrc!=header.recordsCrc32c || firstReceive!=header.firstReceiveNs || lastReceive!=header.lastReceiveNs) {
                    error="binary corpus segment footer or CRC mismatch"; return false;
                }
                finished=true; return true;
            }
            BinaryMarketBlockHeader block{};
            if(!readBytes(stream,&block,sizeof(block)) || block.magic!=kBinaryMarketBlockMagic ||
                block.schemaVersion!=kBinaryMarketCorpusSchemaVersion || block.headerBytes!=sizeof(block) ||
                block.blockNumber!=blocksSeen+1u || block.recordCount==0u || block.recordCount>kBinaryMarketBlockRecords ||
                block.rawBytes!=block.recordCount*sizeof(BinaryMarketRecord) || !validBinaryMarketRecordChannel(block.channel) ||
                block.encodedBytes==0u || block.encodedBytes>hft_compressor::byteBlockEncodeBound(binaryMarketBlockLayout(block.channel),block.recordCount) ||
                block.reserved!=decltype(block.reserved){} || block.firstShardSequence==0u ||
                block.lastShardSequence<block.firstShardSequence ||
                block.headerCrc32c!=headerCrc(block,&BinaryMarketBlockHeader::headerCrc32c) ||
                bodyBytes>header.storedBlockBytes || sizeof(block)+block.encodedBytes>header.storedBlockBytes-bodyBytes ||
                recordsSeen>header.recordCount || block.recordCount>header.recordCount-recordsSeen ||
                !readBytes(stream,encoded.data(),block.encodedBytes) ||
                crcBytes(encoded.data(),block.encodedBytes)!=block.encodedCrc32c) {
                error="binary corpus compressed block header or CRC mismatch"; return false;
            }
            std::size_t rawBytes=0u;
            if(hft_compressor::decodeByteBlock(binaryMarketBlockLayout(block.channel),
                std::span<const std::uint8_t>{encoded.data(),block.encodedBytes},
                std::span<std::uint8_t>{reinterpret_cast<std::uint8_t*>(decoded.data()),block.rawBytes},rawBytes)
                !=hft_compressor::Status::Ok || rawBytes!=block.rawBytes ||
                crcBytes(decoded.data(),rawBytes)!=block.rawCrc32c ||
                decoded[0].header.shardSequence!=block.firstShardSequence ||
                decoded[block.recordCount-1u].header.shardSequence!=block.lastShardSequence) {
                error="binary corpus compressed block decode mismatch"; return false;
            }
            // Validate the complete block before releasing even its first row.
            for(std::size_t n=0u;n<block.recordCount;++n) {
                const auto& row=decoded[n]; const auto& h=row.header;
                if(!validBinaryMarketRecord(row) || h.channel!=block.channel || h.shardIndex!=header.shardIndex ||
                    h.sourceId>sources.size() ||
                    (previousSequence!=0u && h.shardSequence<=previousSequence) ||
                    (previousMonotonic!=0u && h.receiveMonotonicNs<previousMonotonic) ||
                    !std::all_of(row.payload.begin()+h.payloadBytes,row.payload.end(),
                        [](std::byte b) noexcept {return b==std::byte{};})) {
                    error="binary corpus segment record mismatch"; return false;
                }
                previousSequence=h.shardSequence; previousMonotonic=h.receiveMonotonicNs;
                if(firstReceive==0 || h.receiveRealtimeNs<firstReceive) firstReceive=h.receiveRealtimeNs;
                lastReceive=std::max(lastReceive,h.receiveRealtimeNs);
            }
            recordsCrc=codec::crc32cUpdate(recordsCrc,reinterpret_cast<const std::uint8_t*>(decoded.data()),rawBytes);
            recordsSeen+=block.recordCount; bodyBytes+=sizeof(block)+block.encodedBytes; ++blocksSeen;
            position=0u; count=block.recordCount;
        }
        copyBinaryMarketRecord(record,decoded[position++]); available=true; return true;
    }
};

[[nodiscard]] bool recordBefore(const BinaryMarketRecord& left,
                                 const BinaryMarketRecord& right) noexcept {
    const auto& l=left.header; const auto& r=right.header;
    if(l.receiveMonotonicNs!=r.receiveMonotonicNs) return l.receiveMonotonicNs<r.receiveMonotonicNs;
    if(l.shardIndex!=r.shardIndex) return l.shardIndex<r.shardIndex;
    if(l.shardSequence!=r.shardSequence) return l.shardSequence<r.shardSequence;
    if(l.receiveRealtimeNs!=r.receiveRealtimeNs) return l.receiveRealtimeNs<r.receiveRealtimeNs;
    if(l.frameSequence!=r.frameSequence) return l.frameSequence<r.frameSequence;
    if(l.eventOrdinal!=r.eventOrdinal) return l.eventOrdinal<r.eventOrdinal;
    return l.eventSequence<r.eventSequence;
}

}  // namespace

struct BinaryMarketCorpusCursorState final {
    struct Shard final {
        std::vector<SegmentKey> segments{};
        std::size_t position{0u};
        std::unique_ptr<SegmentDecoder> decoder{};
        BinaryMarketRecord head{};
        std::uint64_t previousSequence{0u}, previousMonotonic{0u};
        bool ready{false};
    };
    BinaryMarketSelectionRequest request{};
    BinaryMarketManifest manifest{};
    std::vector<BinaryMarketSource> sources{};
    std::vector<BinaryMarketSourceState> sourceStates{};
    std::vector<Shard> shards{};
    std::uint32_t sourceId{0u};
    std::vector<bool> includedSources{};
    bool failed{false};
};

namespace {
bool fillHead(BinaryMarketCorpusCursorState& state,
              BinaryMarketCorpusCursorState::Shard& shard, std::string& error) {
    shard.ready=false;
    for(;;) {
        if(!shard.decoder) {
            if(shard.position==shard.segments.size()) return true;
            shard.decoder=std::make_unique<SegmentDecoder>();
            if(!shard.decoder->open(state.request.root,shard.segments[shard.position++],state.manifest.producerEpoch,error)) return false;
        }
        BinaryMarketRecord record{}; bool available=false;
        if(!shard.decoder->next(record,available,state.sources,error)) return false;
        if(!available) {shard.decoder.reset();continue;}
        const auto& h=record.header;
        if(!advanceBinaryMarketSourceState(record,state.sourceStates[h.sourceId-1u])) {
            error="binary corpus changed membership/generation boundary";return false;
        }
        if((shard.previousSequence!=0u && h.shardSequence<=shard.previousSequence) ||
           (shard.previousMonotonic!=0u && h.receiveMonotonicNs<shard.previousMonotonic)) {
            error="binary corpus arrival order crosses shard segments"; return false;
        }
        shard.previousSequence=h.shardSequence; shard.previousMonotonic=h.receiveMonotonicNs;
        if(state.includedSources[h.sourceId] &&
           (h.channel==BinaryMarketChannel::SourceLifecycle || (state.request.channelMask&binaryMarketChannelBit(h.channel))!=0u) &&
           h.receiveRealtimeNs>=state.request.beginReceiveNs && h.receiveRealtimeNs<state.request.endReceiveNs) {
            if((h.flags&BinaryMarketRecordGapBoundary)!=0u ||
               (state.request.requireTraderReplayCompatibility &&
                !binaryMarketRecordIsSourceHealth(record) &&
                (h.flags&BinaryMarketRecordTraderReplayCompatible)==0u)) {
                error="selected corpus changed to missing or incompatible records";return false;
            }
            copyBinaryMarketRecord(shard.head,record);shard.ready=true;return true;
        }
    }
}
} // namespace

BinaryMarketCorpusCursor::BinaryMarketCorpusCursor() noexcept = default;
BinaryMarketCorpusCursor::~BinaryMarketCorpusCursor() noexcept = default;
BinaryMarketCorpusCursor::BinaryMarketCorpusCursor(BinaryMarketCorpusCursor&&) noexcept = default;
BinaryMarketCorpusCursor& BinaryMarketCorpusCursor::operator=(BinaryMarketCorpusCursor&&) noexcept = default;

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
                       sources, manifest.sourcesCrc32c, error, manifest.maximumBytes, manifest.sourceCount) ||
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

Status BinaryMarketCorpusCursor::open(
    const BinaryMarketSelectionRequest& request,
    BinaryMarketSelection& output,
    std::string& error) noexcept {
    state_.reset();
    output = {};
    error.clear();
    if (request.root.empty() ||
        (request.allSources ? (!request.exchange.empty() || !request.market.empty() || !request.canonicalSymbol.empty()) :
            (request.exchange.empty() || request.market.empty() || request.canonicalSymbol.empty())) ||
        (!request.allSources && !request.sourceIds.empty()) ||
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
        Status status = BinaryMarketCorpusReader{}.catalog(request.root, output.manifest, sources, error);
        if (!isOk(status)) return status;
        if (request.beginReceiveNs < output.manifest.startedReceiveNs ||
            request.endReceiveNs > output.manifest.finalizedReceiveNs) {
            error = "selected receive interval is outside sealed capture coverage";
            return Status::OutOfRange;
        }
        std::vector<bool> includedSources(sources.size()+1u,request.allSources && request.sourceIds.empty());
        includedSources[0]=false;
        if(request.sourceIds.size()>sources.size()) {error="oversized selected corpus source set";return Status::InvalidArgument;}
        for(const auto id:request.sourceIds) {
            if(id==0u || id>sources.size() || includedSources[id]) {error="invalid or duplicate selected corpus source ID";return Status::InvalidArgument;}
            includedSources[id]=true;
        }
        const BinaryMarketSource* selected = nullptr;
        for (const auto& source : sources) {
            if(request.allSources) break;
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
        if (!selected && !request.allSources) {
            error = "binary corpus source was not found";
            return Status::OutOfRange;
        }
        if(selected) {output.source = *selected;includedSources[selected->sourceId]=true;}

        std::vector<BinaryMarketIndexEntry> index;
        std::vector<BinaryMarketGap> gaps;
        if (!readTable(request.root / "index.bin", kBinaryMarketIndexMagic,
                       index, output.manifest.indexCrc32c, error, output.manifest.maximumBytes, output.manifest.indexCount) ||
            index.size() != output.manifest.indexCount ||
            !readTable(request.root / "gaps.bin", kBinaryMarketGapsMagic,
                       gaps, output.manifest.gapsCrc32c, error, output.manifest.maximumBytes, output.manifest.gapCount)) {
            return Status::CorruptData;
        }
        if (gaps.size() != output.manifest.gapCount) {
            error = "binary corpus gap count does not match manifest";
            return Status::CorruptData;
        }
        std::unordered_set<SegmentKey, SegmentKeyHash> allSegments;
        std::unordered_set<IndexIdentity, IndexIdentityHash> indexIdentities;
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
                !validBinaryMarketRecordChannel(entry.channel) ||
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
            const auto& indexedSource=sources[entry.sourceId-1u];
            if(entry.sourceGeneration<indexedSource.initialSourceGeneration) {
                error="binary corpus index predates source catalog generation";return Status::CorruptData;
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
        }
        if (indexedRecordCount != output.manifest.recordCount ||
            allSegments.size() != output.manifest.segmentCount) {
            error = "binary corpus index totals do not match manifest";
            return Status::CorruptData;
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
                !validBinaryMarketRecordChannel(gap.channel) || gap.reserved32 != 0u ||
                gap.reserved8 != 0u) {
                error = "binary corpus gap entry mismatch";
                return Status::CorruptData;
            }
            if(gap.sourceGeneration<sources[gap.sourceId-1u].initialSourceGeneration) {
                error="binary corpus gap predates source catalog generation";return Status::CorruptData;
            }
            const bool arrivalRangeUnavailable =
                gap.minimumDroppedReceiveNs == 0 &&
                gap.maximumDroppedReceiveNs == 0;
            if (gap.channel==BinaryMarketChannel::SourceLifecycle || (includedSources[gap.sourceId] &&
                (request.channelMask & binaryMarketChannelBit(gap.channel)) != 0u &&
                (arrivalRangeUnavailable ||
                 rangesOverlap(gap.minimumDroppedReceiveNs,
                               gap.maximumDroppedReceiveNs,
                               request.beginReceiveNs,
                               request.endReceiveNs)))) {
                output.gaps.push_back(gap);
            }
        }
        if (!output.gaps.empty()) {
            error = "selected binary corpus interval contains capture gaps";
            return Status::CorruptData;
        }
        auto state=std::make_unique<BinaryMarketCorpusCursorState>();
        state->request=request; state->manifest=output.manifest;state->sources=sources;
        for(const auto& source:sources) state->sourceStates.push_back(initialBinaryMarketSourceState(source));
        state->sourceId=selected?selected->sourceId:0u;state->includedSources=std::move(includedSources);state->shards.resize(output.manifest.shardCount);
        // Full corpus preflight: bounded blocks, no rows accumulated. Index ranges
        // and counts are checked against actual records rather than trusted for filtering.
        output.sourcePresentChannelMasks.resize(sources.size(),0u);
        output.sourceObservedChannelMasks.resize(sources.size(),0u);
        output.sourceLifecycleRevisionsAtBegin.resize(sources.size(),0u);
        output.sourceUnhealthyChannelMasksAtBegin.resize(sources.size(),0u);
        for(const auto& initial:state->sourceStates) output.sourcesAtBegin.push_back(effectiveBinaryMarketSource(initial));
        std::uint64_t generation=0u;
        std::uint64_t observedCorpusRecords=0u;
        std::uint64_t physicalBytes=sizeof(BinaryMarketManifest)+3u*sizeof(BinaryMarketTableHeader)+
            sources.size()*sizeof(BinaryMarketSource)+index.size()*sizeof(BinaryMarketIndexEntry)+gaps.size()*sizeof(BinaryMarketGap);
        for(std::uint32_t number=1u;number<=output.manifest.segmentCount;++number) {
            const SegmentKey key{number,segmentShards[number]};
            SegmentDecoder decoder;
            if(!decoder.open(request.root,key,output.manifest.producerEpoch,error)) return Status::CorruptData;
            const auto segmentBytes=sizeof(BinaryMarketSegmentHeader)+decoder.header.storedBlockBytes+sizeof(BinaryMarketSegmentFooter);
            if(physicalBytes>output.manifest.projectedBytes || segmentBytes>output.manifest.projectedBytes-physicalBytes) {
                error="binary corpus physical storage exceeds sealed quota";return Status::CorruptData;
            }
            physicalBytes+=segmentBytes;
            std::vector<BinaryMarketIndexEntry> observed;
            for(;;) {
                BinaryMarketRecord record{};bool available=false;
                if(!decoder.next(record,available,sources,error)) return Status::CorruptData;
                if(!available) break;
                ++observedCorpusRecords;
                const auto& h=record.header;
                if(!advanceBinaryMarketSourceState(record,state->sourceStates[h.sourceId-1u])) {
                    error="binary corpus membership/generation boundary mismatch";return Status::CorruptData;
                }
                if(h.receiveRealtimeNs<request.beginReceiveNs) {
                    output.sourcesAtBegin[h.sourceId-1u]=effectiveBinaryMarketSource(state->sourceStates[h.sourceId-1u]);
                    output.sourceLifecycleRevisionsAtBegin[h.sourceId-1u]=state->sourceStates[h.sourceId-1u].directoryRevision;
                    output.sourceUnhealthyChannelMasksAtBegin[h.sourceId-1u]=state->sourceStates[h.sourceId-1u].unhealthyMask;
                }
                auto& shard=state->shards[key.shard];
                if((shard.previousSequence!=0u && h.shardSequence<=shard.previousSequence) ||
                   (shard.previousMonotonic!=0u && h.receiveMonotonicNs<shard.previousMonotonic)) {
                    error="binary corpus arrival order crosses segments";return Status::CorruptData;
                }
                shard.previousSequence=h.shardSequence;shard.previousMonotonic=h.receiveMonotonicNs;
                auto found=std::find_if(observed.begin(),observed.end(),[&](const auto& e) {
                    return e.sourceId==h.sourceId && e.sourceGeneration==h.sourceGeneration && e.channel==h.channel;
                });
                if(found==observed.end()) {
                    observed.push_back({.segmentNumber=number,.sourceId=h.sourceId,.sourceGeneration=h.sourceGeneration,
                        .shardIndex=key.shard,.channel=h.channel,.compatibility=BinaryMarketCompatibility::ExactTraderReplay});
                    found=observed.end()-1;
                }
                ++found->recordCount;
                if(found->firstEventSequence==0u) found->firstEventSequence=h.eventSequence;
                found->lastEventSequence=h.eventSequence;
                if(found->firstReceiveNs==0 || h.receiveRealtimeNs<found->firstReceiveNs) found->firstReceiveNs=h.receiveRealtimeNs;
                found->lastReceiveNs=std::max(found->lastReceiveNs,h.receiveRealtimeNs);
                if((h.flags&BinaryMarketRecordTraderReplayCompatible)==0u) {
                    found->flags|=BinaryMarketIndexHasRecordedOnly;found->compatibility=BinaryMarketCompatibility::RecordedOnly;
                }
                if((h.flags&BinaryMarketRecordGapBoundary)!=0u) {found->flags|=BinaryMarketIndexHasGap;++found->gapCount;}
                if(!state->includedSources[h.sourceId] || (h.channel!=BinaryMarketChannel::SourceLifecycle && (request.channelMask&binaryMarketChannelBit(h.channel))==0u) ||
                   h.receiveRealtimeNs<request.beginReceiveNs || h.receiveRealtimeNs>=request.endReceiveNs) continue;
                if((h.flags&BinaryMarketRecordGapBoundary)!=0u) {
                    error="selected binary corpus interval contains capture loss";return Status::CorruptData;
                }
                const bool health=binaryMarketRecordIsSourceHealth(record);
                const bool compatible=(h.flags&BinaryMarketRecordTraderReplayCompatible)!=0u;
                if(h.channel==BinaryMarketChannel::SourceLifecycle) {
                    const auto& lifecycle=*binaryMarketPayload<BinaryMarketSourceLifecyclePayload>(&record);
                    if(lifecycle.cause!=BinaryMarketSourceTransitionCause::Subscription)
                        output.sourceObservedChannelMasks[h.sourceId-1u]|=binaryMarketChannelBit(
                            static_cast<BinaryMarketChannel>(lifecycle.affectedChannel));
                }
                if(health || compatible)
                    output.sourceObservedChannelMasks[h.sourceId-1u]|=binaryMarketChannelBit(h.channel);
                if(!health && compatible) {
                    output.presentChannelMask|=binaryMarketChannelBit(h.channel);
                    output.sourcePresentChannelMasks[h.sourceId-1u]|=binaryMarketChannelBit(h.channel);
                }
                if(selected && generation==0u) generation=h.sourceGeneration;
                if(request.requireTraderReplayCompatibility && !health && !compatible) {
                    error="selected binary corpus record is not trader-replay compatible";return Status::Unimplemented;
                }
            }
            const auto indexedForSegment=std::count_if(index.begin(),index.end(),[&](const auto& e){return e.segmentNumber==number;});
            if(static_cast<std::size_t>(indexedForSegment)!=observed.size()) {error="binary corpus index identity totals mismatch";return Status::CorruptData;}
            for(const auto& actual:observed) {
                const auto expected=std::find_if(index.begin(),index.end(),[&](const auto& e){
                    return e.segmentNumber==number && e.sourceId==actual.sourceId && e.sourceGeneration==actual.sourceGeneration && e.channel==actual.channel;
                });
                if(expected==index.end() || expected->sourceGeneration!=actual.sourceGeneration ||
                   expected->recordCount!=actual.recordCount || expected->gapCount!=actual.gapCount ||
                   expected->firstEventSequence!=actual.firstEventSequence || expected->lastEventSequence!=actual.lastEventSequence ||
                   expected->firstReceiveNs!=actual.firstReceiveNs || expected->lastReceiveNs!=actual.lastReceiveNs ||
                   expected->flags!=actual.flags || expected->compatibility!=actual.compatibility) {
                    error="binary corpus index disagrees with compressed records";return Status::CorruptData;
                }
            }
            state->shards[key.shard].segments.push_back(key);
        }
        if(physicalBytes>output.manifest.projectedBytes) {error="binary corpus physical metadata exceeds sealed quota";return Status::CorruptData;}
        if(observedCorpusRecords!=output.manifest.recordCount) {error="binary corpus observed record totals mismatch";return Status::CorruptData;}
        for(auto& shard:state->shards) {shard.previousSequence=0u;shard.previousMonotonic=0u;}
        for(std::size_t i=0u;i<sources.size();++i) state->sourceStates[i]=initialBinaryMarketSourceState(sources[i]);
        if ((request.requiredChannelMask & ~output.presentChannelMask) != 0u) {
            error = "selected binary corpus interval lacks required channel data";
            return Status::OutOfRange;
        }
        output.sourceGeneration = generation;
        for(auto& shard:state->shards)
            if(!fillHead(*state,shard,error)) return Status::CorruptData;
        state_=std::move(state);
        return Status::Ok;
    } catch (...) {
        error = "binary corpus selection failed";
        return Status::IoError;
    }
}

Status BinaryMarketCorpusCursor::next(BinaryMarketRecord& output,bool& available,std::string& error) noexcept {
    available=false;error.clear();
    if(!state_ || state_->failed) return Status::InvalidArgument;
    try {
        auto* selected=static_cast<BinaryMarketCorpusCursorState::Shard*>(nullptr);
        for(auto& shard:state_->shards)
            if(shard.ready && (!selected || recordBefore(shard.head,selected->head))) selected=&shard;
        if(!selected) return Status::Ok;
        copyBinaryMarketRecord(output,selected->head);
        if(!fillHead(*state_,*selected,error)) {state_->failed=true;return Status::CorruptData;}
        available=true;return Status::Ok;
    } catch(...) {state_->failed=true;error="binary corpus cursor read failed";return Status::IoError;}
}

Status BinaryMarketCorpusReader::select(const BinaryMarketSelectionRequest& request,
    BinaryMarketSelection& output,std::string& error) const noexcept {
    BinaryMarketCorpusCursor cursor;
    auto status=cursor.open(request,output,error);
    if(!isOk(status)) return status;
    try {
        for(;;) {
            BinaryMarketRecord record{};bool available=false;
            status=cursor.next(record,available,error);
            if(!isOk(status)) {output.records.clear();return status;}
            if(!available) return Status::Ok;
            output.records.push_back(record);
        }
    } catch(...) {output.records.clear();error="binary corpus selection allocation failed";return Status::IoError;}
}

}  // namespace hftrec::corpus
