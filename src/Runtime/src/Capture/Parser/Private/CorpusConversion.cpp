#include "CorpusConversion.hpp"

#include <algorithm>
#include <limits>

namespace hftrec::capture::detail {
namespace parser = hft_parser::ipc;

[[nodiscard]] static bool fixedTextValid(const char* data,
                                  std::size_t capacity,
                                  std::uint8_t bytes) noexcept {
    if (!data || bytes == 0u || bytes > capacity) return false;
    for (std::size_t index = static_cast<std::size_t>(bytes);
         index < capacity; ++index) {
        if (data[index] != '\0') return false;
    }
    return true;
}

[[nodiscard]] static bool validCompatibility(
    parser::MarketCaptureCompatibility value) noexcept {
    return value == parser::MarketCaptureCompatibility::Unavailable ||
        value == parser::MarketCaptureCompatibility::ExactTraderReplay ||
        value == parser::MarketCaptureCompatibility::RecordedOnly;
}

[[nodiscard]] bool validSourceDescriptor(
    const parser::MarketCaptureSourceDescriptor& source,
    std::size_t index) noexcept {
    if (source.market.sourceId != index + 1u ||
        source.market.sourceGeneration == 0u || source.market.venueId == 0u ||
        source.market.priceScale > 18u || source.market.quantityScale > 18u ||
        !fixedTextValid(source.market.venueCode.data(),
                        source.market.venueCode.size(),
                        source.market.venueCodeBytes) ||
        !fixedTextValid(source.market.canonicalSymbol.data(),
                        source.market.canonicalSymbol.size(),
                        source.market.canonicalSymbolBytes) ||
        !fixedTextValid(source.market.nativeSymbol.data(),
                        source.market.nativeSymbol.size(),
                        source.market.nativeSymbolBytes) ||
        !fixedTextValid(source.marketCode.data(), source.marketCode.size(),
                        source.marketCodeBytes) ||
        source.market.listingReserved != decltype(source.market.listingReserved){} ||
        source.market.identityReserved != decltype(source.market.identityReserved){} ||
        !parser::validMarketDirectoryListingIdentity(source.market) ||
        (source.instrument.flags &
         ~parser::kMarketCaptureInstrumentMetadataFlags) != 0u ||
        source.instrument.reserved != 0u ||
        !parser::validMarketCaptureQuantityConversion(source.instrument.quantityConversion) ||
        source.instrument.canonicalStepSizeRaw < 0 || source.instrument.executionQuantityStepRaw < 0 ||
        (((source.instrument.flags & parser::MarketCaptureInstrumentCanonicalStepSize) != 0u) !=
         (source.instrument.canonicalStepSizeRaw > 0)) ||
        (((source.instrument.flags & parser::MarketCaptureInstrumentExecutionQuantityStep) != 0u) !=
         (source.instrument.executionQuantityStepRaw > 0)) ||
        (((source.instrument.flags &
           parser::MarketCaptureInstrumentStepSize) != 0u) !=
         (source.instrument.stepSizeRaw > 0)) ||
        (((source.instrument.flags &
           parser::MarketCaptureInstrumentContractBaseQty) != 0u) !=
         (source.instrument.contractBaseQtyRaw > 0)) ||
        source.descriptorReserved != decltype(source.descriptorReserved){} ||
        source.reserved != 0u ||
        (source.configuredChannelMask & ~std::uint16_t{0x0007u}) != 0u ||
        (source.availableChannelMask & ~source.configuredChannelMask) != 0u ||
        (source.traderReplayChannelMask &
         ~source.availableChannelMask) != 0u) {
        return false;
    }
    for (std::size_t channel=0u;channel<source.instrument.quantityConversion.kinds.size();++channel)
        if (source.instrument.quantityConversion.kinds[channel]!=parser::MarketCaptureQuantityKind::Unknown &&
            (source.configuredChannelMask&(std::uint16_t{1u}<<channel))==0u) return false;
    for (std::size_t channel = 0u;
         channel < parser::kMarketCaptureChannelCount; ++channel) {
        const std::uint16_t bit = std::uint16_t{1u} << channel;
        const auto compatibility = source.compatibility[channel];
        if (!validCompatibility(compatibility)) return false;
        if ((source.availableChannelMask & bit) == 0u) {
            if (compatibility !=
                parser::MarketCaptureCompatibility::Unavailable) return false;
        } else if ((source.traderReplayChannelMask & bit) != 0u) {
            if (compatibility !=
                parser::MarketCaptureCompatibility::ExactTraderReplay) {
                return false;
            }
        } else if (compatibility !=
                   parser::MarketCaptureCompatibility::RecordedOnly) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] static corpus::BinaryMarketCompatibility convertCompatibility(
    parser::MarketCaptureCompatibility value) noexcept {
    switch (value) {
        case parser::MarketCaptureCompatibility::ExactTraderReplay:
            return corpus::BinaryMarketCompatibility::ExactTraderReplay;
        case parser::MarketCaptureCompatibility::RecordedOnly:
            return corpus::BinaryMarketCompatibility::RecordedOnly;
        case parser::MarketCaptureCompatibility::Unavailable:
            break;
    }
    return corpus::BinaryMarketCompatibility::Unavailable;
}

[[nodiscard]] corpus::BinaryMarketSource convertSource(
    const parser::MarketCaptureSourceDescriptor& input) noexcept {
    corpus::BinaryMarketSource output{};
    output.sourceId = input.market.sourceId;
    output.canonicalSymbolId = input.market.canonicalSymbolId;
    output.initialSourceGeneration = input.market.sourceGeneration;
    output.tickSizeRaw = input.market.tickSizeRaw;
    output.stepSizeRaw = input.instrument.stepSizeRaw;
    output.contractBaseQtyRaw = input.instrument.contractBaseQtyRaw;
    output.canonicalStepSizeRaw = input.instrument.canonicalStepSizeRaw;
    output.executionQuantityStepRaw = input.instrument.executionQuantityStepRaw;
    const auto& conversion=input.instrument.quantityConversion;
    auto& target=output.quantityConversion;
    for (std::size_t channel=0u;channel<target.kinds.size();++channel)
        target.kinds[channel]=static_cast<corpus::BinaryMarketQuantityKind>(conversion.kinds[channel]);
    target.reserved=conversion.reserved;
    target.canonicalBaseMultiplier=conversion.canonicalBaseMultiplier;
    target.nativeBaseMultiplier=conversion.nativeBaseMultiplier;
    target.nativeContractBaseQtyRaw=conversion.nativeContractBaseQtyRaw;
    target.nativeLotBaseQtyRaw=conversion.nativeLotBaseQtyRaw;
    output.priceBasisQtyRaw = input.market.priceBasisQtyRaw;
    output.economicBaseAssetId = input.market.economicBaseAssetId;
    output.quoteAssetId = input.market.quoteAssetId;
    output.venueId = input.market.venueId;
    output.directoryFlags = input.market.flags;
    output.configuredChannelMask = input.configuredChannelMask;
    output.availableChannelMask = input.availableChannelMask;
    output.traderReplayChannelMask = input.traderReplayChannelMask;
    output.instrumentMetadataFlags = input.instrument.flags;
    output.marketRaw = input.market.marketRaw;
    output.marketKindRaw = static_cast<std::uint8_t>(input.market.marketKind);
    output.assetDomainRaw = input.market.assetDomainRaw;
    output.priceScale = input.market.priceScale;
    output.quantityScale = input.market.quantityScale;
    output.venueBytes = input.market.venueCodeBytes;
    output.marketBytes = input.marketCodeBytes;
    output.canonicalSymbolBytes = input.market.canonicalSymbolBytes;
    output.nativeSymbolBytes = input.market.nativeSymbolBytes;
    output.venue = input.market.venueCode;
    output.market = input.marketCode;
    output.canonicalSymbol = input.market.canonicalSymbol;
    output.nativeSymbol = input.market.nativeSymbol;
    for (std::size_t channel = 0u;
         channel < parser::kMarketCaptureChannelCount; ++channel) {
        output.compatibility[channel] =
            convertCompatibility(input.compatibility[channel]);
    }
    return output;
}

[[nodiscard]] static bool tradeSideValid(hft_parser::market::TradeSide side) noexcept {
    return side == hft_parser::market::TradeSide::Unknown ||
        side == hft_parser::market::TradeSide::BuyerAggressor ||
        side == hft_parser::market::TradeSide::SellerAggressor;
}

[[nodiscard]] static bool copyRecordPayload(
    const parser::MarketCaptureRecord& input,
    corpus::BinaryMarketRecord& output) noexcept {
    using ParserChannel = parser::MarketCaptureChannel;
    switch (input.header.channel) {
        case ParserChannel::Membership: {
            if (input.header.payloadBytes != sizeof(parser::MarketCaptureMembershipPayload)) return false;
            const auto& source = *parser::marketCapturePayload<parser::MarketCaptureMembershipPayload>(&input);
            if (!validSourceDescriptor(source.source, input.header.sourceId - 1u) ||
                source.source.market.sourceGeneration != input.header.sourceGeneration ||
                source.sourceGeneration != input.header.sourceGeneration ||
                source.sessionEpoch != input.header.sessionEpoch ||
                source.canonicalSymbolId != source.source.market.canonicalSymbolId ||
                source.venueId != source.source.market.venueId ||
                source.marketRaw != source.source.market.marketRaw ||
                source.channelMaskAfter != source.source.availableChannelMask ||
                source.reserved != decltype(source.reserved){} ||
                source.affectedChannel < ParserChannel::BookTicker || source.affectedChannel > ParserChannel::Depth ||
                (source.action != static_cast<std::uint8_t>(parser::MarketCaptureSubscriptionAction::Add) &&
                 source.action != static_cast<std::uint8_t>(parser::MarketCaptureSubscriptionAction::Remove))) return false;
            auto& target = *corpus::binaryMarketPayload<corpus::BinaryMarketSourceLifecyclePayload>(&output);
            target.directoryRevision = source.directoryRevision;
            target.previousSourceGeneration = source.previousSourceGeneration;
            target.requestSequence = source.requestSequence;
            target.channelMaskBefore = source.channelMaskBefore;
            target.channelMaskAfter = source.channelMaskAfter;
            target.kind = source.previousSourceGeneration != 0u && source.channelMaskBefore == source.channelMaskAfter
                ? corpus::BinaryMarketSourceLifecycleKind::DirectoryChanged
                : source.action == static_cast<std::uint8_t>(parser::MarketCaptureSubscriptionAction::Remove)
                ? corpus::BinaryMarketSourceLifecycleKind::Removed
                : source.previousSourceGeneration == 0u ? corpus::BinaryMarketSourceLifecycleKind::Added
                : corpus::BinaryMarketSourceLifecycleKind::Readded;
            target.affectedChannel = static_cast<std::uint8_t>(source.affectedChannel);
            target.reason = source.status;
            switch(source.cause) {
                case parser::MarketCaptureSourceTransitionCause::Subscription: target.cause=corpus::BinaryMarketSourceTransitionCause::Subscription;break;
                case parser::MarketCaptureSourceTransitionCause::NativeStale: target.cause=corpus::BinaryMarketSourceTransitionCause::NativeStale;break;
                case parser::MarketCaptureSourceTransitionCause::NativeDegraded: target.cause=corpus::BinaryMarketSourceTransitionCause::NativeDegraded;break;
                case parser::MarketCaptureSourceTransitionCause::NativeSequenceGap: target.cause=corpus::BinaryMarketSourceTransitionCause::NativeSequenceGap;break;
                case parser::MarketCaptureSourceTransitionCause::NativeRecovered: target.cause=corpus::BinaryMarketSourceTransitionCause::NativeRecovered;break;
                case parser::MarketCaptureSourceTransitionCause::NativeClosed: target.cause=corpus::BinaryMarketSourceTransitionCause::NativeClosed;break;
                default:return false;
            }
            target.source = convertSource(source.source);
            output.header.payloadBytes = sizeof(target);
            return true;
        }
        case ParserChannel::BookTicker: {
            if (input.header.payloadBytes !=
                sizeof(parser::MarketCaptureBookTickerPayload)) return false;
            const auto& source = *parser::marketCapturePayload<
                parser::MarketCaptureBookTickerPayload>(&input);
            const bool bidPresent =
                (input.header.sourceFlags &
                 hft_parser::market::SourceBookTickerBidPresent) != 0u;
            const bool askPresent =
                (input.header.sourceFlags &
                 hft_parser::market::SourceBookTickerAskPresent) != 0u;
            if ((!bidPresent && !askPresent) ||
                (bidPresent
                     ? source.bidPriceRaw <= 0 || source.bidQtyRaw < 0
                     : source.bidPriceRaw != 0 || source.bidQtyRaw != 0) ||
                (askPresent
                     ? source.askPriceRaw <= 0 || source.askQtyRaw < 0
                     : source.askPriceRaw != 0 || source.askQtyRaw != 0) ||
                (bidPresent && askPresent &&
                 source.bidPriceRaw > source.askPriceRaw)) {
                return false;
            }
            auto* target = corpus::binaryMarketPayload<
                corpus::BinaryMarketBookTickerPayload>(&output);
            *target = {source.bidPriceRaw, source.bidQtyRaw,
                       source.askPriceRaw, source.askQtyRaw};
            return true;
        }
        case ParserChannel::Trade: {
            if (input.header.payloadBytes !=
                sizeof(parser::MarketCaptureTradePayload)) return false;
            const auto& source = *parser::marketCapturePayload<
                parser::MarketCaptureTradePayload>(&input);
            const bool replayCompatible =
                (input.header.flags &
                 parser::MarketCaptureRecordTraderReplayCompatible) != 0u;
            if (source.priceRaw <= 0 || source.qtyRaw <= 0 ||
                !tradeSideValid(source.side) ||
                (replayCompatible &&
                 source.side == hft_parser::market::TradeSide::Unknown) ||
                source.reserved != decltype(source.reserved){}) return false;
            auto* target = corpus::binaryMarketPayload<
                corpus::BinaryMarketTradePayload>(&output);
            *target = {source.priceRaw, source.qtyRaw,
                       static_cast<std::uint8_t>(source.side), {}};
            return true;
        }
        case ParserChannel::Depth: {
            if (input.header.payloadBytes !=
                sizeof(parser::MarketCaptureDepthChunkPayload)) return false;
            const auto& source = *parser::marketCapturePayload<
                parser::MarketCaptureDepthChunkPayload>(&input);
            if (source.totalLevelCount >
                std::numeric_limits<std::uint32_t>::max() -
                    (parser::kMarketCaptureDepthLevelsPerRecord - 1u)) {
                return false;
            }
            const std::uint32_t expectedParts =
                source.totalLevelCount == 0u
                    ? 1u
                    : (source.totalLevelCount +
                       parser::kMarketCaptureDepthLevelsPerRecord - 1u) /
                          parser::kMarketCaptureDepthLevelsPerRecord;
            if (source.partCount == 0u ||
                source.partCount != expectedParts ||
                source.partIndex >= source.partCount ||
                source.levelCount >
                    parser::kMarketCaptureDepthLevelsPerRecord ||
                source.levelOffset !=
                    static_cast<std::uint32_t>(source.partIndex) *
                        parser::kMarketCaptureDepthLevelsPerRecord ||
                source.levelOffset > source.totalLevelCount ||
                source.levelCount >
                    source.totalLevelCount - source.levelOffset ||
                (source.frameKind !=
                     parser::MarketCaptureDepthFrameKind::Snapshot &&
                 source.frameKind !=
                     parser::MarketCaptureDepthFrameKind::Delta &&
                 source.frameKind !=
                     parser::MarketCaptureDepthFrameKind::State) ||
                source.state < ::cxet::market::DepthSourceState::Empty ||
                source.state > ::cxet::market::DepthSourceState::Gap ||
                source.failure < ::cxet::market::DepthFailure::None ||
                source.failure >
                    ::cxet::market::DepthFailure::ConsumerBookRejected ||
                source.transactionPartCount == 0u ||
                source.transactionPartIndex >=
                    source.transactionPartCount ||
                source.reserved8 != 0u || source.reserved16 != 0u) {
                return false;
            }
            if ((source.totalLevelCount == 0u) !=
                    (source.levelCount == 0u) ||
                (source.totalLevelCount == 0u &&
                 (source.levelOffset != 0u || source.partCount != 1u))) {
                return false;
            }
            if (source.partIndex + 1u == source.partCount) {
                if (source.levelOffset + source.levelCount !=
                    source.totalLevelCount) return false;
            } else if (source.levelCount !=
                       parser::kMarketCaptureDepthLevelsPerRecord) {
                return false;
            }
            const bool replayCompatible =
                (input.header.flags &
                 parser::MarketCaptureRecordTraderReplayCompatible) != 0u;
            const bool partial =
                source.state == ::cxet::market::DepthSourceState::Partial;
            const bool healthy =
                source.state == ::cxet::market::DepthSourceState::Healthy;
            const bool rebaseInFlight =
                source.state ==
                ::cxet::market::DepthSourceState::RebaseInFlight;
            const bool gap =
                source.state == ::cxet::market::DepthSourceState::Gap;
            const bool snapshot = source.frameKind ==
                parser::MarketCaptureDepthFrameKind::Snapshot;
            const bool delta = source.frameKind ==
                parser::MarketCaptureDepthFrameKind::Delta;
            const bool stateRecord = source.frameKind ==
                parser::MarketCaptureDepthFrameKind::State;
            const bool snapshotFlag =
                (input.header.flags &
                 parser::MarketCaptureRecordDepthSnapshot) != 0u;
            const bool deltaFlag =
                (input.header.flags &
                 parser::MarketCaptureRecordDepthDelta) != 0u;
            const bool stateFlag =
                (input.header.flags &
                 parser::MarketCaptureRecordDepthState) != 0u;
            const bool partialFlag =
                (input.header.flags &
                 parser::MarketCaptureRecordDepthPartial) != 0u;
            const bool rebaseFlag =
                (input.header.flags &
                 parser::MarketCaptureRecordDepthRebase) != 0u;
            if ((!partial && !healthy && !rebaseInFlight && !gap) ||
                snapshotFlag != snapshot || deltaFlag != delta ||
                stateFlag != stateRecord ||
                (static_cast<unsigned>(snapshotFlag) +
                 static_cast<unsigned>(deltaFlag) +
                 static_cast<unsigned>(stateFlag)) != 1u ||
                partialFlag != partial ||
                (replayCompatible !=
                 (partial || healthy || rebaseInFlight)) ||
                ((source.totalLevelCount == 0u) != stateRecord) ||
                ((partial || healthy || rebaseInFlight) &&
                 source.failure != ::cxet::market::DepthFailure::None) ||
                (gap &&
                 source.failure == ::cxet::market::DepthFailure::None) ||
                ((partial || healthy) && !delta) ||
                (rebaseFlag != rebaseInFlight) ||
                (rebaseInFlight && source.totalLevelCount == 0u) ||
                (rebaseInFlight &&
                 (snapshot != (source.transactionPartIndex == 0u))) ||
                (!rebaseInFlight &&
                 (source.transactionPartIndex != 0u ||
                  source.transactionPartCount != 1u)) ||
                (stateRecord && (!gap || replayCompatible || partialFlag ||
                                 rebaseFlag ||
                                 source.sourceReceiveRealtimeNs != 0u)) ||
                (!stateRecord &&
                 (source.sourceReceiveRealtimeNs == 0u ||
                  source.sourceReceiveRealtimeNs >
                      static_cast<std::uint64_t>(
                          std::numeric_limits<std::int64_t>::max()) ||
                  source.sourceFreshnessMonotonicNs == 0u))) {
                return false;
            }
            corpus::BinaryMarketDepthChunkPayload target{};
            target.sequence = {source.sequence.firstUpdateId,
                               source.sequence.lastUpdateId,
                               source.sequence.previousUpdateId};
            target.totalLevelCount = source.totalLevelCount;
            target.levelOffset = source.levelOffset;
            target.partIndex = source.partIndex;
            target.partCount = source.partCount;
            target.levelCount = source.levelCount;
            target.frameKind = static_cast<corpus::BinaryMarketDepthFrameKind>(
                static_cast<std::uint8_t>(source.frameKind));
            target.state = static_cast<corpus::BinaryMarketDepthState>(
                static_cast<std::uint8_t>(source.state));
            target.failure = static_cast<std::uint8_t>(source.failure);
            target.transactionPartIndex = source.transactionPartIndex;
            target.transactionPartCount = source.transactionPartCount;
            target.sourceReceiveRealtimeNs =
                source.sourceReceiveRealtimeNs;
            target.sourceFreshnessMonotonicNs =
                source.sourceFreshnessMonotonicNs;
            for (std::uint16_t index = 0u; index < source.levelCount; ++index) {
                const auto& level = source.levels[index];
                const bool side =
                    level.side == ::cxet::market::DepthSide::Bid ||
                    level.side == ::cxet::market::DepthSide::Ask;
                const bool action =
                    (level.action == ::cxet::market::DepthAction::Upsert &&
                     level.quantityRaw > 0u) ||
                    (level.action == ::cxet::market::DepthAction::Erase &&
                     level.quantityRaw == 0u);
                if (level.priceRaw == 0u || !side || !action ||
                    (snapshot &&
                     level.action != ::cxet::market::DepthAction::Upsert) ||
                    level.reserved16 != 0u || level.reserved32 != 0u) {
                    return false;
                }
                target.levels[index] = {
                    level.priceRaw, level.quantityRaw,
                    static_cast<std::uint8_t>(level.side),
                    static_cast<std::uint8_t>(level.action), 0u, 0u};
            }
            *corpus::binaryMarketPayload<
                corpus::BinaryMarketDepthChunkPayload>(&output) = target;
            return true;
        }

    }
    return false;
}

[[nodiscard]] static std::uint32_t convertRecordFlags(std::uint32_t flags) noexcept {
    std::uint32_t output = corpus::BinaryMarketRecordNone;
    if ((flags & parser::MarketCaptureRecordTraderReplayCompatible) != 0u)
        output |= corpus::BinaryMarketRecordTraderReplayCompatible;
    if ((flags & parser::MarketCaptureRecordBboFreshnessOnly) != 0u)
        output |= corpus::BinaryMarketRecordBboFreshnessOnly;
    if ((flags & parser::MarketCaptureRecordDepthSnapshot) != 0u)
        output |= corpus::BinaryMarketRecordDepthSnapshot;
    if ((flags & parser::MarketCaptureRecordDepthDelta) != 0u)
        output |= corpus::BinaryMarketRecordDepthDelta;
    if ((flags & parser::MarketCaptureRecordDepthPartial) != 0u)
        output |= corpus::BinaryMarketRecordDepthPartial;
    if ((flags & parser::MarketCaptureRecordDepthRebase) != 0u)
        output |= corpus::BinaryMarketRecordDepthRebase;
    if ((flags & parser::MarketCaptureRecordExchangeTimestampMissing) != 0u)
        output |= corpus::BinaryMarketRecordExchangeTimestampMissing;
    if ((flags & parser::MarketCaptureRecordRealtimeRegression) != 0u)
        output |= corpus::BinaryMarketRecordRealtimeRegression;
    if ((flags & parser::MarketCaptureRecordMonotonicNonIncreasing) != 0u)
        output |= corpus::BinaryMarketRecordMonotonicNonIncreasing;
    if ((flags & parser::MarketCaptureRecordExchangeAheadOfReceive) != 0u)
        output |= corpus::BinaryMarketRecordExchangeAheadOfReceive;
    if ((flags & parser::MarketCaptureRecordDepthState) != 0u)
        output |= corpus::BinaryMarketRecordDepthState;
    return output;
}

[[nodiscard]] bool convertRecord(
    const parser::MarketCaptureRecord& input,
    std::uint16_t expectedShard,
    std::span<const parser::MarketCaptureSourceDescriptor> directory,
    corpus::BinaryMarketRecord& output) noexcept {
    if (!parser::validMarketCaptureRecord(input) ||
        input.header.shardIndex != expectedShard ||
        input.header.sourceId > directory.size() ||
        (input.header.sourceFlags & ~hft_parser::market::kSourceFlagsKnown) !=
            0u) {
        return false;
    }
    const auto& source = directory[input.header.sourceId - 1u];
    const std::size_t channelIndex =
        parser::marketCaptureChannelIndex(input.header.channel);
    const auto compatibility = source.compatibility[channelIndex];
    const bool recordCompatible =
        (input.header.flags &
         parser::MarketCaptureRecordTraderReplayCompatible) != 0u;
    if (input.header.channel != parser::MarketCaptureChannel::Membership &&
        (compatibility == parser::MarketCaptureCompatibility::Unavailable ||
        (recordCompatible && compatibility !=
             parser::MarketCaptureCompatibility::ExactTraderReplay))) {
        return false;
    }
    const bool bboOnly =
        (input.header.flags & parser::MarketCaptureRecordBboFreshnessOnly) !=
        0u;
    const std::uint32_t depthFlags =
        parser::MarketCaptureRecordDepthSnapshot |
        parser::MarketCaptureRecordDepthDelta |
        parser::MarketCaptureRecordDepthPartial |
        parser::MarketCaptureRecordDepthRebase |
        parser::MarketCaptureRecordDepthState;
    if ((bboOnly && input.header.channel !=
                        parser::MarketCaptureChannel::BookTicker) ||
        ((input.header.flags & depthFlags) != 0u &&
         input.header.channel != parser::MarketCaptureChannel::Depth) ||
        (input.header.channel == parser::MarketCaptureChannel::Depth &&
         (input.header.flags & (parser::MarketCaptureRecordDepthSnapshot |
                                parser::MarketCaptureRecordDepthDelta |
                                parser::MarketCaptureRecordDepthState)) ==
             0u)) {
        return false;
    }
    const auto payloadTail =
        input.payload.begin() + input.header.payloadBytes;
    if (!std::all_of(payloadTail, input.payload.end(),
                     [](std::byte value) noexcept {
                         return value == std::byte{};
                     })) {
        return false;
    }

    output = {};
    output.header.sourceId = input.header.sourceId;
    output.header.flags = convertRecordFlags(input.header.flags);
    if (input.header.channel == parser::MarketCaptureChannel::Membership)
        output.header.flags |= corpus::BinaryMarketRecordSourceLifecycle;
    output.header.sourceGeneration = input.header.sourceGeneration;
    output.header.sessionEpoch = input.header.sessionEpoch;
    output.header.eventSequence = input.header.eventSequence;
    output.header.frameSequence = input.header.frameSequence;
    output.header.nativeIdentityFirst = input.header.nativeIdentityFirst;
    output.header.nativeIdentitySecond = input.header.nativeIdentitySecond;
    output.header.exchangeTimestampNs = input.header.exchangeTimestampNs;
    output.header.receiveRealtimeNs = input.header.receiveRealtimeNs;
    output.header.receiveMonotonicNs = input.header.receiveMonotonicNs;
    output.header.shardSequence = input.header.shardSequence;
    output.header.payloadBytes = input.header.payloadBytes;
    output.header.shardIndex = input.header.shardIndex;
    output.header.sourceFlags = input.header.sourceFlags;
    output.header.nativeIdentityShape = input.header.nativeIdentityShape;
    output.header.eventOrdinal = input.header.eventOrdinal;
    output.header.channel = input.header.channel == parser::MarketCaptureChannel::Membership
        ? corpus::BinaryMarketChannel::SourceLifecycle : static_cast<corpus::BinaryMarketChannel>(
            static_cast<std::uint8_t>(input.header.channel));
    return copyRecordPayload(input, output) &&
        corpus::validBinaryMarketRecord(output);
}

}  // namespace hftrec::capture::detail
