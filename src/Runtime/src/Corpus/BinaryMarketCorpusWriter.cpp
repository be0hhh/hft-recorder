#include "BinaryMarketCorpusWriter.hpp"

#include "Codec/Crc32c.hpp"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <limits>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace hftrec::corpus {
namespace {

struct IndexKey final {
    std::uint32_t sourceId{0u};
    std::uint64_t sourceGeneration{0u};
    BinaryMarketChannel channel{BinaryMarketChannel::BookTicker};

    [[nodiscard]] bool operator==(const IndexKey&) const noexcept = default;
};

struct IndexKeyHash final {
    [[nodiscard]] std::size_t operator()(const IndexKey& value) const noexcept {
        std::uint64_t hash = value.sourceGeneration ^
            (static_cast<std::uint64_t>(value.sourceId) << 32u) ^
            static_cast<std::uint8_t>(value.channel);
        hash ^= hash >> 33u;
        hash *= 0xff51afd7ed558ccdull;
        hash ^= hash >> 33u;
        return static_cast<std::size_t>(hash);
    }
};

struct SegmentState final {
    std::ofstream stream{};
    std::filesystem::path partialPath{};
    std::filesystem::path finalPath{};
    BinaryMarketSegmentHeader header{};
    std::unordered_map<IndexKey, std::size_t, IndexKeyHash> indexPositions{};
    std::uint64_t projectedBytes{0u};
    bool open{false};
};

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

[[nodiscard]] bool addWithin(std::uint64_t current,
                             std::uint64_t amount,
                             std::uint64_t limit) noexcept {
    return amount <= std::numeric_limits<std::uint64_t>::max() - current &&
           current + amount <= limit;
}

[[nodiscard]] std::filesystem::path segmentPath(
    const std::filesystem::path& root,
    std::uint16_t shard,
    std::uint32_t segment,
    const char* suffix) {
    char name[96]{};
    (void)std::snprintf(name, sizeof(name),
                        "shard-%04u-segment-%06u.%s",
                        static_cast<unsigned>(shard),
                        static_cast<unsigned>(segment), suffix);
    return root / "segments" / name;
}

[[nodiscard]] bool writeBytes(std::ofstream& stream,
                              const void* data,
                              std::size_t bytes) noexcept {
    stream.write(static_cast<const char*>(data),
                 static_cast<std::streamsize>(bytes));
    return stream.good();
}

template <typename Record>
[[nodiscard]] bool writeTableAtomic(const std::filesystem::path& target,
                                    std::uint32_t magic,
                                    const std::vector<Record>& records,
                                    std::uint32_t* recordsCrc) noexcept {
    if (!recordsCrc ||
        records.size() > std::numeric_limits<std::uint32_t>::max()) return false;
    const std::filesystem::path temporary = target.string() + ".tmp";
    BinaryMarketTableHeader header{};
    header.magic = magic;
    header.headerBytes = sizeof(header);
    header.recordBytes = sizeof(Record);
    header.recordCount = static_cast<std::uint32_t>(records.size());
    const std::size_t bytes = records.size() * sizeof(Record);
    header.recordsCrc32c = crcBytes(records.data(), bytes);
    header.headerCrc32c =
        headerCrc(header, &BinaryMarketTableHeader::headerCrc32c);
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream.is_open() || !writeBytes(stream, &header, sizeof(header)) ||
        (bytes != 0u && !writeBytes(stream, records.data(), bytes))) {
        return false;
    }
    stream.flush();
    if (!stream.good()) return false;
    stream.close();
    std::error_code error;
    std::filesystem::rename(temporary, target, error);
    if (error) return false;
    *recordsCrc = header.recordsCrc32c;
    return true;
}

[[nodiscard]] bool writeManifestAtomic(
    const std::filesystem::path& target,
    BinaryMarketManifest manifest) noexcept {
    const std::filesystem::path temporary = target.string() + ".tmp";
    manifest.headerBytes = sizeof(manifest);
    manifest.manifestCrc32c = 0u;
    manifest.manifestCrc32c = crcBytes(&manifest, sizeof(manifest));
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream.is_open() || !writeBytes(stream, &manifest, sizeof(manifest)))
        return false;
    stream.flush();
    if (!stream.good()) return false;
    stream.close();
    std::error_code error;
    std::filesystem::rename(temporary, target, error);
    return !error;
}

}  // namespace

struct BinaryMarketCorpusWriterState final {
    BinaryMarketWriterConfig config{};
    BinaryMarketManifest manifest{};
    std::vector<BinaryMarketSource> sources{};
    std::vector<BinaryMarketIndexEntry> index{};
    std::vector<BinaryMarketGap> gaps{};
    std::vector<SegmentState> segments{};
    std::uint64_t projectedBytes{0u};
    std::uint64_t writeLimitBytes{0u};
    std::size_t maximumGapRecords{0u};
    std::uint32_t nextSegmentNumber{1u};
    bool active{false};
    bool quotaReached{false};
};

namespace {

[[nodiscard]] bool reserveBytes(BinaryMarketCorpusWriterState& state,
                                std::uint64_t bytes) noexcept {
    if (!addWithin(state.projectedBytes, bytes, state.writeLimitBytes)) {
        state.quotaReached = true;
        return false;
    }
    state.projectedBytes += bytes;
    return true;
}

[[nodiscard]] bool openSegment(BinaryMarketCorpusWriterState& state,
                               std::uint16_t shardIndex) noexcept {
    if (shardIndex >= state.segments.size()) return false;
    auto& segment = state.segments[shardIndex];
    if (segment.open) return true;
    if (state.nextSegmentNumber ==
        std::numeric_limits<std::uint32_t>::max()) {
        return false;
    }
    segment = {};
    segment.header.headerBytes = sizeof(BinaryMarketSegmentHeader);
    segment.header.recordBytes = sizeof(BinaryMarketRecord);
    segment.header.segmentNumber = state.nextSegmentNumber++;
    segment.header.producerEpoch = state.config.producerEpoch;
    segment.header.shardIndex = shardIndex;
    segment.partialPath = segmentPath(state.config.root, shardIndex,
                                      segment.header.segmentNumber, "partial");
    segment.finalPath = segmentPath(state.config.root, shardIndex,
                                    segment.header.segmentNumber, "cxm");
    segment.stream.open(segment.partialPath,
                        std::ios::binary | std::ios::trunc);
    if (!segment.stream.is_open() ||
        !writeBytes(segment.stream, &segment.header,
                    sizeof(segment.header))) return false;
    segment.projectedBytes = sizeof(BinaryMarketSegmentHeader) +
        sizeof(BinaryMarketSegmentFooter);
    segment.open = true;
    return true;
}

[[nodiscard]] bool sealSegment(BinaryMarketCorpusWriterState& state,
                               std::uint16_t shardIndex) noexcept {
    if (shardIndex >= state.segments.size()) return false;
    auto& segment = state.segments[shardIndex];
    if (!segment.open) return true;
    segment.header.headerCrc32c = headerCrc(
        segment.header, &BinaryMarketSegmentHeader::headerCrc32c);
    segment.stream.seekp(0, std::ios::beg);
    if (!writeBytes(segment.stream, &segment.header, sizeof(segment.header)))
        return false;
    segment.stream.seekp(0, std::ios::end);
    BinaryMarketSegmentFooter footer{};
    footer.footerBytes = sizeof(footer);
    footer.segmentNumber = segment.header.segmentNumber;
    footer.recordsCrc32c = segment.header.recordsCrc32c;
    footer.recordCount = segment.header.recordCount;
    footer.firstReceiveNs = segment.header.firstReceiveNs;
    footer.lastReceiveNs = segment.header.lastReceiveNs;
    footer.footerCrc32c = headerCrc(
        footer, &BinaryMarketSegmentFooter::footerCrc32c);
    if (!writeBytes(segment.stream, &footer, sizeof(footer))) return false;
    segment.stream.flush();
    if (!segment.stream.good()) return false;
    segment.stream.close();
    std::error_code error;
    std::filesystem::rename(segment.partialPath, segment.finalPath, error);
    if (error) return false;
    ++state.manifest.segmentCount;
    segment.open = false;
    segment.indexPositions.clear();
    return true;
}

[[nodiscard]] BinaryMarketIndexEntry* ensureIndex(
    BinaryMarketCorpusWriterState& state,
    SegmentState& segment,
    std::uint16_t shardIndex,
    std::uint32_t sourceId,
    std::uint64_t sourceGeneration,
    BinaryMarketChannel channel) noexcept {
    const IndexKey key{sourceId, sourceGeneration, channel};
    const auto found = segment.indexPositions.find(key);
    if (found != segment.indexPositions.end())
        return state.index.data() + found->second;
    const std::size_t position = state.index.size();
    try {
        state.index.push_back({
            .segmentNumber = segment.header.segmentNumber,
            .sourceId = sourceId,
            .sourceGeneration = sourceGeneration,
            .shardIndex = shardIndex,
            .channel = channel,
            .compatibility = BinaryMarketCompatibility::ExactTraderReplay});
        segment.indexPositions.emplace(key, position);
    } catch (...) {
        return nullptr;
    }
    return state.index.data() + position;
}

}  // namespace

BinaryMarketCorpusWriter::BinaryMarketCorpusWriter() noexcept = default;

BinaryMarketCorpusWriter::~BinaryMarketCorpusWriter() noexcept {
    if (!state_) return;
    for (auto& segment : state_->segments) {
        if (segment.stream.is_open()) segment.stream.close();
    }
}

Status BinaryMarketCorpusWriter::start(
    const BinaryMarketWriterConfig& config) noexcept {
    if (state_ && state_->active) return Status::InvalidArgument;
    if (config.root.empty() || config.sources.empty() ||
        config.producerEpoch == 0u || config.maximumBytes == 0u ||
        config.shardCount == 0u || config.ringCapacity == 0u ||
        config.startedReceiveNs <= 0 || config.startedMonotonicNs == 0u ||
        config.targetDurationNs == 0u ||
        config.segmentTargetBytes < sizeof(BinaryMarketSegmentHeader) +
            sizeof(BinaryMarketRecord) + sizeof(BinaryMarketSegmentFooter) ||
        config.sources.size() > std::numeric_limits<std::uint32_t>::max()) {
        return Status::InvalidArgument;
    }
    for (std::size_t index = 0u; index < config.sources.size(); ++index)
        if (!validBinaryMarketSource(config.sources[index], index))
            return Status::InvalidArgument;
    try {
        auto state = std::make_unique<BinaryMarketCorpusWriterState>();
        state->config = config;
        state->config.sources = {};
        state->sources.assign(config.sources.begin(), config.sources.end());
        state->segments.resize(config.shardCount);
        state->manifest.headerBytes = sizeof(BinaryMarketManifest);
        state->manifest.writerAbiFingerprint =
            binaryMarketCorpusAbiFingerprint();
        state->manifest.producerEpoch = config.producerEpoch;
        state->manifest.startedReceiveNs = config.startedReceiveNs;
        state->manifest.replayAnchorReceiveNs = config.startedReceiveNs;
        state->manifest.replayAnchorMonotonicNs = config.startedMonotonicNs;
        state->manifest.maximumBytes = config.maximumBytes;
        state->manifest.segmentTargetBytes = config.segmentTargetBytes;
        state->manifest.sourceCount =
            static_cast<std::uint32_t>(state->sources.size());
        state->manifest.shardCount = config.shardCount;
        const std::uint64_t fixedMetadata =
            2u * sizeof(BinaryMarketManifest) +
            3u * sizeof(BinaryMarketTableHeader) +
            state->sources.size() * sizeof(BinaryMarketSource) +
            state->sources.size() * kBinaryMarketChannelCount *
                sizeof(BinaryMarketGap);
        const std::uint64_t maximumPendingRecords =
            static_cast<std::uint64_t>(config.shardCount) *
            static_cast<std::uint64_t>(config.ringCapacity);
        constexpr std::uint64_t kWorstPersistedBytesPerPendingRecord =
            sizeof(BinaryMarketRecord) + sizeof(BinaryMarketIndexEntry) +
            sizeof(BinaryMarketSegmentHeader) +
            sizeof(BinaryMarketSegmentFooter);
        if (maximumPendingRecords >
                std::numeric_limits<std::uint64_t>::max() /
                    kWorstPersistedBytesPerPendingRecord) {
            return Status::OutOfRange;
        }
        const std::uint64_t finalDrainReserve =
            maximumPendingRecords * kWorstPersistedBytesPerPendingRecord;
        if (!addWithin(0u, fixedMetadata, config.maximumBytes) ||
            finalDrainReserve > config.maximumBytes - fixedMetadata)
            return Status::OutOfRange;
        state->projectedBytes = fixedMetadata;
        state->writeLimitBytes = config.maximumBytes - finalDrainReserve;
        state->maximumGapRecords =
            state->sources.size() * kBinaryMarketChannelCount;

        std::error_code error;
        if (std::filesystem::exists(config.root, error)) {
            if (error || !std::filesystem::is_directory(config.root, error) ||
                error || !std::filesystem::is_empty(config.root, error) ||
                error) return Status::InvalidArgument;
        } else if (!std::filesystem::create_directories(config.root, error) ||
                   error) {
            return Status::IoError;
        }
        if (!std::filesystem::create_directories(config.root / "segments",
                                                  error) || error) {
            return Status::IoError;
        }
        BinaryMarketManifest pending = state->manifest;
        pending.stopReason = BinaryMarketStopReason::Error;
        if (!writeManifestAtomic(config.root / "manifest.pending", pending))
            return Status::IoError;
        state->active = true;
        state_ = std::move(state);
        return Status::Ok;
    } catch (...) {
        return Status::IoError;
    }
}

Status BinaryMarketCorpusWriter::append(
    const BinaryMarketRecord& record) noexcept {
    if (!state_ || !state_->active) return Status::InvalidArgument;
    if (!validBinaryMarketRecord(record) ||
        record.header.shardIndex >= state_->segments.size() ||
        record.header.sourceId > state_->sources.size()) {
        return Status::InvalidArgument;
    }
    const auto& source = state_->sources[record.header.sourceId - 1u];
    const std::uint16_t channelBit =
        binaryMarketChannelBit(record.header.channel);
    const auto compatibility = source.compatibility[
        binaryMarketChannelIndex(record.header.channel)];
    const bool replayCompatible =
        (record.header.flags &
         BinaryMarketRecordTraderReplayCompatible) != 0u;
    if (record.header.sourceGeneration != source.initialSourceGeneration ||
        (source.availableChannelMask & channelBit) == 0u ||
        compatibility == BinaryMarketCompatibility::Unavailable ||
        (replayCompatible &&
         compatibility != BinaryMarketCompatibility::ExactTraderReplay)) {
        return Status::InvalidArgument;
    }
    try {
        const std::uint16_t shardIndex = record.header.shardIndex;
        auto* segment = &state_->segments[shardIndex];
        const bool rotate = segment->open &&
            segment->header.recordCount != 0u &&
            (segment->projectedBytes > state_->config.segmentTargetBytes ||
             sizeof(BinaryMarketRecord) >
                 state_->config.segmentTargetBytes -
                     segment->projectedBytes);
        const bool openNeeded = !segment->open || rotate;
        const IndexKey key{record.header.sourceId,
                           record.header.sourceGeneration,
                           record.header.channel};
        const bool indexNeeded = openNeeded ||
            segment->indexPositions.find(key) ==
                segment->indexPositions.end();
        std::uint64_t requiredBytes = sizeof(BinaryMarketRecord);
        if (openNeeded) {
            constexpr std::uint64_t fixedSegmentBytes =
                sizeof(BinaryMarketSegmentHeader) +
                sizeof(BinaryMarketSegmentFooter);
            if (requiredBytes >
                std::numeric_limits<std::uint64_t>::max() -
                    fixedSegmentBytes) return Status::OutOfRange;
            requiredBytes += fixedSegmentBytes;
            if (state_->nextSegmentNumber ==
                std::numeric_limits<std::uint32_t>::max()) {
                return Status::OutOfRange;
            }
        }
        if (indexNeeded) {
            if (requiredBytes >
                std::numeric_limits<std::uint64_t>::max() -
                    sizeof(BinaryMarketIndexEntry)) {
                return Status::OutOfRange;
            }
            requiredBytes += sizeof(BinaryMarketIndexEntry);
        }
        if (!reserveBytes(*state_, requiredBytes)) return Status::OutOfRange;
        if (rotate && !sealSegment(*state_, shardIndex))
            return Status::IoError;
        if (openNeeded) {
            if (!openSegment(*state_, shardIndex)) return Status::IoError;
            segment = &state_->segments[shardIndex];
        }
        BinaryMarketIndexEntry* index = ensureIndex(
            *state_, *segment, shardIndex, record.header.sourceId,
            record.header.sourceGeneration, record.header.channel);
        if (!index) return state_->quotaReached
            ? Status::OutOfRange : Status::IoError;
        if (!writeBytes(segment->stream, &record, sizeof(record)))
            return Status::IoError;
        segment->header.recordsCrc32c = codec::crc32cUpdate(
            segment->header.recordsCrc32c,
            reinterpret_cast<const std::uint8_t*>(&record), sizeof(record));
        ++segment->header.recordCount;
        segment->projectedBytes += sizeof(record);
        if (segment->header.firstReceiveNs == 0 ||
            record.header.receiveRealtimeNs <
                segment->header.firstReceiveNs)
            segment->header.firstReceiveNs = record.header.receiveRealtimeNs;
        if (record.header.receiveRealtimeNs > segment->header.lastReceiveNs)
            segment->header.lastReceiveNs = record.header.receiveRealtimeNs;

        ++index->recordCount;
        if (index->firstEventSequence == 0u)
            index->firstEventSequence = record.header.eventSequence;
        index->lastEventSequence = record.header.eventSequence;
        if (index->firstReceiveNs == 0 ||
            record.header.receiveRealtimeNs < index->firstReceiveNs)
            index->firstReceiveNs = record.header.receiveRealtimeNs;
        if (record.header.receiveRealtimeNs > index->lastReceiveNs)
            index->lastReceiveNs = record.header.receiveRealtimeNs;
        if ((record.header.flags &
             BinaryMarketRecordTraderReplayCompatible) == 0u) {
            index->flags |= BinaryMarketIndexHasRecordedOnly;
            index->compatibility = BinaryMarketCompatibility::RecordedOnly;
        }
        if ((record.header.flags & BinaryMarketRecordGapBoundary) != 0u) {
            index->flags |= BinaryMarketIndexHasGap;
            ++index->gapCount;
        }
        ++state_->manifest.recordCount;
        if (record.header.receiveMonotonicNs <
            state_->manifest.replayAnchorMonotonicNs) {
            state_->manifest.replayAnchorMonotonicNs =
                record.header.receiveMonotonicNs;
            state_->manifest.replayAnchorReceiveNs =
                record.header.receiveRealtimeNs;
        }
        if (state_->manifest.firstReceiveNs == 0 ||
            record.header.receiveRealtimeNs <
                state_->manifest.firstReceiveNs)
            state_->manifest.firstReceiveNs = record.header.receiveRealtimeNs;
        if (record.header.receiveRealtimeNs >
            state_->manifest.lastReceiveNs) {
            state_->manifest.lastReceiveNs = record.header.receiveRealtimeNs;
        }
        return Status::Ok;
    } catch (...) {
        return Status::IoError;
    }
}

Status BinaryMarketCorpusWriter::beginFinalDrain() noexcept {
    if (!state_ || !state_->active) return Status::InvalidArgument;
    state_->writeLimitBytes = state_->config.maximumBytes;
    return Status::Ok;
}

Status BinaryMarketCorpusWriter::appendGap(
    const BinaryMarketGap& gap) noexcept {
    if (!state_ || !state_->active || gap.sourceId == 0u ||
        gap.sourceId > state_->sources.size() ||
        gap.sourceGeneration == 0u || gap.gapEpoch == 0u ||
        gap.droppedRecords == 0u || gap.gapEpoch != gap.droppedRecords ||
        gap.firstDroppedEventSequence == 0u ||
        gap.lastDroppedEventSequence == 0u ||
        gap.observedReceiveNs <= 0 ||
        !((gap.minimumDroppedReceiveNs > 0 &&
           gap.maximumDroppedReceiveNs >= gap.minimumDroppedReceiveNs) ||
          (gap.minimumDroppedReceiveNs == 0 &&
           gap.maximumDroppedReceiveNs == 0)) ||
        gap.shardIndex >= state_->segments.size() ||
        !validBinaryMarketChannel(gap.channel) || gap.reserved32 != 0u ||
        gap.reserved8 != 0u) return Status::InvalidArgument;
    if (gap.sourceGeneration !=
            state_->sources[gap.sourceId - 1u].initialSourceGeneration ||
        (state_->sources[gap.sourceId - 1u].availableChannelMask &
         binaryMarketChannelBit(gap.channel)) == 0u) {
        return Status::InvalidArgument;
    }
    try {
        if (state_->gaps.size() >= state_->maximumGapRecords)
            return Status::OutOfRange;
        state_->gaps.push_back(gap);
        ++state_->manifest.gapCount;
        return Status::Ok;
    } catch (...) {
        return Status::IoError;
    }
}

Status BinaryMarketCorpusWriter::finalize(
    BinaryMarketStopReason reason,
    std::int64_t finalizedReceiveNs) noexcept {
    if (!state_ || !state_->active || finalizedReceiveNs <= 0 ||
        reason < BinaryMarketStopReason::Requested ||
        reason > BinaryMarketStopReason::Error)
        return Status::InvalidArgument;
    try {
        for (std::uint16_t shard = 0u; shard < state_->segments.size(); ++shard)
            if (!sealSegment(*state_, shard)) return Status::IoError;
        std::uint32_t sourcesCrc = 0u;
        std::uint32_t indexCrc = 0u;
        std::uint32_t gapsCrc = 0u;
        if (!writeTableAtomic(state_->config.root / "sources.bin",
                              kBinaryMarketSourcesMagic, state_->sources,
                              &sourcesCrc) ||
            !writeTableAtomic(state_->config.root / "index.bin",
                              kBinaryMarketIndexMagic, state_->index,
                              &indexCrc) ||
            !writeTableAtomic(state_->config.root / "gaps.bin",
                              kBinaryMarketGapsMagic, state_->gaps,
                              &gapsCrc)) return Status::IoError;
        state_->manifest.startedReceiveNs =
            state_->manifest.firstReceiveNs > 0
                ? std::min(state_->config.startedReceiveNs,
                           state_->manifest.firstReceiveNs)
                : state_->config.startedReceiveNs;
        state_->manifest.finalizedReceiveNs =
            std::max({finalizedReceiveNs, state_->config.startedReceiveNs,
                      state_->manifest.lastReceiveNs});
        state_->manifest.stopReason = reason;
        state_->manifest.complete =
            reason == BinaryMarketStopReason::Error ? 0u : 1u;
        state_->manifest.indexCount =
            static_cast<std::uint32_t>(state_->index.size());
        state_->manifest.sourcesCrc32c = sourcesCrc;
        state_->manifest.indexCrc32c = indexCrc;
        state_->manifest.gapsCrc32c = gapsCrc;
        state_->manifest.projectedBytes = state_->projectedBytes;
        if (!writeManifestAtomic(state_->config.root / "manifest.bin",
                                 state_->manifest)) return Status::IoError;
        std::error_code error;
        (void)std::filesystem::remove(state_->config.root / "manifest.pending",
                                      error);
        state_->active = false;
        return Status::Ok;
    } catch (...) {
        return Status::IoError;
    }
}

BinaryMarketWriterSnapshot BinaryMarketCorpusWriter::snapshot() const noexcept {
    BinaryMarketWriterSnapshot result{};
    if (!state_) return result;
    result.active = state_->active;
    result.quotaReached = state_->quotaReached;
    result.recordCount = state_->manifest.recordCount;
    result.gapCount = state_->manifest.gapCount;
    result.projectedBytes = state_->projectedBytes;
    result.segmentCount = state_->manifest.segmentCount;
    result.firstReceiveNs = state_->manifest.firstReceiveNs;
    result.lastReceiveNs = state_->manifest.lastReceiveNs;
    return result;
}

}  // namespace hftrec::corpus
