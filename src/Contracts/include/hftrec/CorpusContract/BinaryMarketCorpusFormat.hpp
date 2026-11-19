#pragma once

#include "cxet/Api/Market/MarketNativeEvidence.hpp"
#include "cxet/Market/Depth.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <type_traits>

namespace hftrec::corpus {

inline constexpr std::size_t kBinaryMarketChannelCount = 8u;
inline constexpr std::size_t kBinaryMarketStoredChannelCount = 9u;
inline constexpr std::size_t kBinaryMarketBlockStreamCount = 10u;
inline constexpr std::size_t kBinaryMarketVenueBytes = 16u;
inline constexpr std::size_t kBinaryMarketMarketBytes = 16u;
inline constexpr std::size_t kBinaryMarketSymbolBytes = 48u;
inline constexpr std::size_t kBinaryMarketPayloadBytes = 888u;
inline constexpr std::size_t kBinaryMarketDepthLevelsPerRecord = 32u;
inline constexpr std::size_t kBinaryMarketDepthCoveragePerRecord = 5u;
inline constexpr std::uint8_t kBinaryMarketFundingRateScaleDigits = 8u;

inline constexpr std::uint32_t kBinaryMarketManifestMagic = 0x4d435248u;
inline constexpr std::uint32_t kBinaryMarketSourcesMagic = 0x53435248u;
inline constexpr std::uint32_t kBinaryMarketIndexMagic = 0x49435248u;
inline constexpr std::uint32_t kBinaryMarketGapsMagic = 0x47435248u;
inline constexpr std::uint32_t kBinaryMarketSegmentMagic = 0x53475248u;
inline constexpr std::uint32_t kBinaryMarketSegmentFooterMagic = 0x46475248u;

inline constexpr std::size_t kBinaryMarketBlockRecords = 256u;
inline constexpr std::uint16_t kBinaryMarketMaximumShards = 64u;
inline constexpr std::uint32_t kBinaryMarketMaximumSources = 65536u;
inline constexpr std::uint32_t kBinaryMarketBlockMagic = 0x42435248u;
inline constexpr std::uint32_t kBinaryMarketCompactSegmentCodecId = 2u;

enum class ByteBlockCodecMode : std::uint8_t {
    Opaque = 0u,
    TransformOnly = 1u,
    Lz4Fast = 2u,
    ZstdFast = 3u,
    ZstdMax = 4u,
    Brotli = 5u,
    XzLzma = 6u,
    ArithmeticShift = 7u,
    RansByteStatic = 8u,
};

[[nodiscard]] inline constexpr bool validByteBlockCodecMode(
    ByteBlockCodecMode mode) noexcept {
    return static_cast<std::uint8_t>(mode) <=
           static_cast<std::uint8_t>(ByteBlockCodecMode::RansByteStatic);
}

[[nodiscard]] inline constexpr bool isOnlineByteBlockCodecMode(
    ByteBlockCodecMode mode) noexcept {
    return mode == ByteBlockCodecMode::Opaque ||
           mode == ByteBlockCodecMode::TransformOnly ||
           mode == ByteBlockCodecMode::Lz4Fast ||
           mode == ByteBlockCodecMode::ZstdFast;
}

[[nodiscard]] inline constexpr std::string_view byteBlockCodecModeName(
    ByteBlockCodecMode mode) noexcept {
    switch (mode) {
        case ByteBlockCodecMode::Opaque: return "opaque";
        case ByteBlockCodecMode::TransformOnly: return "transform_only";
        case ByteBlockCodecMode::Lz4Fast: return "lz4_fast";
        case ByteBlockCodecMode::ZstdFast: return "zstd_fast";
        case ByteBlockCodecMode::ZstdMax: return "zstd_max";
        case ByteBlockCodecMode::Brotli: return "brotli";
        case ByteBlockCodecMode::XzLzma: return "xz_lzma";
        case ByteBlockCodecMode::ArithmeticShift: return "arithmetic_shift";
        case ByteBlockCodecMode::RansByteStatic: return "rans_byte_static";
    }
    return "unknown";
}

[[nodiscard]] inline constexpr bool parseByteBlockCodecMode(
    std::string_view text,
    ByteBlockCodecMode& out) noexcept {
    if (text == "lz4_fast" || text == "lz4") {
        out = ByteBlockCodecMode::Lz4Fast;
        return true;
    }
    if (text == "zstd_fast" || text == "zstd") {
        out = ByteBlockCodecMode::ZstdFast;
        return true;
    }
    if (text == "transform_only" || text == "transform") {
        out = ByteBlockCodecMode::TransformOnly;
        return true;
    }
    if (text == "opaque" || text == "none" || text == "raw") {
        out = ByteBlockCodecMode::Opaque;
        return true;
    }
    if (text == "zstd_max") {
        out = ByteBlockCodecMode::ZstdMax;
        return true;
    }
    if (text == "brotli") {
        out = ByteBlockCodecMode::Brotli;
        return true;
    }
    if (text == "xz_lzma") {
        out = ByteBlockCodecMode::XzLzma;
        return true;
    }
    if (text == "arithmetic_shift") {
        out = ByteBlockCodecMode::ArithmeticShift;
        return true;
    }
    if (text == "rans_byte_static") {
        out = ByteBlockCodecMode::RansByteStatic;
        return true;
    }
    return false;
}

enum class BinaryMarketChannel : std::uint8_t {
    BookTicker = 1u,
    Trade = 2u,
    Depth = 3u,
    Liquidation = 4u,
    MarkPrice = 5u,
    IndexPrice = 6u,
    Funding = 7u,
    PriceLimit = 8u,
    SourceLifecycle = 9u,
};

enum class BinaryMarketBlockStream : std::uint8_t {
    BookTicker = 1u,
    Trade = 2u,
    Depth = 3u,
    Liquidation = 4u,
    MarkPrice = 5u,
    IndexPrice = 6u,
    Funding = 7u,
    PriceLimit = 8u,
    SourceLifecycle = 9u,
    DepthCoverage = 10u,
};

[[nodiscard]] inline constexpr bool validBinaryMarketChannel(
    BinaryMarketChannel channel) noexcept {
    return channel >= BinaryMarketChannel::BookTicker &&
           channel <= BinaryMarketChannel::PriceLimit;
}

[[nodiscard]] inline constexpr bool validBinaryMarketRecordChannel(
    BinaryMarketChannel channel) noexcept {
    return validBinaryMarketChannel(channel) ||
           channel == BinaryMarketChannel::SourceLifecycle;
}

[[nodiscard]] inline constexpr bool validBinaryMarketBlockStream(
    BinaryMarketBlockStream stream) noexcept {
    return stream >= BinaryMarketBlockStream::BookTicker &&
           stream <= BinaryMarketBlockStream::DepthCoverage;
}

[[nodiscard]] inline constexpr std::size_t binaryMarketChannelIndex(
    BinaryMarketChannel channel) noexcept {
    return static_cast<std::size_t>(channel) - 1u;
}

[[nodiscard]] inline constexpr std::size_t binaryMarketBlockStreamIndex(
    BinaryMarketBlockStream stream) noexcept {
    return static_cast<std::size_t>(stream) - 1u;
}

[[nodiscard]] inline constexpr BinaryMarketChannel binaryMarketChannelForStream(
    BinaryMarketBlockStream stream) noexcept {
    return stream == BinaryMarketBlockStream::DepthCoverage
        ? BinaryMarketChannel::Depth
        : static_cast<BinaryMarketChannel>(static_cast<std::uint8_t>(stream));
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
    BinaryMarketInstrumentCanonicalStepSize = 1u << 2u,
    BinaryMarketInstrumentExecutionQuantityStep = 1u << 3u,
};

inline constexpr std::uint32_t kBinaryMarketInstrumentMetadataFlags =
    BinaryMarketInstrumentStepSize |
    BinaryMarketInstrumentContractBaseQty |
    BinaryMarketInstrumentCanonicalStepSize |
    BinaryMarketInstrumentExecutionQuantityStep;

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
    BinaryMarketDirectoryPriceLimit = 1u << 11u,
    BinaryMarketDirectoryFunding = 1u << 12u,
    BinaryMarketDirectoryReferenceOnly = 1u << 13u,
};

inline constexpr std::uint16_t kBinaryMarketKnownDirectoryFlags =
    BinaryMarketDirectoryBookTicker | BinaryMarketDirectoryTrade |
    BinaryMarketDirectoryTickSize | BinaryMarketDirectoryDepth |
    BinaryMarketDirectoryBboSpread | BinaryMarketDirectoryRanking |
    BinaryMarketDirectoryFundingEligible | BinaryMarketDirectoryDelivery |
    BinaryMarketDirectoryMarginEligibilityKnown |
    BinaryMarketDirectoryMarginEligible |
    BinaryMarketDirectorySpotPostOnly |
    BinaryMarketDirectoryPriceLimit |
    BinaryMarketDirectoryFunding |
    BinaryMarketDirectoryReferenceOnly;

inline constexpr std::uint16_t kBinaryMarketDirectoryMarketFamilyFlags =
    BinaryMarketDirectoryBookTicker | BinaryMarketDirectoryTrade |
    BinaryMarketDirectoryDepth | BinaryMarketDirectoryPriceLimit |
    BinaryMarketDirectoryFunding;

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
    BinaryMarketRecordSourceLifecycle = 1u << 12u,
    BinaryMarketRecordDepthCoverage = 1u << 13u,
    BinaryMarketRecordDepthHasCoverage = 1u << 14u,
    BinaryMarketRecordDepthTransactionEnd = 1u << 15u,
    BinaryMarketRecordDepthSourcePosition = 1u << 16u,
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
    BinaryMarketRecordDepthState |
    BinaryMarketRecordSourceLifecycle |
    BinaryMarketRecordDepthCoverage |
    BinaryMarketRecordDepthHasCoverage |
    BinaryMarketRecordDepthTransactionEnd |
    BinaryMarketRecordDepthSourcePosition;

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

enum class BinaryMarketQuantityKind : std::uint8_t {
    Unknown=0u, CanonicalBase=1u, NativeBase=2u, NativeContract=3u, NativeLot=4u,
};

struct BinaryMarketQuantityConversion final {
    std::array<BinaryMarketQuantityKind,3u> kinds{};
    std::array<std::byte,5u> reserved{};
    std::uint32_t canonicalBaseMultiplier{0u};
    std::uint32_t nativeBaseMultiplier{0u};
    std::int64_t nativeContractBaseQtyRaw{0};
    std::int64_t nativeLotBaseQtyRaw{0};
    bool operator==(const BinaryMarketQuantityConversion&) const = default;
};
static_assert(sizeof(BinaryMarketQuantityConversion)==32u);

[[nodiscard]] inline constexpr bool validBinaryMarketQuantityConversion(
    const BinaryMarketQuantityConversion& conversion) noexcept {
    if (conversion.reserved!=decltype(conversion.reserved){} ||
        conversion.nativeContractBaseQtyRaw<0 || conversion.nativeLotBaseQtyRaw<0) return false;
    bool known=false,contract=false,lot=false;
    for (const auto kind:conversion.kinds) {
        if (static_cast<std::uint8_t>(kind)>4u) return false;
        known|=kind!=BinaryMarketQuantityKind::Unknown;
        contract|=kind==BinaryMarketQuantityKind::NativeContract;
        lot|=kind==BinaryMarketQuantityKind::NativeLot;
    }
    if (!known) return !conversion.canonicalBaseMultiplier && !conversion.nativeBaseMultiplier &&
        !conversion.nativeContractBaseQtyRaw && !conversion.nativeLotBaseQtyRaw;
    return conversion.canonicalBaseMultiplier && conversion.nativeBaseMultiplier &&
        (contract ? conversion.nativeContractBaseQtyRaw>0 : conversion.nativeContractBaseQtyRaw==0) &&
        (lot ? conversion.nativeLotBaseQtyRaw>0 : conversion.nativeLotBaseQtyRaw==0);
}

struct BinaryMarketSource final {
    std::uint32_t sourceId{0u};
    std::uint32_t canonicalSymbolId{0u};
    std::uint64_t initialSourceGeneration{0u};
    std::int64_t tickSizeRaw{0};
    std::int64_t stepSizeRaw{0};
    std::int64_t contractBaseQtyRaw{0};
    std::int64_t canonicalStepSizeRaw{0};
    std::int64_t executionQuantityStepRaw{0};
    BinaryMarketQuantityConversion quantityConversion{};
    std::int64_t priceBasisQtyRaw{0};
    std::uint32_t economicBaseAssetId{0u};
    std::uint32_t quoteAssetId{0u};
    std::uint16_t venueId{0u};
    std::uint16_t directoryFlags{0u};
    std::uint16_t configuredChannelMask{0u};
    std::uint16_t availableChannelMask{0u};
    std::uint16_t traderReplayChannelMask{0u};
    std::uint32_t instrumentMetadataFlags{0u};
    std::uint32_t maxDepthCoverageSources{0u};
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
    // Initial capture membership; late-listed final catalog entries stay inactive until Added.
    std::uint8_t initiallyPresent{1u};
    std::array<std::byte, 2u> reserved{};
    std::array<char, kBinaryMarketVenueBytes> venue{};
    std::array<char, kBinaryMarketMarketBytes> market{};
    std::array<char, kBinaryMarketSymbolBytes> canonicalSymbol{};
    std::array<char, kBinaryMarketSymbolBytes> nativeSymbol{};
};
static_assert(sizeof(BinaryMarketSource) == 272u);

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
        (source.initialSourceGeneration == 0u && source.initiallyPresent != 0u) ||
        source.venueId == 0u ||
        (source.marketKindRaw != 1u && source.marketKindRaw != 2u) ||
        source.assetDomainRaw > 2u ||
        source.priceScale > 18u || source.quantityScale > 18u ||
        source.maxDepthCoverageSources > cxet::market::kMaximumDepthSourceCapacity ||
        !validBinaryMarketText(source.venue, source.venueBytes) ||
        !validBinaryMarketCode(source.marketRaw, source.market,
                               source.marketBytes) ||
        !validBinaryMarketText(source.canonicalSymbol,
                               source.canonicalSymbolBytes) ||
        !validBinaryMarketText(source.nativeSymbol,
                               source.nativeSymbolBytes) ||
        source.tickSizeRaw < 0 || source.stepSizeRaw < 0 ||
        source.contractBaseQtyRaw < 0 || source.priceBasisQtyRaw < 0 ||
        source.canonicalStepSizeRaw < 0 || source.executionQuantityStepRaw < 0 ||
        !validBinaryMarketQuantityConversion(source.quantityConversion) ||
        source.economicBaseAssetId == 0u || source.quoteAssetId == 0u ||
        (source.directoryFlags & ~kBinaryMarketKnownDirectoryFlags) != 0u ||
        ((source.directoryFlags & BinaryMarketDirectoryReferenceOnly) != 0u &&
         ((source.directoryFlags & kBinaryMarketDirectoryMarketFamilyFlags) != 0u ||
          source.configuredChannelMask != 0u)) ||
        (((source.directoryFlags & BinaryMarketDirectoryTickSize) != 0u) !=
         (source.tickSizeRaw > 0)) ||
        ((source.directoryFlags & BinaryMarketDirectoryMarginEligible) != 0u &&
         (source.directoryFlags &
          BinaryMarketDirectoryMarginEligibilityKnown) == 0u) ||
        (source.instrumentMetadataFlags &
         ~kBinaryMarketInstrumentMetadataFlags) != 0u ||
        (((source.instrumentMetadataFlags & BinaryMarketInstrumentCanonicalStepSize) != 0u) !=
         (source.canonicalStepSizeRaw > 0)) ||
        (((source.instrumentMetadataFlags & BinaryMarketInstrumentExecutionQuantityStep) != 0u) !=
         (source.executionQuantityStepRaw > 0)) ||
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
        source.initiallyPresent > 1u ||
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
    for (std::size_t channel=0u;channel<source.quantityConversion.kinds.size();++channel)
        if (source.quantityConversion.kinds[channel]!=BinaryMarketQuantityKind::Unknown &&
            (source.configuredChannelMask&(std::uint16_t{1u}<<channel))==0u) return false;
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

enum class BinaryMarketSourceLifecycleKind : std::uint8_t {
    Added=1u, Removed=2u, Readded=3u, DirectoryChanged=4u,
};
enum class BinaryMarketSourceTransitionCause : std::uint8_t {
    Subscription=0u, NativeStale=1u, NativeDegraded=2u, NativeSequenceGap=3u,
    NativeRecovered=4u, NativeClosed=5u,
};
[[nodiscard]] inline constexpr bool binaryMarketTransitionIsUnhealthy(BinaryMarketSourceTransitionCause cause) noexcept {
    return cause==BinaryMarketSourceTransitionCause::NativeStale || cause==BinaryMarketSourceTransitionCause::NativeDegraded ||
        cause==BinaryMarketSourceTransitionCause::NativeSequenceGap || cause==BinaryMarketSourceTransitionCause::NativeClosed;
}
struct BinaryMarketSourceLifecyclePayload final {
    std::uint64_t directoryRevision{0u};
    std::uint64_t previousSourceGeneration{0u};
    std::uint64_t requestSequence{0u};
    std::uint16_t channelMaskBefore{0u};
    std::uint16_t channelMaskAfter{0u};
    BinaryMarketSourceLifecycleKind kind{BinaryMarketSourceLifecycleKind::Added};
    std::uint8_t affectedChannel{0u};
    std::uint8_t reason{0u};
    BinaryMarketSourceTransitionCause cause{BinaryMarketSourceTransitionCause::Subscription};
    // Exact producer metadata at the transition. initialSourceGeneration is
    // the effective generation here; sources.bin preserves initial metadata.
    BinaryMarketSource source{};
};

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
    BinaryMarketChannel channel{BinaryMarketChannel::BookTicker};
    std::uint8_t nativeIdentityShape{0u};
    std::uint16_t eventOrdinal{0u};
    std::uint32_t reserved{0u};
};

[[nodiscard]] inline constexpr BinaryMarketBlockStream binaryMarketBlockStreamForRecord(
    const BinaryMarketRecordHeader& header) noexcept {
    if (header.channel == BinaryMarketChannel::Depth &&
        (header.flags & BinaryMarketRecordDepthCoverage) != 0u) {
        return BinaryMarketBlockStream::DepthCoverage;
    }
    return static_cast<BinaryMarketBlockStream>(
        static_cast<std::uint8_t>(header.channel));
}

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
    cxet::api::market::MarketNativeEvidence nativeEvidence{};
};

struct BinaryMarketTradePayload final {
    std::int64_t priceRaw{0};
    std::int64_t qtyRaw{0};
    std::uint8_t side{0u};
    std::array<std::byte, 7u> reserved{};
    cxet::api::market::MarketNativeEvidence nativeEvidence{};
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
    cxet::api::market::MarketNativeEvidence nativeEvidence{};
    std::array<BinaryMarketDepthLevel, kBinaryMarketDepthLevelsPerRecord>
        levels{};
};

[[nodiscard]] inline bool binaryMarketDepthRecordIsComposedCut(const BinaryMarketRecord& record) noexcept {
    const auto flags=record.header.flags;
    constexpr auto required=BinaryMarketRecordDepthSnapshot|BinaryMarketRecordDepthSourcePosition|BinaryMarketRecordDepthHasCoverage;
    constexpr auto forbidden=BinaryMarketRecordDepthDelta|BinaryMarketRecordDepthState|BinaryMarketRecordDepthRebase|BinaryMarketRecordDepthCoverage;
    if(record.header.channel!=BinaryMarketChannel::Depth || record.header.payloadBytes!=sizeof(BinaryMarketDepthChunkPayload) ||
       (flags&required)!=required || (flags&forbidden)!=0u) return false;
    const auto& p=*binaryMarketPayload<BinaryMarketDepthChunkPayload>(&record);
    return p.frameKind==BinaryMarketDepthFrameKind::Snapshot && p.transactionPartIndex==0u && p.transactionPartCount==1u &&
        (p.state==BinaryMarketDepthState::Healthy || p.state==BinaryMarketDepthState::Partial || p.state==BinaryMarketDepthState::Gap ||
         (p.state==BinaryMarketDepthState::Empty && p.totalLevelCount==0u && p.levelCount==0u));
}

[[nodiscard]] inline bool validBinaryMarketDepthNativeState(const BinaryMarketRecord& record) noexcept {
    const auto& p=*binaryMarketPayload<BinaryMarketDepthChunkPayload>(&record);
    const auto flags=record.header.flags;
    const bool composed=binaryMarketDepthRecordIsComposedCut(record);
    const bool partial=p.state==BinaryMarketDepthState::Partial;
    const bool healthy=p.state==BinaryMarketDepthState::Healthy;
    const bool rebase=p.state==BinaryMarketDepthState::RebaseInFlight;
    const bool gap=p.state==BinaryMarketDepthState::Gap;
    const bool snapshot=p.frameKind==BinaryMarketDepthFrameKind::Snapshot;
    const bool delta=p.frameKind==BinaryMarketDepthFrameKind::Delta;
    const bool state=p.frameKind==BinaryMarketDepthFrameKind::State;
    const bool compatible=(flags&BinaryMarketRecordTraderReplayCompatible)!=0u;
    if((!partial&&!healthy&&!rebase&&!gap&&!composed) || (!snapshot&&!delta&&state==false) ||
       p.failure>static_cast<std::uint8_t>(cxet::market::DepthFailure::ConsumerBookRejected) ||
       (((flags&BinaryMarketRecordDepthSnapshot)!=0u)!=snapshot) ||
       (((flags&BinaryMarketRecordDepthDelta)!=0u)!=delta) ||
       (((flags&BinaryMarketRecordDepthState)!=0u)!=state) ||
       (((flags&BinaryMarketRecordDepthPartial)!=0u)!=partial) ||
       (((flags&BinaryMarketRecordDepthRebase)!=0u)!=rebase) ||
       compatible!=(partial||healthy||rebase||composed) ||
       (p.totalLevelCount==0u&&!state&&!snapshot) || (state&&p.totalLevelCount!=0u) ||
       ((partial||healthy||rebase)&&p.failure!=0u) || (gap&&p.failure==0u&&!composed) ||
       (p.state==BinaryMarketDepthState::Empty&&p.failure!=0u) ||
       ((partial||healthy)&&!delta&&!snapshot) ||
       (rebase&&(snapshot!=(p.transactionPartIndex==0u))) ||
       (!rebase&&p.transactionPartCount!=1u) ||
       (state&&(!gap||compatible||partial||rebase||p.sourceReceiveRealtimeNs!=0u)) ||
       (!state&&((!composed&&(p.sourceReceiveRealtimeNs==0u||p.sourceFreshnessMonotonicNs==0u)) ||
                 p.sourceReceiveRealtimeNs>0x7fff'ffff'ffff'ffffull))) return false;
    return true;
}

[[nodiscard]] inline bool binaryMarketDepthRecordIsNativeRebaseSnapshot(const BinaryMarketRecord& record) noexcept {
    constexpr auto required=BinaryMarketRecordTraderReplayCompatible|BinaryMarketRecordDepthSnapshot|BinaryMarketRecordDepthRebase;
    constexpr auto forbidden=BinaryMarketRecordDepthDelta|BinaryMarketRecordDepthState|BinaryMarketRecordDepthCoverage|
        BinaryMarketRecordDepthHasCoverage|BinaryMarketRecordDepthSourcePosition;
    if(record.header.channel!=BinaryMarketChannel::Depth || record.header.payloadBytes!=sizeof(BinaryMarketDepthChunkPayload) ||
       (record.header.flags&required)!=required || (record.header.flags&forbidden)!=0u) return false;
    const auto& p=*binaryMarketPayload<BinaryMarketDepthChunkPayload>(&record);
    return p.frameKind==BinaryMarketDepthFrameKind::Snapshot && p.state==BinaryMarketDepthState::RebaseInFlight &&
        p.transactionPartIndex==0u && p.partIndex==0u && p.failure==0u &&
        cxet::market::validDepthSnapshotSequence({p.sequence.firstUpdateId,p.sequence.lastUpdateId,p.sequence.previousUpdateId});
}

struct BinaryMarketDepthCoverage final {
    std::uint32_t sourceId{}, sequenceDomain{}, maximumLevelsPerSide{}, sequenceFlags{};
    std::uint64_t sourceGeneration{}, sessionEpoch{}, lastUpdateId{}, freshnessMonotonicNs{};
    std::int64_t lowestBidPriceRaw{}, highestAskPriceRaw{};
    std::int64_t lowestProtectedBidPriceRaw{}, highestProtectedAskPriceRaw{};
    BinaryMarketDepthState state{BinaryMarketDepthState::Empty};
    std::uint8_t failure{}, snapshotAligned{}, liquidityPopulation{};
    std::uint32_t reserved32{};
    cxet::api::market::MarketNativeEvidence nativeEvidence{};
    friend bool operator==(const BinaryMarketDepthCoverage&, const BinaryMarketDepthCoverage&) noexcept = default;
};

struct BinaryMarketDepthCoveragePayload final {
    std::uint32_t totalCoverageCount{}, coverageOffset{};
    std::uint16_t coverageCount{}, partIndex{}, partCount{}, transactionPartCount{};
    std::array<BinaryMarketDepthCoverage, kBinaryMarketDepthCoveragePerRecord> coverage{};
};

[[nodiscard]] inline constexpr std::size_t binaryMarketStreamPayloadBytes(
    BinaryMarketBlockStream stream) noexcept {
    switch (stream) {
        case BinaryMarketBlockStream::BookTicker:
            return sizeof(BinaryMarketBookTickerPayload);
        case BinaryMarketBlockStream::Trade:
            return sizeof(BinaryMarketTradePayload);
        case BinaryMarketBlockStream::Depth:
            return sizeof(BinaryMarketDepthChunkPayload);
        case BinaryMarketBlockStream::Liquidation:
            return sizeof(BinaryMarketLiquidationPayload);
        case BinaryMarketBlockStream::MarkPrice:
        case BinaryMarketBlockStream::IndexPrice:
            return sizeof(BinaryMarketScalarPricePayload);
        case BinaryMarketBlockStream::Funding:
            return sizeof(BinaryMarketFundingPayload);
        case BinaryMarketBlockStream::PriceLimit:
            return sizeof(BinaryMarketPriceLimitPayload);
        case BinaryMarketBlockStream::SourceLifecycle:
            return sizeof(BinaryMarketSourceLifecyclePayload);
        case BinaryMarketBlockStream::DepthCoverage:
            return sizeof(BinaryMarketDepthCoveragePayload);
    }
    return 0u;
}

[[nodiscard]] inline constexpr std::size_t binaryMarketCompactRecordBytes(
    BinaryMarketBlockStream stream) noexcept {
    const std::size_t payload = binaryMarketStreamPayloadBytes(stream);
    return payload == 0u ? 0u : sizeof(BinaryMarketRecordHeader) + payload;
}

[[nodiscard]] inline BinaryMarketDepthCoverage encodeBinaryMarketDepthCoverage(const cxet::market::DepthCoverage& feed) noexcept {
    BinaryMarketDepthCoverage out{};
    out.sourceId=feed.sourceId;out.sequenceDomain=feed.sequenceDomain;
    out.maximumLevelsPerSide=feed.maximumLevelsPerSide;out.sequenceFlags=feed.sequenceFlags;
    out.sourceGeneration=feed.sourceGeneration;out.sessionEpoch=feed.sessionEpoch;
    out.lastUpdateId=feed.lastUpdateId;out.freshnessMonotonicNs=feed.freshnessMonotonicNs;
    out.lowestBidPriceRaw=feed.lowestBidPriceRaw;out.highestAskPriceRaw=feed.highestAskPriceRaw;
    out.lowestProtectedBidPriceRaw=feed.lowestProtectedBidPriceRaw;
    out.highestProtectedAskPriceRaw=feed.highestProtectedAskPriceRaw;
    out.state=static_cast<BinaryMarketDepthState>(feed.state);
    out.failure=static_cast<std::uint8_t>(feed.failure);out.snapshotAligned=feed.snapshotAligned?1u:0u;
    out.liquidityPopulation=static_cast<std::uint8_t>(feed.liquidityPopulation);
    out.nativeEvidence=feed.nativeEvidence;
    return out;
}

[[nodiscard]] inline cxet::market::DepthCoverage decodeBinaryMarketDepthCoverage(const BinaryMarketDepthCoverage& feed) noexcept {
    cxet::market::DepthCoverage out{};
    out.sourceId=feed.sourceId;out.sequenceDomain=feed.sequenceDomain;
    out.maximumLevelsPerSide=feed.maximumLevelsPerSide;out.sequenceFlags=feed.sequenceFlags;
    out.sourceGeneration=feed.sourceGeneration;out.sessionEpoch=feed.sessionEpoch;
    out.lastUpdateId=feed.lastUpdateId;out.freshnessMonotonicNs=feed.freshnessMonotonicNs;
    out.lowestBidPriceRaw=feed.lowestBidPriceRaw;out.highestAskPriceRaw=feed.highestAskPriceRaw;
    out.lowestProtectedBidPriceRaw=feed.lowestProtectedBidPriceRaw;
    out.highestProtectedAskPriceRaw=feed.highestProtectedAskPriceRaw;
    out.state=static_cast<cxet::market::DepthSourceState>(feed.state);
    out.failure=static_cast<cxet::market::DepthFailure>(feed.failure);out.snapshotAligned=feed.snapshotAligned!=0u;
    out.liquidityPopulation=static_cast<cxet::market::DepthLiquidityPopulation>(feed.liquidityPopulation);
    out.nativeEvidence=feed.nativeEvidence;
    return out;
}

[[nodiscard]] inline bool validBinaryMarketDepthCoverage(const BinaryMarketDepthCoverage& value) noexcept {
    return value.sourceId!=0u && value.sourceId<=cxet::market::kMaximumDepthSourceCapacity &&
        value.maximumLevelsPerSide<=cxet::market::kMaximumDepthLevelsPerSide &&
        (value.sequenceFlags&~std::uint32_t{cxet::market::DepthEventSourcePosition})==0u &&
        value.lowestBidPriceRaw>=0 && value.highestAskPriceRaw>=0 &&
        value.lowestProtectedBidPriceRaw>=0 && value.highestProtectedAskPriceRaw>=0 &&
        value.state<=BinaryMarketDepthState::Gap && value.failure<=8u &&
        value.snapshotAligned<=1u && value.liquidityPopulation<=3u && value.reserved32==0u &&
        cxet::api::market::validMarketNativeEvidence(value.nativeEvidence);
}

[[nodiscard]] inline bool validBinaryMarketDepthCoveragePayload(const BinaryMarketDepthCoveragePayload& value) noexcept {
    if (value.totalCoverageCount == 0u ||
        value.totalCoverageCount > cxet::market::kMaximumDepthSourceCapacity ||
        value.partCount == 0u ||
        static_cast<std::uint64_t>(value.partCount - 1u) * 5u >= value.totalCoverageCount ||
        value.totalCoverageCount > static_cast<std::uint64_t>(value.partCount) * 5u ||
        value.partIndex >= value.partCount ||
        value.coverageOffset != static_cast<std::uint32_t>(value.partIndex) * 5u ||
        value.coverageOffset >= value.totalCoverageCount ||
        value.coverageCount == 0u || value.coverageCount > 5u ||
        value.coverageCount != (value.totalCoverageCount - value.coverageOffset < 5u
                                    ? value.totalCoverageCount - value.coverageOffset
                                    : 5u) ||
        value.transactionPartCount == 0u ||
        value.transactionPartCount > cxet::market::kMaximumDepthTransactionParts) {
        return false;
    }
    for (std::size_t i=0u;i<value.coverage.size();++i)
        if (i<value.coverageCount ? !validBinaryMarketDepthCoverage(value.coverage[i]) :
            value.coverage[i]!=BinaryMarketDepthCoverage{}) return false;
    return true;
}

[[nodiscard]] inline bool binaryMarketRecordIsSourceHealth(
    const BinaryMarketRecord& record) noexcept {
    const auto& h=record.header;
    if(!validBinaryMarketChannel(h.channel)) return false;
    if(binaryMarketDepthRecordIsComposedCut(record)) {
        const auto state=binaryMarketPayload<BinaryMarketDepthChunkPayload>(&record)->state;
        return state==BinaryMarketDepthState::Gap || state==BinaryMarketDepthState::Empty;
    }
    if((h.sourceFlags&(BinaryMarketSourceStale|BinaryMarketSourceDegraded|
                      BinaryMarketSourceSequenceGap))!=0u) return true;
    if(h.channel!=BinaryMarketChannel::Depth ||
       (h.flags&BinaryMarketRecordDepthState)==0u ||
       h.payloadBytes!=sizeof(BinaryMarketDepthChunkPayload)) return false;
    const auto& p=*binaryMarketPayload<BinaryMarketDepthChunkPayload>(&record);
    return p.frameKind==BinaryMarketDepthFrameKind::State &&
           p.state==BinaryMarketDepthState::Gap;
}

enum class BinaryMarketStopReason : std::uint8_t {
    Requested = 1u,
    Duration = 2u,
    Quota = 3u,
    ParserDisconnected = 4u,
    Error = 5u,
};

struct alignas(64) BinaryMarketManifest final {
    std::uint32_t magic{kBinaryMarketManifestMagic};
    std::uint16_t headerBytes{0u};
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
    std::uint16_t headerBytes{0u};
    std::uint32_t recordBytes{0u};
    std::uint32_t recordCount{0u};
    std::uint32_t recordsCrc32c{0u};
    std::uint32_t headerCrc32c{0u};
    std::array<std::byte, 40u> reserved{};
};

struct alignas(64) BinaryMarketSegmentHeader final {
    std::uint32_t magic{kBinaryMarketSegmentMagic};
    std::uint16_t headerBytes{0u};
    std::uint32_t recordBytes{0u};
    std::uint32_t segmentNumber{0u};
    std::uint64_t producerEpoch{0u};
    std::uint64_t recordCount{0u};
    std::int64_t firstReceiveNs{0};
    std::int64_t lastReceiveNs{0};
    std::uint16_t shardIndex{0u};
    std::uint16_t reserved16{0u};
    std::uint32_t blocksCrc32c{0u};
    std::uint32_t headerCrc32c{0u};
    std::uint64_t storedBlockBytes{0u};
    std::uint32_t blockCount{0u};
    std::uint32_t codecId{kBinaryMarketCompactSegmentCodecId};
    std::array<std::byte, 48u> reserved{};
};

struct BinaryMarketBlockHeader final {
    std::uint32_t magic{kBinaryMarketBlockMagic};
    std::uint16_t headerBytes{64u};
    std::uint32_t blockNumber{0u};
    std::uint32_t recordCount{0u};
    std::uint32_t rawBytes{0u};
    std::uint32_t encodedBytes{0u};
    std::uint32_t rawCrc32c{0u};
    std::uint32_t encodedCrc32c{0u};
    std::uint64_t firstShardSequence{0u};
    std::uint64_t lastShardSequence{0u};
    std::uint32_t headerCrc32c{0u};
    BinaryMarketChannel channel{BinaryMarketChannel::BookTicker};
    BinaryMarketBlockStream stream{BinaryMarketBlockStream::BookTicker};
    ByteBlockCodecMode codecMode{ByteBlockCodecMode::Lz4Fast};
    std::array<std::byte, 9u> reserved{};
};

enum BinaryMarketBlockFlags : std::uint8_t {
    BinaryMarketBlockNone = 0u,
    BinaryMarketBlockHasHealthFence = 1u << 0u,
    BinaryMarketBlockHasDepthRebase = 1u << 1u,
};

struct BinaryMarketBlockDirectoryEntry final {
    std::uint64_t offset{0u};
    std::uint64_t firstMonotonicNs{0u};
    std::uint64_t lastMonotonicNs{0u};
    std::uint64_t firstShardSequence{0u};
    std::uint64_t lastShardSequence{0u};
    std::int64_t firstReceiveNs{0};
    std::int64_t lastReceiveNs{0};
    std::uint32_t blockNumber{0u};
    std::uint32_t encodedBytes{0u};
    std::uint32_t recordCount{0u};
    BinaryMarketChannel channel{BinaryMarketChannel::BookTicker};
    BinaryMarketBlockStream stream{BinaryMarketBlockStream::BookTicker};
    ByteBlockCodecMode codecMode{ByteBlockCodecMode::Lz4Fast};
    std::uint8_t flags{BinaryMarketBlockNone};
};

struct alignas(64) BinaryMarketSegmentFooter final {
    std::uint32_t magic{kBinaryMarketSegmentFooterMagic};
    std::uint16_t footerBytes{0u};
    std::uint32_t segmentNumber{0u};
    std::uint32_t blocksCrc32c{0u};
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
    BinaryMarketIndexHasHealthyCompatible = 1u << 2u,
    BinaryMarketIndexHasObserved = 1u << 3u,
    BinaryMarketIndexHasIncompatibleMarket = 1u << 4u,
    BinaryMarketIndexHasDepthRebase = 1u << 5u,
};

inline constexpr std::uint32_t kBinaryMarketKnownIndexFlags =
    BinaryMarketIndexHasGap |
    BinaryMarketIndexHasRecordedOnly |
    BinaryMarketIndexHasHealthyCompatible |
    BinaryMarketIndexHasObserved |
    BinaryMarketIndexHasIncompatibleMarket |
    BinaryMarketIndexHasDepthRebase;

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
    std::uint64_t initialGeneration{0u};
    std::uint64_t finalGeneration{0u};
    std::uint64_t initialDirectoryRevision{0u};
    std::uint64_t finalDirectoryRevision{0u};
    std::uint32_t priorDepthRebaseSegment{0u};
    std::uint32_t priorDepthRebaseBlock{0u};
    std::uint32_t lastDepthRebaseSegment{0u};
    std::uint32_t lastDepthRebaseBlock{0u};
    std::uint32_t flags{BinaryMarketIndexNone};
    std::uint16_t shardIndex{0u};
    BinaryMarketChannel channel{BinaryMarketChannel::BookTicker};
    BinaryMarketCompatibility compatibility{
        BinaryMarketCompatibility::Unavailable};
    std::uint16_t initialAvailableMask{0u};
    std::uint16_t initialUnhealthyMask{0u};
    std::uint16_t initialAdminUnhealthyMask{0u};
    std::uint16_t initialRecoveryPendingMask{0u};
    std::uint16_t finalAvailableMask{0u};
    std::uint16_t finalUnhealthyMask{0u};
    std::uint16_t finalAdminUnhealthyMask{0u};
    std::uint16_t finalRecoveryPendingMask{0u};
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
    if (!validBinaryMarketRecordChannel(header.channel)) return false;
    const auto stream = binaryMarketBlockStreamForRecord(header);
    if (header.payloadBytes != binaryMarketStreamPayloadBytes(stream)) return false;
    const bool lifecycle = header.channel == BinaryMarketChannel::SourceLifecycle;
    if (header.channel == BinaryMarketChannel::BookTicker) {
        if (!cxet::api::market::validMarketNativeEvidence(
                binaryMarketPayload<BinaryMarketBookTickerPayload>(&record)->nativeEvidence)) return false;
    } else if (header.channel == BinaryMarketChannel::Trade) {
        const auto& payload = *binaryMarketPayload<BinaryMarketTradePayload>(&record);
        if (payload.reserved != decltype(payload.reserved){} ||
            !cxet::api::market::validMarketNativeEvidence(payload.nativeEvidence)) return false;
    } else if (header.channel == BinaryMarketChannel::Liquidation) {
        if (binaryMarketPayload<BinaryMarketLiquidationPayload>(&record)->reserved !=
            decltype(BinaryMarketLiquidationPayload::reserved){}) return false;
    } else if (header.channel == BinaryMarketChannel::PriceLimit) {
        if (binaryMarketPayload<BinaryMarketPriceLimitPayload>(&record)->reserved !=
            decltype(BinaryMarketPriceLimitPayload::reserved){}) return false;
    } else if (stream == BinaryMarketBlockStream::DepthCoverage) {
        const auto& payload = *binaryMarketPayload<BinaryMarketDepthCoveragePayload>(&record);
        if (!validBinaryMarketDepthCoveragePayload(payload) ||
            header.eventOrdinal != payload.partIndex ||
            header.sourceFlags != (header.exchangeTimestampNs == 0 ? BinaryMarketSourceExchangeTimestampMissing : BinaryMarketSourceHealthy) ||
            (header.flags & (BinaryMarketRecordDepthSnapshot | BinaryMarketRecordDepthDelta | BinaryMarketRecordDepthState)) != 0u ||
            (header.flags & (BinaryMarketRecordDepthHasCoverage | BinaryMarketRecordDepthSourcePosition |
                             BinaryMarketRecordTraderReplayCompatible)) !=
                (BinaryMarketRecordDepthHasCoverage | BinaryMarketRecordDepthSourcePosition | BinaryMarketRecordTraderReplayCompatible) ||
            (((header.flags & BinaryMarketRecordDepthTransactionEnd) != 0u) != (payload.partIndex + 1u == payload.partCount))) return false;
    } else if (header.channel == BinaryMarketChannel::Depth) {
        const auto& payload = *binaryMarketPayload<BinaryMarketDepthChunkPayload>(&record);
        if (payload.reserved8 != 0u || payload.reserved16 != 0u ||
            !validBinaryMarketDepthNativeState(record) ||
            !cxet::api::market::validMarketNativeEvidence(payload.nativeEvidence)) return false;
    }
    if (header.channel != BinaryMarketChannel::Depth &&
        (header.flags & (BinaryMarketRecordDepthCoverage | BinaryMarketRecordDepthHasCoverage |
                         BinaryMarketRecordDepthTransactionEnd | BinaryMarketRecordDepthSourcePosition)) != 0u) return false;
    if ((header.flags & BinaryMarketRecordDepthSourcePosition) != 0u &&
        (header.nativeIdentityShape != 0u || header.nativeIdentityFirst != 0u || header.nativeIdentitySecond != 0u)) return false;
    if (lifecycle) {
        const auto* p = binaryMarketPayload<BinaryMarketSourceLifecyclePayload>(&record);
        if (p->directoryRevision == 0u ||
            p->cause > BinaryMarketSourceTransitionCause::NativeClosed ||
            (p->cause != BinaryMarketSourceTransitionCause::Subscription &&
             (p->kind != BinaryMarketSourceLifecycleKind::DirectoryChanged || p->channelMaskBefore != p->channelMaskAfter ||
              p->affectedChannel == 0u || p->affectedChannel > 3u || p->requestSequence != 0u)) ||
            (p->cause != BinaryMarketSourceTransitionCause::Subscription &&
             (p->source.configuredChannelMask & binaryMarketChannelBit(static_cast<BinaryMarketChannel>(p->affectedChannel))) == 0u) ||
            p->kind < BinaryMarketSourceLifecycleKind::Added || p->kind > BinaryMarketSourceLifecycleKind::DirectoryChanged ||
            p->source.sourceId != header.sourceId || p->source.initialSourceGeneration != header.sourceGeneration ||
            p->source.sourceId == 0u || !validBinaryMarketSource(p->source, p->source.sourceId - 1u) ||
            (p->channelMaskBefore & ~std::uint16_t{0xffu}) != 0u || p->channelMaskAfter != p->source.availableChannelMask ||
            (p->affectedChannel != 0u && !validBinaryMarketChannel(static_cast<BinaryMarketChannel>(p->affectedChannel))) ||
            (p->previousSourceGeneration == 0u && p->kind != BinaryMarketSourceLifecycleKind::Added) ||
            (header.flags & BinaryMarketRecordTraderReplayCompatible) == 0u || header.sourceFlags != 0u || header.nativeIdentityShape != 0u) {
            return false;
        }
    }
    return header.sourceId != 0u && header.sourceGeneration != 0u &&
           (lifecycle || header.sessionEpoch != 0u) && header.eventSequence != 0u &&
           (lifecycle || header.frameSequence != 0u) &&
           (((header.flags & BinaryMarketRecordSourceLifecycle) != 0u) == lifecycle) &&
           header.exchangeTimestampNs >= 0 &&
           (((header.flags &
              BinaryMarketRecordExchangeTimestampMissing) != 0u) ==
            (header.exchangeTimestampNs == 0)) &&
           header.receiveRealtimeNs > 0 && header.receiveMonotonicNs != 0u &&
           header.shardSequence != 0u &&
           header.payloadBytes != 0u &&
           header.payloadBytes <= kBinaryMarketPayloadBytes &&
           (header.nativeIdentityShape == 0u
                ? header.nativeIdentityFirst == 0u && header.nativeIdentitySecond == 0u
                : header.nativeIdentityShape == 1u
                      ? header.nativeIdentityFirst != 0u && header.nativeIdentitySecond == 0u
                      : header.nativeIdentityShape == 2u ||
                            (header.nativeIdentityShape == 4u &&
                             header.channel == BinaryMarketChannel::Trade &&
                             header.nativeIdentitySecond == 0u)) &&
           (header.flags & ~kBinaryMarketKnownRecordFlags) == 0u &&
           (header.sourceFlags & ~kBinaryMarketKnownSourceFlags) == 0u &&
           (((header.sourceFlags &
              BinaryMarketSourceExchangeTimestampMissing) == 0u) ||
            ((header.flags &
              BinaryMarketRecordExchangeTimestampMissing) != 0u)) &&
           header.reserved == 0u;
}

static_assert(sizeof(BinaryMarketRecordHeader) == 104u);
static_assert(offsetof(BinaryMarketRecordHeader, channel) == 94u);
static_assert(offsetof(BinaryMarketRecordHeader, eventOrdinal) == 96u);
static_assert(offsetof(BinaryMarketRecord, payload) == 104u);
static_assert(sizeof(BinaryMarketDepthLevel) == 24u);
static_assert(sizeof(BinaryMarketDepthChunkPayload) == kBinaryMarketPayloadBytes);
static_assert(sizeof(BinaryMarketManifest) == 192u);
static_assert(offsetof(BinaryMarketManifest, producerEpoch) == 8u);
static_assert(offsetof(BinaryMarketManifest, manifestCrc32c) == 132u);
static_assert(sizeof(BinaryMarketTradePayload) == 80u);
static_assert(sizeof(BinaryMarketBookTickerPayload) == 88u);
static_assert(sizeof(BinaryMarketDepthCoverage) == 144u);
static_assert(sizeof(BinaryMarketDepthCoveragePayload) == 736u);
static_assert(sizeof(BinaryMarketRecord) == 1024u);
static_assert(sizeof(BinaryMarketTableHeader) == 64u);
static_assert(sizeof(BinaryMarketSegmentHeader) == 128u);
static_assert(sizeof(BinaryMarketBlockHeader) == 64u);
static_assert(sizeof(BinaryMarketBlockDirectoryEntry) == 72u);
static_assert(sizeof(BinaryMarketSegmentFooter) == 64u);
static_assert(sizeof(BinaryMarketIndexEntry) == 136u);
static_assert(std::is_trivially_copyable_v<BinaryMarketManifest>);
static_assert(std::is_trivially_copyable_v<BinaryMarketSource>);
static_assert(std::is_trivially_copyable_v<BinaryMarketRecord>);
static_assert(std::is_trivially_copyable_v<BinaryMarketBlockDirectoryEntry>);
static_assert(std::is_trivially_copyable_v<BinaryMarketIndexEntry>);
static_assert(std::is_trivially_copyable_v<BinaryMarketGap>);

}  // namespace hftrec::corpus
