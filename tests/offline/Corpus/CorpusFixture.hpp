#pragma once

#include "OfflineCase.hpp"
#include "hftrec/CorpusContract/BinaryMarketCorpusReader.hpp"
#include "hftrec/CorpusContract/BinaryMarketCorpusWriter.hpp"
#include "Corpus/BinaryMarketSourceState.hpp"
#include "hftrec/CorpusContract/BinaryMarketDepthTransaction.hpp"
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

namespace {
using namespace hftrec::corpus;
using hftrec::Status;
template<std::size_t N> void text(std::array<char,N>& out, const std::string& value) {
  CXET_CHECK(value.size() <= out.size());
  for (std::size_t i=0; i<value.size(); ++i) out[i]=value[i];
}
struct Fixture {
  std::filesystem::path root = std::filesystem::current_path() /
      ("recorder-fixture-" + std::to_string(getpid()) + "-" +
       std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
  std::array<BinaryMarketSource,2> sources{};
  BinaryMarketCorpusWriter writer;
  BinaryMarketCorpusReader reader;
  explicit Fixture(std::uint64_t maximumBytes=16*1024, std::uint16_t shards=1, bool allChannels=false, bool twoSources=false,
      const BinaryMarketQuantityConversion* quantityConversion=nullptr) {
    auto& s=sources[0]; s.sourceId=1; s.canonicalSymbolId=7; s.initialSourceGeneration=11;
    s.venueId=1; s.marketRaw=2; s.marketKindRaw=1; s.economicBaseAssetId=1; s.quoteAssetId=2;
    s.priceScale=s.quantityScale=8;
    text(s.venue,"fixture"); s.venueBytes=7;
    text(s.market,"spot"); s.marketBytes=4;
    text(s.canonicalSymbol,"AAA"); s.canonicalSymbolBytes=3;
    text(s.nativeSymbol,"AAA"); s.nativeSymbolBytes=3;
    s.directoryFlags=BinaryMarketDirectoryBookTicker|BinaryMarketDirectoryTrade;
    s.configuredChannelMask=s.availableChannelMask=s.traderReplayChannelMask=
        binaryMarketChannelBit(BinaryMarketChannel::BookTicker)|binaryMarketChannelBit(BinaryMarketChannel::Trade);
    s.compatibility[0]=s.compatibility[1]=BinaryMarketCompatibility::ExactTraderReplay;
    if(allChannels) {
      s.directoryFlags|=BinaryMarketDirectoryDepth;
      s.configuredChannelMask=s.availableChannelMask=s.traderReplayChannelMask=0xff;
      s.compatibility.fill(BinaryMarketCompatibility::ExactTraderReplay);
    }
    if(quantityConversion) {
      s.quantityConversion=*quantityConversion;
      s.marketRaw=1u;s.marketKindRaw=2u;s.assetDomainRaw=1u;
      s.market={};text(s.market,"futures");s.marketBytes=7u;
    }
    if(twoSources) {
      sources[1]=sources[0];sources[1].sourceId=2;sources[1].canonicalSymbolId=8;sources[1].initialSourceGeneration=12;
      sources[1].canonicalSymbol={};sources[1].nativeSymbol={};text(sources[1].canonicalSymbol,"BBB");text(sources[1].nativeSymbol,"BBB");
    }
    BinaryMarketWriterConfig cfg{}; cfg.root=root; cfg.sources=std::span<const BinaryMarketSource>{sources}.first(twoSources?2u:1u); cfg.producerEpoch=13;
    cfg.maximumBytes=maximumBytes; cfg.segmentTargetBytes=4096; cfg.targetDurationNs=100000;
    cfg.startedReceiveNs=100; cfg.startedMonotonicNs=100; cfg.ringCapacity=2; cfg.shardCount=shards;
    CXET_CHECK(writer.start(cfg)==Status::Ok);
  }
  ~Fixture() { std::error_code ec; std::filesystem::remove_all(root,ec); }
  BinaryMarketRecord record(std::uint64_t sequence, std::int64_t receive=200) {
    BinaryMarketRecord r{}; auto& h=r.header; h.sourceId=1; h.sourceGeneration=11;
    h.sessionEpoch=17; h.eventSequence=h.frameSequence=h.shardSequence=sequence;
    h.receiveRealtimeNs=receive; h.receiveMonotonicNs=static_cast<std::uint64_t>(receive);
    h.exchangeTimestampNs=receive-1; h.flags=BinaryMarketRecordTraderReplayCompatible;
    h.payloadBytes=sizeof(BinaryMarketBookTickerPayload);
    h.sourceFlags=BinaryMarketSourceBookTickerBidPresent|BinaryMarketSourceBookTickerAskPresent;
    *binaryMarketPayload<BinaryMarketBookTickerPayload>(&r)={100,2,101,3}; return r;
  }
  void seal() { CXET_CHECK(writer.finalize(BinaryMarketStopReason::Requested,10000)==Status::Ok); }
  BinaryMarketSelectionRequest request() const {
    BinaryMarketSelectionRequest r{}; r.root=root; r.exchange="fixture";
    r.market=sources[0].marketRaw==1u?"futures":"spot";
    r.canonicalSymbol="AAA"; r.beginReceiveNs=100; r.endReceiveNs=10000;
    r.channelMask=r.requiredChannelMask=binaryMarketChannelBit(BinaryMarketChannel::BookTicker); return r;
  }
};
BinaryMarketRecord lifecycle(Fixture& fixture,std::uint64_t sequence,std::int64_t receive,
    BinaryMarketSourceLifecycleKind kind,BinaryMarketSource source,
    std::uint64_t previous,std::uint16_t before,std::uint16_t after) {
  auto record=fixture.record(sequence,receive);record.payload={};
  record.header.channel=BinaryMarketChannel::SourceLifecycle;
  record.header.flags=BinaryMarketRecordSourceLifecycle|BinaryMarketRecordTraderReplayCompatible|BinaryMarketRecordExchangeTimestampMissing;
  record.header.sourceFlags=0;record.header.sourceId=source.sourceId;
  record.header.sourceGeneration=source.initialSourceGeneration;record.header.frameSequence=0;
  record.header.exchangeTimestampNs=0;record.header.payloadBytes=sizeof(BinaryMarketSourceLifecyclePayload);
  auto* payload=binaryMarketPayload<BinaryMarketSourceLifecyclePayload>(&record);
  payload->directoryRevision=sequence;payload->previousSourceGeneration=previous;payload->requestSequence=sequence;
  payload->kind=kind;payload->channelMaskBefore=before;payload->channelMaskAfter=after;payload->source=source;
  return record;
}
}
