#pragma once
#include "hftrec/CorpusContract/BinaryMarketCorpusFormat.hpp"

namespace hftrec::corpus {
struct BinaryMarketSourceState final {
    BinaryMarketSource metadata{};
    std::uint64_t generation{0u};
    std::uint16_t availableMask{0u};
    std::uint64_t directoryRevision{0u};
    std::uint16_t unhealthyMask{0u};
};
inline BinaryMarketSourceState initialBinaryMarketSourceState(const BinaryMarketSource& source) noexcept {
    return {source,source.initiallyPresent?source.initialSourceGeneration:0u,
        source.initiallyPresent?source.availableChannelMask:std::uint16_t{0u}};
}
inline BinaryMarketSource effectiveBinaryMarketSource(const BinaryMarketSourceState& state) noexcept {
    auto source=state.metadata;source.initiallyPresent=state.availableMask!=0u?1u:0u;
    source.initialSourceGeneration=state.generation;source.availableChannelMask=state.availableMask;
    source.traderReplayChannelMask&=state.availableMask;
    for(std::size_t c=0u;c<kBinaryMarketChannelCount;++c)
        if((state.availableMask&(std::uint16_t{1u}<<c))==0u) source.compatibility[c]=BinaryMarketCompatibility::Unavailable;
    return source;
}
inline bool sameBinaryMarketSourceIdentity(const BinaryMarketSource& left,const BinaryMarketSource& right) noexcept {
    return left.sourceId==right.sourceId && left.canonicalSymbolId==right.canonicalSymbolId &&
        left.venueId==right.venueId && left.marketRaw==right.marketRaw &&
        left.venueBytes==right.venueBytes && left.marketBytes==right.marketBytes &&
        left.canonicalSymbolBytes==right.canonicalSymbolBytes && left.nativeSymbolBytes==right.nativeSymbolBytes &&
        left.venue==right.venue && left.market==right.market && left.canonicalSymbol==right.canonicalSymbol &&
        left.nativeSymbol==right.nativeSymbol;
}
inline bool sameBinaryMarketSourceRules(const BinaryMarketSource& left,const BinaryMarketSource& right) noexcept {
    return left.tickSizeRaw==right.tickSizeRaw && left.stepSizeRaw==right.stepSizeRaw &&
        left.contractBaseQtyRaw==right.contractBaseQtyRaw && left.priceBasisQtyRaw==right.priceBasisQtyRaw &&
        left.canonicalStepSizeRaw==right.canonicalStepSizeRaw && left.executionQuantityStepRaw==right.executionQuantityStepRaw &&
        left.quantityConversion==right.quantityConversion &&
        left.instrumentMetadataFlags==right.instrumentMetadataFlags && left.priceScale==right.priceScale &&
        left.quantityScale==right.quantityScale && left.economicBaseAssetId==right.economicBaseAssetId &&
        left.quoteAssetId==right.quoteAssetId && left.assetDomainRaw==right.assetDomainRaw && left.marketKindRaw==right.marketKindRaw;
}
// Per-source lifecycle is producer ordered on the same shard as its market
// records. This state never infers membership from stale data or missing BBO.
inline bool advanceBinaryMarketSourceState(const BinaryMarketRecord& record,
                                           BinaryMarketSourceState& state) noexcept {
    const auto& h=record.header;
    if(h.channel!=BinaryMarketChannel::SourceLifecycle) {
        const auto bit=binaryMarketChannelBit(h.channel);
        if(state.generation!=h.sourceGeneration) return false;
        // A native health notification may describe a feed already unavailable
        // after a captured membership boundary. It never admits market data.
        if(binaryMarketRecordIsSourceHealth(record)) {
            state.unhealthyMask|=bit;return true;
        }
        const bool valid=(state.availableMask&bit)!=0u &&
            state.metadata.compatibility[binaryMarketChannelIndex(h.channel)]!=BinaryMarketCompatibility::Unavailable &&
            ((h.flags&BinaryMarketRecordTraderReplayCompatible)==0u ||
             state.metadata.compatibility[binaryMarketChannelIndex(h.channel)]==BinaryMarketCompatibility::ExactTraderReplay);
        if(valid && (h.flags&BinaryMarketRecordTraderReplayCompatible)!=0u)
            state.unhealthyMask&=static_cast<std::uint16_t>(~bit);
        return valid;
    }
    const auto& p=*binaryMarketPayload<BinaryMarketSourceLifecyclePayload>(&record);
    const bool nativeHealth=p.cause!=BinaryMarketSourceTransitionCause::Subscription;
    if(!sameBinaryMarketSourceIdentity(state.metadata,p.source)) return false;
    if(nativeHealth) {
        // The actual frame arrival precedes this shared directory sample; a
        // sibling selection may already have changed. Only native lane health
        // and its proven generation belong to this administrative marker.
        if(p.kind!=BinaryMarketSourceLifecycleKind::DirectoryChanged ||
            !sameBinaryMarketSourceRules(state.metadata,p.source) ||
            h.sourceGeneration<state.generation ||
            (h.sourceGeneration!=state.generation && p.cause!=BinaryMarketSourceTransitionCause::NativeRecovered) ||
            p.channelMaskBefore!=p.channelMaskAfter) return false;
        const auto bit=binaryMarketChannelBit(static_cast<BinaryMarketChannel>(p.affectedChannel));
        if(binaryMarketTransitionIsUnhealthy(p.cause)) state.unhealthyMask|=bit;
        else if(p.cause==BinaryMarketSourceTransitionCause::NativeRecovered) state.unhealthyMask&=static_cast<std::uint16_t>(~bit);
        state.generation=h.sourceGeneration;
        return true;
    }
    if(p.previousSourceGeneration!=state.generation || p.channelMaskBefore!=state.availableMask ||
       h.sourceGeneration<state.generation || p.directoryRevision<=state.directoryRevision) return false;
    switch(p.kind) {
        case BinaryMarketSourceLifecycleKind::Added:
            if(p.channelMaskAfter==0u || (p.channelMaskAfter&~p.channelMaskBefore)==0u ||
               (p.channelMaskBefore&~p.channelMaskAfter)!=0u) return false;
            break;
        case BinaryMarketSourceLifecycleKind::Removed:
            if(h.sourceGeneration!=state.generation || p.channelMaskBefore==0u || (p.channelMaskBefore&~p.channelMaskAfter)==0u ||
               (p.channelMaskAfter&~p.channelMaskBefore)!=0u) return false;
            break;
        case BinaryMarketSourceLifecycleKind::Readded:
            if(p.channelMaskAfter==0u || state.generation==0u ||
               (p.channelMaskAfter&~p.channelMaskBefore)==0u || (p.channelMaskBefore&~p.channelMaskAfter)!=0u) return false;
            break;
        case BinaryMarketSourceLifecycleKind::DirectoryChanged:
            if(p.channelMaskBefore!=p.channelMaskAfter || state.generation==0u) return false;
            break;
    }
    const auto changedMask=static_cast<std::uint16_t>(p.channelMaskBefore^p.channelMaskAfter);
    if(h.sourceGeneration!=state.generation) state.unhealthyMask=0u;
    else state.unhealthyMask&=static_cast<std::uint16_t>(~changedMask);
    state.metadata=p.source;state.generation=h.sourceGeneration;state.availableMask=p.channelMaskAfter;state.directoryRevision=p.directoryRevision;
    return true;
}
} // namespace hftrec::corpus
