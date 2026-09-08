#pragma once


#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace hftrec::corpus {

inline constexpr std::uint16_t kBinaryMarketCorpusSchemaVersion = 3u;
inline constexpr std::uint16_t kBinaryMarketRecordSchemaVersion = 2u;
inline constexpr std::size_t kBinaryMarketChannelCount = 8u;
inline constexpr std::size_t kBinaryMarketVenueBytes = 16u;
inline constexpr std::size_t kBinaryMarketMarketBytes = 16u;
inline constexpr std::size_t kBinaryMarketSymbolBytes = 48u;
inline constexpr std::size_t kBinaryMarketPayloadBytes = 832u;
inline constexpr std::size_t kBinaryMarketDepthLevelsPerRecord = 32u;
inline constexpr std::uint8_t kBinaryMarketFundingRateScaleDigits = 8u;

inline constexpr std::uint32_t kBinaryMarketManifestMagic = 0x4d435248u;
inline constexpr std::uint32_t kBinaryMarketSourcesMagic = 0x53435248u;
inline constexpr std::uint32_t kBinaryMarketIndexMagic = 0x49435248u;
inline constexpr std::uint32_t kBinaryMarketGapsMagic = 0x47435248u;
inline constexpr std::uint32_t kBinaryMarketSegmentMagic = 0x53475248u;
inline constexpr std::uint32_t kBinaryMarketSegmentFooterMagic = 0x46475248u;

enum class BinaryMarketChannel : std::uint8_t {
    BookTicker = 1u,
    Trade = 2u,
    Depth = 3u,
    Liquidation = 4u,
    MarkPrice = 5u,
    IndexPrice = 6u,
    Funding = 7u,
    PriceLimit = 8u,
};

[[nodiscard]] inline constexpr bool validBinaryMarketChannel(
    BinaryMarketChannel channel) noexcept {
    return channel >= BinaryMarketChannel::BookTicker &&
           channel <= BinaryMarketChannel::PriceLimit;
}

[[nodiscard]] inline constexpr std::size_t binaryMarketChannelIndex(
    BinaryMarketChannel channel) noexcept {
    return static_cast<std::size_t>(channel) - 1u;
}

[[nodiscard]] inline constexpr std::uint16_t binaryMarketChannelBit(
    BinaryMarketChannel channel) noexcept {
    return validBinaryMarketChannel(channel)
        ? static_cast<std::uint16_t>(1u << binaryMarketChannelIndex(channel))
        : 0u;
}

enum class BinaryMarketCompatibility : std::uint8_t {
    Unavailable = 0u,
    ExactTraderReplay = 1u,
    RecordedOnly = 2u,
};

enum BinaryMarketInstrumentMetadataFlags : std::uint32_t {
    BinaryMarketInstrumentStepSize = 1u << 0u,
    BinaryMarketInstrumentContractBaseQty = 1u << 1u,
};

inline constexpr std::uint32_t kBinaryMarketInstrumentMetadataFlags =
    BinaryMarketInstrumentStepSize |
    BinaryMarketInstrumentContractBaseQty;

enum BinaryMarketDirectoryFlags : std::uint16_t {
    BinaryMarketDirectoryBookTicker = 1u << 0u,
    BinaryMarketDirectoryTrade = 1u << 1u,
    BinaryMarketDirectoryTickSize = 1u << 2u,
    BinaryMarketDirectoryDepth = 1u << 3u,
    BinaryMarketDirectoryBboSpread = 1u << 4u,
    BinaryMarketDirectoryRanking = 1u << 5u,
    BinaryMarketDirectoryFundingEligible = 1u << 6u,
    BinaryMarketDirectoryDelivery = 1u << 7u,
    BinaryMarketDirectoryMarginEligibilityKnown = 1u << 8u,
    BinaryMarketDirectoryMarginEligible = 1u << 9u,
    BinaryMarketDirectorySpotPostOnly = 1u << 10u,
};

inline constexpr std::uint16_t kBinaryMarketKnownDirectoryFlags =
    BinaryMarketDirectoryBookTicker | BinaryMarketDirectoryTrade |
    BinaryMarketDirectoryTickSize | BinaryMarketDirectoryDepth |
    BinaryMarketDirectoryBboSpread | BinaryMarketDirectoryRanking |
    BinaryMarketDirectoryFundingEligible | BinaryMarketDirectoryDelivery |
    BinaryMarketDirectoryMarginEligibilityKnown |
    BinaryMarketDirectoryMarginEligible |
    BinaryMarketDirectorySpotPostOnly;

enum BinaryMarketRecordFlags : std::uint32_t {
    BinaryMarketRecordNone = 0u,
    BinaryMarketRecordTraderReplayCompatible = 1u << 0u,
    BinaryMarketRecordBboFreshnessOnly = 1u << 1u,
    BinaryMarketRecordDepthSnapshot = 1u << 2u,
    BinaryMarketRecordDepthDelta = 1u << 3u,
    BinaryMarketRecordDepthPartial = 1u << 4u,
    BinaryMarketRecordDepthRebase = 1u << 5u,
    BinaryMarketRecordExchangeTimestampMissing = 1u << 6u,
    BinaryMarketRecordGapBoundary = 1u << 7u,
    BinaryMarketRecordRealtimeRegression = 1u << 8u,
    BinaryMarketRecordMonotonicNonIncreasing = 1u << 9u,
    BinaryMarketRecordExchangeAheadOfReceive = 1u << 10u,
    BinaryMarketRecordDepthState = 1u << 11u,
};

inline constexpr std::uint32_t kBinaryMarketKnownRecordFlags =
    BinaryMarketRecordTraderReplayCompatible |
    BinaryMarketRecordBboFreshnessOnly |
    BinaryMarketRecordDepthSnapshot |
    BinaryMarketRecordDepthDelta |
    BinaryMarketRecordDepthPartial |
    BinaryMarketRecordDepthRebase |
    BinaryMarketRecordExchangeTimestampMissing |
    BinaryMarketRecordGapBoundary |
    BinaryMarketRecordRealtimeRegression |
    BinaryMarketRecordMonotonicNonIncreasing |
    BinaryMarketRecordExchangeAheadOfReceive |
    BinaryMarketRecordDepthState;

enum BinaryMarketSourceFlags : std::uint16_t {
    BinaryMarketSourceHealthy = 0u,
    BinaryMarketSourceStale = 1u << 0u,
    BinaryMarketSourceDegraded = 1u << 1u,
    BinaryMarketSourceSequenceGap = 1u << 2u,
    BinaryMarketSourceExchangeTimestampMissing = 1u << 3u,
    BinaryMarketSourceBookTickerBidPresent = 1u << 4u,
    BinaryMarketSourceBookTickerAskPresent = 1u << 5u,
};

inline constexpr std::uint16_t kBinaryMarketKnownSourceFlags =
    BinaryMarketSourceStale | BinaryMarketSourceDegraded |
    BinaryMarketSourceSequenceGap |
    BinaryMarketSourceExchangeTimestampMissing |
    BinaryMarketSourceBookTickerBidPresent |
    BinaryMarketSourceBookTickerAskPresent;

struct BinaryMarketSource final {
    std::uint32_t sourceId{0u};
    std::uint32_t canonicalSymbolId{0u};
    std::uint64_t initialSourceGeneration{0u};
    std::int64_t tickSizeRaw{0};
    std::int64_t stepSizeRaw{0};
    std::int64_t contractBaseQtyRaw{0};
    std::int64_t priceBasisQtyRaw{0};
    std::uint32_t economicBaseAssetId{0u};
    std::uint32_t quoteAssetId{0u};
    std::uint16_t venueId{0u};
    std::uint16_t directoryFlags{0u};
    std::uint16_t configuredChannelMask{0u};
    std::uint16_t availableChannelMask{0u};
    std::uint16_t traderReplayChannelMask{0u};
    std::uint32_t instrumentMetadataFlags{0u};
    std::uint8_t marketRaw{0u};
    std::uint8_t marketKindRaw{0u};
    std::uint8_t assetDomainRaw{0u};
    std::uint8_t priceScale{0u};
    std::uint8_t quantityScale{0u};
    std::uint8_t venueBytes{0u};
    std::uint8_t marketBytes{0u};
    std::uint8_t canonicalSymbolBytes{0u};
    std::uint8_t nativeSymbolBytes{0u};
    std::array<BinaryMarketCompatibility, kBinaryMarketChannelCount>
        compatibility{};
    std::array<std::byte, 6u> reserved{};
    std::array<char, kBinaryMarketVenueBytes> venue{};
    std::array<char, kBinaryMarketMarketBytes> market{};
    std::array<char, kBinaryMarketSymbolBytes> canonicalSymbol{};
    std::array<char, kBinaryMarketSymbolBytes> nativeSymbol{};
};

[[nodiscard]] inline constexpr bool validBinaryMarketCompatibility(
    BinaryMarketCompatibility value) noexcept {
    return value == BinaryMarketCompatibility::Unavailable ||
        value == BinaryMarketCompatibility::ExactTraderReplay ||
        value == BinaryMarketCompatibility::RecordedOnly;
}

template <std::size_t Capacity>
[[nodiscard]] inline constexpr bool validBinaryMarketText(
    const std::array<char, Capacity>& value,
    std::uint8_t bytes) noexcept {
    if (bytes == 0u || bytes > Capacity) return false;
    for (std::size_t index = static_cast<std::size_t>(bytes);
         index < Capacity; ++index) {
        if (value[index] != '\0') return false;
    }
    return true;
}

[[nodiscard]] inline constexpr bool validBinaryMarketCode(
    std::uint8_t marketRaw,
    const std::array<char, kBinaryMarketMarketBytes>& market,
    std::uint8_t bytes) noexcept {
    if (!validBinaryMarketText(market, bytes)) return false;
    const std::string_view value{market.data(), bytes};
    switch (marketRaw) {
        case 1u: return value == "futures";
        case 2u: return value == "spot";
        case 3u: return value == "margin";
        case 4u: return value == "option";
        case 5u: return value == "inverse";
        case 6u: return value == "swap";
        default: return false;
    }
}

[[nodiscard]] inline constexpr bool validBinaryMarketSource(
    const BinaryMarketSource& source,
    std::size_t index) noexcept {
    if (source.sourceId != index + 1u ||
        source.canonicalSymbolId == 0u ||
        source.initialSourceGeneration == 0u || source.venueId == 0u ||
        (source.marketKindRaw != 1u && source.marketKindRaw != 2u) ||
        source.assetDomainRaw > 2u ||
        source.priceScale > 18u || source.quantityScale > 18u ||
        !validBinaryMarketText(source.venue, source.venueBytes) ||
        !validBinaryMarketCode(source.marketRaw, source.market,
                               source.marketBytes) ||
        !validBinaryMarketText(source.canonicalSymbol,
                               source.canonicalSymbolBytes) ||
        !validBinaryMarketText(source.nativeSymbol,
                               source.nativeSymbolBytes) ||
        source.tickSizeRaw < 0 || source.stepSizeRaw < 0 ||
        source.contractBaseQtyRaw < 0 || source.priceBasisQtyRaw < 0 ||
        source.economicBaseAssetId == 0u || source.quoteAssetId == 0u ||
        (source.directoryFlags & ~kBinaryMarketKnownDirectoryFlags) != 0u ||
        (((source.directoryFlags & BinaryMarketDirectoryTickSize) != 0u) !=
         (source.tickSizeRaw > 0)) ||
        ((source.directoryFlags & BinaryMarketDirectoryMarginEligible) != 0u &&
         (source.directoryFlags &
          BinaryMarketDirectoryMarginEligibilityKnown) == 0u) ||
        (source.instrumentMetadataFlags &
         ~kBinaryMarketInstrumentMetadataFlags) != 0u ||
        (((source.instrumentMetadataFlags &
           BinaryMarketInstrumentStepSize) != 0u) !=
         (source.stepSizeRaw > 0)) ||
        (((source.instrumentMetadataFlags &
           BinaryMarketInstrumentContractBaseQty) != 0u) !=
         (source.contractBaseQtyRaw > 0)) ||
        (source.configuredChannelMask & ~std::uint16_t{0x00ffu}) != 0u ||
        (source.availableChannelMask & ~source.configuredChannelMask) != 0u ||
        (source.traderReplayChannelMask &
         ~source.availableChannelMask) != 0u ||
        source.reserved != decltype(source.reserved){}) {
        return false;
    }
    const std::uint16_t directoryChannels =
        ((source.directoryFlags & BinaryMarketDirectoryBookTicker) != 0u
             ? binaryMarketChannelBit(BinaryMarketChannel::BookTicker)
             : 0u) |
        ((source.directoryFlags & BinaryMarketDirectoryTrade) != 0u
             ? binaryMarketChannelBit(BinaryMarketChannel::Trade)
             : 0u) |
        ((source.directoryFlags & BinaryMarketDirectoryDepth) != 0u
             ? binaryMarketChannelBit(BinaryMarketChannel::Depth)
             : 0u);
    constexpr std::uint16_t directoryOwnedChannelMask =
        binaryMarketChannelBit(BinaryMarketChannel::BookTicker) |
        binaryMarketChannelBit(BinaryMarketChannel::Trade) |
        binaryMarketChannelBit(BinaryMarketChannel::Depth);
    if ((source.configuredChannelMask & directoryOwnedChannelMask) !=
        directoryChannels) return false;
    for (std::size_t channel = 0u;
         channel < kBinaryMarketChannelCount; ++channel) {
        const std::uint16_t bit = std::uint16_t{1u} << channel;
        const auto compatibility = source.compatibility[channel];
        if (!validBinaryMarketCompatibility(compatibility)) return false;
        if ((source.availableChannelMask & bit) == 0u) {
            if (compatibility != BinaryMarketCompatibility::Unavailable)
                return false;
        } else if ((source.traderReplayChannelMask & bit) != 0u) {
            if (compatibility !=
                BinaryMarketCompatibility::ExactTraderReplay) return false;
        } else if (compatibility != BinaryMarketCompatibility::RecordedOnly) {
            return false;
        }
    }
    return true;
}

struct BinaryMarketRecordHeader final {
    std::uint32_t sourceId{0u};
    std::uint32_t flags{BinaryMarketRecordNone};
    std::uint64_t sourceGeneration{0u};
    std::uint64_t sessionEpoch{0u};
    std::uint64_t eventSequence{0u};
    std::uint64_t frameSequence{0u};
    std::uint64_t nativeIdentityFirst{0u};
    std::uint64_t nativeIdentitySecond{0u};
    std::int64_t exchangeTimestampNs{0};
    std::int64_t receiveRealtimeNs{0};
    std::uint64_t receiveMonotonicNs{0u};
    std::uint64_t shardSequence{0u};
    std::uint16_t payloadBytes{0u};
    std::uint16_t shardIndex{0u};
    std::uint16_t sourceFlags{0u};
    std::uint16_t schemaVersion{kBinaryMarketRecordSchemaVersion};
    BinaryMarketChannel channel{BinaryMarketChannel::BookTicker};
    std::uint8_t nativeIdentityShape{0u};
    std::uint16_t eventOrdinal{0u};
    std::uint32_t reserved{0u};
};

struct alignas(64) BinaryMarketRecord final {
    BinaryMarketRecordHeader header{};
    alignas(8) std::array<std::byte, kBinaryMarketPayloadBytes> payload{};
};

template <typename Payload>
[[nodiscard]] inline Payload* binaryMarketPayload(
    BinaryMarketRecord* record) noexcept {
    static_assert(std::is_trivially_copyable_v<Payload>);
    static_assert(sizeof(Payload) <= kBinaryMarketPayloadBytes);
    return record ? reinterpret_cast<Payload*>(record->payload.data()) : nullptr;
}

template <typename Payload>
[[nodiscard]] inline const Payload* binaryMarketPayload(
    const BinaryMarketRecord* record) noexcept {
    static_assert(std::is_trivially_copyable_v<Payload>);
    static_assert(sizeof(Payload) <= kBinaryMarketPayloadBytes);
    return record
        ? reinterpret_cast<const Payload*>(record->payload.data())
        : nullptr;
}

struct BinaryMarketBookTickerPayload final {
    std::int64_t bidPriceRaw{0};
    std::int64_t bidQtyRaw{0};
    std::int64_t askPriceRaw{0};
    std::int64_t askQtyRaw{0};
};

struct BinaryMarketTradePayload final {
    std::int64_t priceRaw{0};
    std::int64_t qtyRaw{0};
    std::uint8_t side{0u};
    std::array<std::byte, 7u> reserved{};
};

struct BinaryMarketLiquidationPayload final {
    std::int64_t priceRaw{0};
    std::int64_t qtyRaw{0};
    std::int64_t averagePriceRaw{0};
    std::int64_t filledQtyRaw{0};
    std::int32_t orderType{0};
    std::int32_t timeInForce{0};
    std::int32_t status{0};
    std::int32_t sourceMode{0};
    std::uint8_t side{0u};
    std::array<std::byte, 7u> reserved{};
};

struct BinaryMarketScalarPricePayload final {
    std::int64_t priceRaw{0};
};

struct BinaryMarketFundingPayload final {
    std::int64_t fundingRateRaw{0};
    std::int64_t fundingTimestampNs{0};
    std::int64_t nextFundingTimestampNs{0};
};

struct BinaryMarketPriceLimitPayload final {
    std::int64_t buyLimitRaw{0};
    std::int64_t sellLimitRaw{0};
    std::uint8_t enabled{0u};
    std::array<std::byte, 7u> reserved{};
};

struct BinaryMarketDepthSequence final {
    std::uint64_t firstUpdateId{0u};
    std::uint64_t lastUpdateId{0u};
    std::uint64_t previousUpdateId{0u};
};

struct BinaryMarketDepthLevel final {
    std::uint64_t priceRaw{0u};
    std::uint64_t quantityRaw{0u};
    std::uint8_t side{0u};
    std::uint8_t action{0u};
    std::uint16_t reserved16{0u};
    std::uint32_t reserved32{0u};
};

enum class BinaryMarketDepthFrameKind : std::uint8_t {
    Unknown = 0u,
    Snapshot = 1u,
    Delta = 2u,
    State = 3u,
};

enum class BinaryMarketDepthState : std::uint8_t {
    Empty = 0u,
    Partial = 1u,
    RebaseInFlight = 2u,
    Healthy = 3u,
    Gap = 4u,
};

struct BinaryMarketDepthChunkPayload final {
    BinaryMarketDepthSequence sequence{};
    std::uint32_t totalLevelCount{0u};
    std::uint32_t levelOffset{0u};
    std::uint16_t partIndex{0u};
    std::uint16_t partCount{0u};
    std::uint16_t levelCount{0u};
    BinaryMarketDepthFrameKind frameKind{
        BinaryMarketDepthFrameKind::Unknown};
    BinaryMarketDepthState state{BinaryMarketDepthState::Empty};
    std::uint8_t failure{0u};
    std::uint8_t reserved8{0u};
    std::uint16_t transactionPartIndex{0u};
    std::uint16_t transactionPartCount{0u};
    std::uint16_t reserved16{0u};
    std::uint64_t sourceReceiveRealtimeNs{0u};
    std::uint64_t sourceFreshnessMonotonicNs{0u};
    std::array<BinaryMarketDepthLevel, kBinaryMarketDepthLevelsPerRecord>
        levels{};
};

enum class BinaryMarketStopReason : std::uint8_t {
    Requested = 1u,
    Duration = 2u,
    Quota = 3u,
    ParserDisconnected = 4u,
    Error = 5u,
};

struct alignas(64) BinaryMarketManifest final {
    std::uint32_t magic{kBinaryMarketManifestMagic};
    std::uint16_t schemaVersion{kBinaryMarketCorpusSchemaVersion};
    std::uint16_t headerBytes{0u};
    std::uint64_t writerAbiFingerprint{0u};
    std::uint64_t producerEpoch{0u};
    std::int64_t startedReceiveNs{0};
    std::int64_t replayAnchorReceiveNs{0};
    std::uint64_t replayAnchorMonotonicNs{0u};
    std::int64_t finalizedReceiveNs{0};
    std::int64_t firstReceiveNs{0};
    std::int64_t lastReceiveNs{0};
    std::uint64_t maximumBytes{0u};
    std::uint64_t segmentTargetBytes{0u};
    std::uint64_t projectedBytes{0u};
    std::uint64_t recordCount{0u};
    std::uint64_t gapCount{0u};
    std::uint32_t sourceCount{0u};
    std::uint32_t segmentCount{0u};
    std::uint32_t indexCount{0u};
    std::uint16_t shardCount{0u};
    BinaryMarketStopReason stopReason{BinaryMarketStopReason::Requested};
    std::uint8_t complete{0u};
    std::uint32_t sourcesCrc32c{0u};
    std::uint32_t indexCrc32c{0u};
    std::uint32_t gapsCrc32c{0u};
    std::uint32_t manifestCrc32c{0u};
    std::array<std::byte, 40u> reserved{};
};

struct BinaryMarketTableHeader final {
    std::uint32_t magic{0u};
    std::uint16_t schemaVersion{kBinaryMarketCorpusSchemaVersion};
    std::uint16_t headerBytes{0u};
    std::uint32_t recordBytes{0u};
    std::uint32_t recordCount{0u};
    std::uint32_t recordsCrc32c{0u};
    std::uint32_t headerCrc32c{0u};
    std::array<std::byte, 40u> reserved{};
};

struct alignas(64) BinaryMarketSegmentHeader final {
    std::uint32_t magic{kBinaryMarketSegmentMagic};
    std::uint16_t schemaVersion{kBinaryMarketCorpusSchemaVersion};
    std::uint16_t headerBytes{0u};
    std::uint32_t recordBytes{0u};
    std::uint32_t segmentNumber{0u};
    std::uint64_t producerEpoch{0u};
    std::uint64_t recordCount{0u};
    std::int64_t firstReceiveNs{0};
    std::int64_t lastReceiveNs{0};
    std::uint16_t shardIndex{0u};
    std::uint16_t reserved16{0u};
    std::uint32_t recordsCrc32c{0u};
    std::uint32_t headerCrc32c{0u};
    std::array<std::byte, 64u> reserved{};
};

struct alignas(64) BinaryMarketSegmentFooter final {
    std::uint32_t magic{kBinaryMarketSegmentFooterMagic};
    std::uint16_t schemaVersion{kBinaryMarketCorpusSchemaVersion};
    std::uint16_t footerBytes{0u};
    std::uint32_t segmentNumber{0u};
    std::uint32_t recordsCrc32c{0u};
    std::uint64_t recordCount{0u};
    std::int64_t firstReceiveNs{0};
    std::int64_t lastReceiveNs{0};
    std::uint32_t footerCrc32c{0u};
    std::array<std::byte, 20u> reserved{};
};

enum BinaryMarketIndexFlags : std::uint32_t {
    BinaryMarketIndexNone = 0u,
    BinaryMarketIndexHasGap = 1u << 0u,
    BinaryMarketIndexHasRecordedOnly = 1u << 1u,
};

struct BinaryMarketIndexEntry final {
    std::uint32_t segmentNumber{0u};
    std::uint32_t sourceId{0u};
    std::uint64_t sourceGeneration{0u};
    std::uint64_t recordCount{0u};
    std::uint64_t gapCount{0u};
    std::uint64_t firstEventSequence{0u};
    std::uint64_t lastEventSequence{0u};
    std::int64_t firstReceiveNs{0};
    std::int64_t lastReceiveNs{0};
    std::uint32_t flags{BinaryMarketIndexNone};
    std::uint16_t shardIndex{0u};
    BinaryMarketChannel channel{BinaryMarketChannel::BookTicker};
    BinaryMarketCompatibility compatibility{
        BinaryMarketCompatibility::Unavailable};
};

struct BinaryMarketGap final {
    std::uint32_t sourceId{0u};
    std::uint32_t reserved32{0u};
    std::uint64_t sourceGeneration{0u};
    std::uint64_t gapEpoch{0u};
    std::uint64_t droppedRecords{0u};
    std::uint64_t firstDroppedEventSequence{0u};
    std::uint64_t lastDroppedEventSequence{0u};
    // A zero/zero range means the arrival clock itself was unavailable. Such a
    // loss intersects every receive-time selection for this source/channel.
    std::int64_t minimumDroppedReceiveNs{0};
    std::int64_t maximumDroppedReceiveNs{0};
    std::int64_t observedReceiveNs{0};
    std::uint16_t shardIndex{0u};
    BinaryMarketChannel channel{BinaryMarketChannel::BookTicker};
    std::uint8_t reserved8{0u};
};

[[nodiscard]] inline bool validBinaryMarketRecord(
    const BinaryMarketRecord& record) noexcept {
    const auto& header = record.header;
    return header.sourceId != 0u && header.sourceGeneration != 0u &&
           header.sessionEpoch != 0u && header.eventSequence != 0u &&
           header.frameSequence != 0u &&
           header.exchangeTimestampNs >= 0 &&
           (((header.flags &
              BinaryMarketRecordExchangeTimestampMissing) != 0u) ==
            (header.exchangeTimestampNs == 0)) &&
           header.receiveRealtimeNs > 0 && header.receiveMonotonicNs != 0u &&
           header.shardSequence != 0u &&
           header.payloadBytes != 0u &&
           header.payloadBytes <= kBinaryMarketPayloadBytes &&
           header.schemaVersion == kBinaryMarketRecordSchemaVersion &&
           validBinaryMarketChannel(header.channel) &&
           (header.flags & ~kBinaryMarketKnownRecordFlags) == 0u &&
           (header.sourceFlags & ~kBinaryMarketKnownSourceFlags) == 0u &&
           (((header.sourceFlags &
              BinaryMarketSourceExchangeTimestampMissing) == 0u) ||
            ((header.flags &
              BinaryMarketRecordExchangeTimestampMissing) != 0u)) &&
           header.reserved == 0u;
}

[[nodiscard]] inline constexpr std::uint64_t
binaryMarketCorpusAbiFingerprint() noexcept {
    return 0x4852434f52500003ull ^
        (static_cast<std::uint64_t>(sizeof(BinaryMarketManifest)) << 1u) ^
        (static_cast<std::uint64_t>(sizeof(BinaryMarketSource)) << 11u) ^
        (static_cast<std::uint64_t>(sizeof(BinaryMarketRecord)) << 27u) ^
        (static_cast<std::uint64_t>(sizeof(BinaryMarketRecordHeader)) << 35u) ^
        (static_cast<std::uint64_t>(sizeof(BinaryMarketIndexEntry)) << 43u) ^
        (static_cast<std::uint64_t>(sizeof(BinaryMarketGap)) << 53u);
}

static_assert(sizeof(BinaryMarketRecordHeader) == 104u);
static_assert(sizeof(BinaryMarketDepthLevel) == 24u);
static_assert(sizeof(BinaryMarketDepthChunkPayload) ==
              kBinaryMarketPayloadBytes);
static_assert(sizeof(BinaryMarketManifest) == 192u);
static_assert(sizeof(BinaryMarketRecord) == 960u);
static_assert(sizeof(BinaryMarketTableHeader) == 64u);
static_assert(sizeof(BinaryMarketSegmentHeader) == 128u);
static_assert(sizeof(BinaryMarketSegmentFooter) == 64u);
static_assert(std::is_trivially_copyable_v<BinaryMarketManifest>);
static_assert(std::is_trivially_copyable_v<BinaryMarketSource>);
static_assert(std::is_trivially_copyable_v<BinaryMarketRecord>);
static_assert(std::is_trivially_copyable_v<BinaryMarketIndexEntry>);
static_assert(std::is_trivially_copyable_v<BinaryMarketGap>);

}  // namespace hftrec::corpus
