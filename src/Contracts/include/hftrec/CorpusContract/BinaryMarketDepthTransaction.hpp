#pragma once

#include "hftrec/CorpusContract/BinaryMarketCorpusFormat.hpp"
#include <algorithm>

namespace hftrec::corpus {

// One bounded transaction per producer shard. Root identity is stable;
// only the final native header owns the coverage tail metadata.
struct BinaryMarketDepthTransactionState final {
    BinaryMarketRecordHeader lastNative{};
    std::uint64_t frameSequence{}, firstShardSequence{}, receiveMonotonicNs{};
    std::int64_t receiveRealtimeNs{};
    std::uint32_t totalLevelCount{}, totalCoverageCount{};
    BinaryMarketDepthSequence nativeSequence{};
    cxet::api::market::MarketNativeEvidence nativeEvidence{};
    std::uint64_t sourceReceiveRealtimeNs{}, sourceFreshnessMonotonicNs{};
    BinaryMarketDepthFrameKind frameKind{BinaryMarketDepthFrameKind::Unknown};
    BinaryMarketDepthState nativeState{BinaryMarketDepthState::Empty};
    std::uint8_t failure{};
    std::uint16_t nativeParts{}, nextNative{}, chunkParts{}, nextChunk{}, coverageParts{}, nextCoverage{};
    bool hasCoverage{}, awaitingCoverage{};
    [[nodiscard]] bool active() const noexcept { return frameSequence!=0u; }
};

[[nodiscard]] inline bool advanceBinaryMarketDepthTransaction(
    const BinaryMarketRecord& record, BinaryMarketDepthTransactionState& state) noexcept {
    const auto& h=record.header;
    if(h.channel!=BinaryMarketChannel::Depth) return !state.active();
    const bool coverage=(h.flags&BinaryMarketRecordDepthCoverage)!=0u;
    const bool hasCoverage=(h.flags&BinaryMarketRecordDepthHasCoverage)!=0u;
    const bool end=(h.flags&BinaryMarketRecordDepthTransactionEnd)!=0u;
    constexpr auto semanticFlags=BinaryMarketRecordTraderReplayCompatible|BinaryMarketRecordDepthSnapshot|
        BinaryMarketRecordDepthDelta|BinaryMarketRecordDepthPartial|BinaryMarketRecordDepthRebase|
        BinaryMarketRecordDepthState|BinaryMarketRecordDepthHasCoverage|BinaryMarketRecordDepthSourcePosition;
    if(!validBinaryMarketRecord(record)) return false;
    if(!state.active()) {
        if(coverage) return false;
        const auto& p=*binaryMarketPayload<BinaryMarketDepthChunkPayload>(&record);
        if(p.transactionPartIndex!=0u || p.partIndex!=0u) return false;
        state.frameSequence=h.frameSequence;state.firstShardSequence=h.shardSequence;
        state.receiveMonotonicNs=h.receiveMonotonicNs;state.receiveRealtimeNs=h.receiveRealtimeNs;
        state.nativeParts=p.transactionPartCount;state.hasCoverage=hasCoverage;
    }
    if(h.frameSequence!=state.frameSequence || h.receiveMonotonicNs!=state.receiveMonotonicNs ||
       h.receiveRealtimeNs!=state.receiveRealtimeNs || hasCoverage!=state.hasCoverage) return false;
    if(state.lastNative.sourceId!=0u && (h.sourceId!=state.lastNative.sourceId ||
       h.sourceGeneration!=state.lastNative.sourceGeneration || h.sessionEpoch!=state.lastNative.sessionEpoch)) return false;
    if(coverage) {
        const auto& p=*binaryMarketPayload<BinaryMarketDepthCoveragePayload>(&record);
        if(!state.awaitingCoverage || p.transactionPartCount!=state.nativeParts || p.partIndex!=state.nextCoverage ||
           h.sourceId!=state.lastNative.sourceId || h.sourceGeneration!=state.lastNative.sourceGeneration ||
           h.sessionEpoch!=state.lastNative.sessionEpoch || h.eventSequence!=state.lastNative.eventSequence ||
           h.exchangeTimestampNs!=state.lastNative.exchangeTimestampNs) return false;
        if(state.nextCoverage==0u) {state.totalCoverageCount=p.totalCoverageCount;state.coverageParts=p.partCount;}
        if(p.totalCoverageCount!=state.totalCoverageCount || p.partCount!=state.coverageParts) return false;
        if(++state.nextCoverage==state.coverageParts) {if(!end)return false;state={};}
        return true;
    }
    if(state.awaitingCoverage) return false;
    const auto& p=*binaryMarketPayload<BinaryMarketDepthChunkPayload>(&record);
    if(p.transactionPartCount==0u || p.transactionPartCount>cxet::market::kMaximumDepthTransactionParts ||
       p.transactionPartCount!=state.nativeParts || p.transactionPartIndex!=state.nextNative ||
       p.partCount==0u || p.partIndex!=state.nextChunk || p.partIndex>=p.partCount ||
       p.levelOffset!=static_cast<std::uint32_t>(p.partIndex)*kBinaryMarketDepthLevelsPerRecord ||
       p.levelOffset>p.totalLevelCount || p.levelCount>kBinaryMarketDepthLevelsPerRecord ||
       p.levelCount!=std::min<std::uint32_t>(kBinaryMarketDepthLevelsPerRecord,p.totalLevelCount-p.levelOffset) ||
       p.partCount!=(static_cast<std::uint64_t>(p.totalLevelCount)+kBinaryMarketDepthLevelsPerRecord-1u)/kBinaryMarketDepthLevelsPerRecord+
           (p.totalLevelCount==0u?1u:0u)) return false;
    if(state.nextChunk==0u) {
        state.totalLevelCount=p.totalLevelCount;state.chunkParts=p.partCount;
        state.nativeSequence=p.sequence;state.nativeEvidence=p.nativeEvidence;
        state.sourceReceiveRealtimeNs=p.sourceReceiveRealtimeNs;state.sourceFreshnessMonotonicNs=p.sourceFreshnessMonotonicNs;
        state.frameKind=p.frameKind;state.nativeState=p.state;state.failure=p.failure;
    }
    else if(h.sourceId!=state.lastNative.sourceId || h.sourceGeneration!=state.lastNative.sourceGeneration ||
            h.sessionEpoch!=state.lastNative.sessionEpoch || h.eventSequence!=state.lastNative.eventSequence ||
            h.exchangeTimestampNs!=state.lastNative.exchangeTimestampNs || h.sourceFlags!=state.lastNative.sourceFlags ||
            (h.flags&semanticFlags)!=(state.lastNative.flags&semanticFlags) ||
            p.sequence.firstUpdateId!=state.nativeSequence.firstUpdateId || p.sequence.lastUpdateId!=state.nativeSequence.lastUpdateId ||
            p.sequence.previousUpdateId!=state.nativeSequence.previousUpdateId || p.nativeEvidence!=state.nativeEvidence ||
            p.sourceReceiveRealtimeNs!=state.sourceReceiveRealtimeNs || p.sourceFreshnessMonotonicNs!=state.sourceFreshnessMonotonicNs ||
            p.frameKind!=state.frameKind || p.state!=state.nativeState || p.failure!=state.failure) return false;
    if(p.totalLevelCount!=state.totalLevelCount || p.partCount!=state.chunkParts) return false;
    state.lastNative=h;
    const bool finalChunk=p.partIndex+1u==p.partCount;
    const bool finalNative=p.transactionPartIndex+1u==p.transactionPartCount;
    if(end!=(!hasCoverage && finalChunk && finalNative)) return false;
    if(++state.nextChunk==state.chunkParts) {
        state.nextChunk=0u;++state.nextNative;
        if(state.nextNative==state.nativeParts) {
            if(state.hasCoverage) state.awaitingCoverage=true;
            else state={};
        }
    }
    return true;
}

} // namespace hftrec::corpus
