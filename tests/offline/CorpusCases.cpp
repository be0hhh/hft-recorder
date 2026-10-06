#include "OfflineCase.hpp"
#include "hftrec/CorpusContract/BinaryMarketCorpusReader.hpp"
#include "hftrec/CorpusContract/BinaryMarketCorpusWriter.hpp"
#include "Corpus/BinaryMarketSourceState.hpp"
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
void capturedHealthIsAnAvailabilityFence() {
  Fixture f; CXET_CHECK(f.writer.append(f.record(1))==Status::Ok);
  constexpr std::array<std::uint32_t,3> flags{BinaryMarketSourceStale,BinaryMarketSourceDegraded,BinaryMarketSourceSequenceGap};
  for(std::size_t i=0;i<flags.size();++i) {
    auto row=f.record(i+2,201+i);row.header.sourceFlags|=flags[i];
    row.header.flags&=~BinaryMarketRecordTraderReplayCompatible;
    CXET_CHECK(f.writer.append(row)==Status::Ok);
  }
  CXET_CHECK(f.writer.append(f.record(5,205))==Status::Ok);f.seal();
  BinaryMarketSelection out;std::string error;
  CXET_CHECK(f.reader.select(f.request(),out,error)==Status::Ok && out.records.size()==5);
  for(std::size_t i=0;i<flags.size();++i) CXET_CHECK((out.records[i+1].header.sourceFlags&flags[i])!=0);
  Fixture lost;auto row=lost.record(1);row.header.flags|=BinaryMarketRecordGapBoundary;
  CXET_CHECK(lost.writer.append(row)==Status::Ok);lost.seal();
  CXET_CHECK(lost.reader.select(lost.request(),out,error)==Status::CorruptData);
}
void healthCoverageAndIntervalStart() {
  Fixture f;auto row=f.record(1,200);row.header.sourceFlags|=BinaryMarketSourceStale;
  CXET_CHECK(f.writer.append(row)==Status::Ok);
  CXET_CHECK(f.writer.append(f.record(2,400))==Status::Ok);f.seal();
  auto request=f.request();request.endReceiveNs=300;
  BinaryMarketSelection out;std::string error;
  CXET_CHECK(f.reader.select(request,out,error)==Status::OutOfRange);
  request.beginReceiveNs=300;request.endReceiveNs=500;
  CXET_CHECK(f.reader.select(request,out,error)==Status::Ok && out.records.size()==1);
  CXET_CHECK(out.sourceUnhealthyChannelMasksAtBegin.size()==1 && out.sourceUnhealthyChannelMasksAtBegin[0]==1);
  CXET_CHECK(out.sourcePresentChannelMasks[0]==1);
  request.beginReceiveNs=450;request.requiredChannelMask=0;
  CXET_CHECK(f.reader.select(request,out,error)==Status::Ok && out.sourceUnhealthyChannelMasksAtBegin[0]==0);
}
void depthStateGapIsNotAReplayPrice() {
  Fixture f(4*1024*1024,1,true);
  CXET_CHECK(f.writer.append(f.record(1))==Status::Ok);
  auto row=f.record(2,201);row.payload={};row.header.channel=BinaryMarketChannel::Depth;
  row.header.sourceFlags=0;row.header.flags=BinaryMarketRecordDepthState;
  row.header.payloadBytes=sizeof(BinaryMarketDepthChunkPayload);
  auto& p=*binaryMarketPayload<BinaryMarketDepthChunkPayload>(&row);
  p.frameKind=BinaryMarketDepthFrameKind::State;p.state=BinaryMarketDepthState::Gap;p.partCount=1;
  CXET_CHECK(f.writer.append(row)==Status::Ok);f.seal();auto request=f.request();request.channelMask=5;
  BinaryMarketSelection out;std::string error;
  CXET_CHECK(f.reader.select(request,out,error)==Status::Ok && out.records.size()==2);
  CXET_CHECK(out.presentChannelMask==1 && out.sourcePresentChannelMasks[0]==1);
  CXET_CHECK(out.sourceObservedChannelMasks[0]==5);
  CXET_CHECK((out.records[1].header.flags&BinaryMarketRecordTraderReplayCompatible)==0);
  CXET_CHECK(binaryMarketPayload<BinaryMarketDepthChunkPayload>(&out.records[1])->state==BinaryMarketDepthState::Gap);
  request.requiredChannelMask=5;
  CXET_CHECK(f.reader.select(request,out,error)==Status::OutOfRange);
}
void roundtrip() {
  Fixture f; CXET_CHECK(f.writer.append(f.record(1))==Status::Ok); f.seal();
  BinaryMarketSelection out; std::string error;
  CXET_CHECK(f.reader.select(f.request(),out,error)==Status::Ok && out.records.size()==1);
  CXET_CHECK(out.sourceGeneration==11 && out.records[0].header.sessionEpoch==17);
  const auto* p=binaryMarketPayload<BinaryMarketBookTickerPayload>(&out.records[0]);
  CXET_CHECK(p->bidPriceRaw==100 && p->askQtyRaw==3 && out.manifest.complete==1);
}
void compressedCursor() {
  Fixture f(4*1024*1024,2);
  // The selected corpus exceeds the reader block size; arrival merge crosses shards.
  for (std::uint64_t n=1; n<=1025; ++n) {
    auto r=f.record((n+1)/2,200+n); r.header.shardIndex=(n-1)%2;
    CXET_CHECK(f.writer.append(r)==Status::Ok);
  }
  f.seal(); BinaryMarketCorpusCursor cursor; BinaryMarketSelection metadata; std::string error;
  CXET_CHECK(cursor.open(f.request(),metadata,error)==Status::Ok);
  CXET_CHECK(metadata.records.empty() && metadata.presentChannelMask==1);
  std::uint64_t count=0; bool available=false; BinaryMarketRecord record;
  do {
    CXET_CHECK(cursor.next(record,available,error)==Status::Ok);
    if (available) { ++count; CXET_CHECK(record.header.receiveRealtimeNs==200+count); }
  } while (available);
  CXET_CHECK(count==1025);
  std::uint64_t physicalBytes=0;
  for (const auto& entry : std::filesystem::recursive_directory_iterator(f.root))
    if(entry.is_regular_file()) physicalBytes+=entry.file_size();
  CXET_CHECK(physicalBytes < count*sizeof(BinaryMarketRecord)/2);
  CXET_CHECK(physicalBytes <= metadata.manifest.projectedBytes);
}
void allChannelsExactBytes() {
  Fixture f(4*1024*1024,1,true);
  std::array<BinaryMarketRecord,8> expected{};
  for(std::size_t n=0;n<expected.size();++n) {
    auto& r=expected[n];r=f.record(n+1,200+n);
    r.header.channel=static_cast<BinaryMarketChannel>(n+1);
    r.header.sourceFlags=0u;r.header.payloadBytes=kBinaryMarketPayloadBytes;
    r.header.nativeIdentityShape=1u;
    r.header.nativeIdentityFirst=0x1122334455667788ull+n;
    if(n==2) {
      auto* p=binaryMarketPayload<BinaryMarketDepthChunkPayload>(&r);
      *p={};p->totalLevelCount=p->levelCount=kBinaryMarketDepthLevelsPerRecord;
      p->partCount=1;p->frameKind=BinaryMarketDepthFrameKind::Snapshot;
      p->state=BinaryMarketDepthState::Healthy;
      p->sequence={100,132,99};p->sourceReceiveRealtimeNs=198;
      for(std::size_t i=0;i<p->levels.size();++i) {
        p->levels[i].priceRaw=100+i;p->levels[i].quantityRaw=10+i;
        p->levels[i].side=i%2;p->levels[i].action=1;
      }
      r.header.flags|=BinaryMarketRecordDepthSnapshot;
    }
    CXET_CHECK(f.writer.append(r)==Status::Ok);
  }
  f.seal();auto request=f.request();request.channelMask=request.requiredChannelMask=0xff;
  BinaryMarketCorpusCursor cursor;BinaryMarketSelection metadata;std::string error;
  CXET_CHECK(cursor.open(request,metadata,error)==Status::Ok && metadata.presentChannelMask==0xff);
  for(const auto& expectedRecord:expected) {
    BinaryMarketRecord actual;bool available=false;
    CXET_CHECK(cursor.next(actual,available,error)==Status::Ok && available);
    const auto* a=reinterpret_cast<const unsigned char*>(&actual);
    const auto* e=reinterpret_cast<const unsigned char*>(&expectedRecord);
    for(std::size_t i=0;i<sizeof(actual);++i) CXET_CHECK(a[i]==e[i]);
  }
}
void wholeCorpusCursor() {
  Fixture f(4*1024*1024,2,false,true);
  for(std::uint64_t n=1;n<=4;++n) {
    auto r=f.record((n+1)/2,200+n);r.header.shardIndex=(n-1)%2;
    if(n%2==0) {r.header.sourceId=2;r.header.sourceGeneration=12;}
    CXET_CHECK(f.writer.append(r)==Status::Ok);
  }
  f.seal();auto request=f.request();request.allSources=true;
  request.exchange.clear();request.market.clear();request.canonicalSymbol.clear();
  BinaryMarketCorpusCursor cursor;BinaryMarketSelection metadata;std::string error;
  CXET_CHECK(cursor.open(request,metadata,error)==Status::Ok && metadata.records.empty());
  CXET_CHECK(metadata.source.sourceId==0 && metadata.sourceGeneration==0);
  CXET_CHECK(metadata.sourcePresentChannelMasks.size()==2 && metadata.sourcePresentChannelMasks[0]==1 && metadata.sourcePresentChannelMasks[1]==1);
  for(std::uint64_t n=1;n<=4;++n) {
    BinaryMarketRecord record;bool available=false;
    CXET_CHECK(cursor.next(record,available,error)==Status::Ok && available);
    CXET_CHECK(record.header.receiveRealtimeNs==200+n && record.header.sourceId==(n%2==0?2:1));
  }
}
void selectedCorpusSourcesIgnoreForeignGaps() {
  Fixture f(4*1024*1024,2,false,true);
  CXET_CHECK(f.writer.append(f.record(1,200))==Status::Ok);
  auto second=f.record(1,300);second.header.sourceId=2;second.header.sourceGeneration=12;second.header.shardIndex=1;
  CXET_CHECK(f.writer.append(second)==Status::Ok);
  BinaryMarketGap gap{};gap.sourceId=2;gap.sourceGeneration=12;gap.shardIndex=1;
  gap.gapEpoch=gap.droppedRecords=1;gap.firstDroppedEventSequence=gap.lastDroppedEventSequence=2;
  gap.minimumDroppedReceiveNs=gap.maximumDroppedReceiveNs=gap.observedReceiveNs=400;
  CXET_CHECK(f.writer.appendGap(gap)==Status::Ok);f.seal();
  auto request=f.request();request.allSources=true;request.sourceIds={1};
  request.exchange.clear();request.market.clear();request.canonicalSymbol.clear();
  BinaryMarketCorpusCursor cursor;BinaryMarketSelection metadata;std::string error;
  CXET_CHECK(cursor.open(request,metadata,error)==Status::Ok);
  BinaryMarketRecord record;bool available=false;
  CXET_CHECK(cursor.next(record,available,error)==Status::Ok && available && record.header.sourceId==1);
  CXET_CHECK(cursor.next(record,available,error)==Status::Ok && !available);
  request.sourceIds.clear();
  CXET_CHECK(cursor.open(request,metadata,error)==Status::CorruptData);
}
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
void lifecycleRemoveAndReadd() {
  Fixture f(4*1024*1024);CXET_CHECK(f.writer.append(f.record(1,200))==Status::Ok);
  auto removed=f.sources[0];removed.initialSourceGeneration=11;removed.availableChannelMask=removed.traderReplayChannelMask=0;removed.compatibility.fill(BinaryMarketCompatibility::Unavailable);
  CXET_CHECK(f.writer.append(lifecycle(f,2,300,BinaryMarketSourceLifecycleKind::Removed,removed,11,3,0))==Status::Ok);
  auto invalid=f.record(3,350);invalid.header.sourceGeneration=11;
  CXET_CHECK(f.writer.append(invalid)==Status::InvalidArgument);
  auto readded=f.sources[0];readded.initialSourceGeneration=13;
  CXET_CHECK(f.writer.append(lifecycle(f,3,400,BinaryMarketSourceLifecycleKind::Readded,readded,11,0,3))==Status::Ok);
  auto next=f.record(4,500);next.header.sourceGeneration=13;
  CXET_CHECK(f.writer.append(next)==Status::Ok);f.seal();
  BinaryMarketCorpusCursor cursor;BinaryMarketSelection metadata;std::string error;
  CXET_CHECK(cursor.open(f.request(),metadata,error)==Status::Ok);
  for(auto channel:{BinaryMarketChannel::BookTicker,BinaryMarketChannel::SourceLifecycle,BinaryMarketChannel::SourceLifecycle,BinaryMarketChannel::BookTicker}) {
    BinaryMarketRecord record;bool available=false;CXET_CHECK(cursor.next(record,available,error)==Status::Ok && available);
    CXET_CHECK(record.header.channel==channel);
    if(channel==BinaryMarketChannel::SourceLifecycle) CXET_CHECK(record.header.frameSequence==0);
  }
}
void nativeHealthPreservesMembershipAndHydratesInterval() {
  Fixture f(4*1024*1024);CXET_CHECK(f.writer.append(f.record(1,200))==Status::Ok);
  auto closed=lifecycle(f,2,300,BinaryMarketSourceLifecycleKind::DirectoryChanged,f.sources[0],11,3,3);
  auto& evidence=*binaryMarketPayload<BinaryMarketSourceLifecyclePayload>(&closed);
  evidence.requestSequence=0u;evidence.affectedChannel=1u;evidence.cause=BinaryMarketSourceTransitionCause::NativeClosed;
  CXET_CHECK(f.writer.append(closed)==Status::Ok);
  auto recovered=closed;recovered.header.eventSequence=recovered.header.shardSequence=3u;
  recovered.header.receiveMonotonicNs=recovered.header.receiveRealtimeNs=500;
  auto& recoveredEvidence=*binaryMarketPayload<BinaryMarketSourceLifecyclePayload>(&recovered);
  recoveredEvidence.directoryRevision=3u;recoveredEvidence.cause=BinaryMarketSourceTransitionCause::NativeRecovered;
  CXET_CHECK(f.writer.append(recovered)==Status::Ok);CXET_CHECK(f.writer.append(f.record(4,600))==Status::Ok);f.seal();
  auto request=f.request();request.beginReceiveNs=400;
  BinaryMarketSelection out;std::string error;
  CXET_CHECK(f.reader.select(request,out,error)==Status::Ok);
  CXET_CHECK(out.sourcesAtBegin[0].availableChannelMask==3u && out.sourceUnhealthyChannelMasksAtBegin[0]==1u);
  request.beginReceiveNs=550;
  CXET_CHECK(f.reader.select(request,out,error)==Status::Ok && out.sourceUnhealthyChannelMasksAtBegin[0]==0u);
}
void nativeHealthSampleCannotApplyFutureSiblingSelection() {
  Fixture f(4*1024*1024);
  auto sampled=f.sources[0];sampled.availableChannelMask=sampled.traderReplayChannelMask=1u;
  sampled.compatibility[1]=BinaryMarketCompatibility::Unavailable;
  auto closed=lifecycle(f,1,200,BinaryMarketSourceLifecycleKind::DirectoryChanged,sampled,11,1,1);
  auto& p=*binaryMarketPayload<BinaryMarketSourceLifecyclePayload>(&closed);
  p.directoryRevision=99u;p.requestSequence=0u;p.affectedChannel=1u;
  p.cause=BinaryMarketSourceTransitionCause::NativeClosed;
  CXET_CHECK(f.writer.append(closed)==Status::Ok);
  CXET_CHECK(f.writer.append(lifecycle(f,2,300,BinaryMarketSourceLifecycleKind::Removed,sampled,11,3,1))==Status::Ok);
  CXET_CHECK(f.writer.append(f.record(3,400))==Status::Ok);f.seal();
  auto request=f.request();request.beginReceiveNs=250;
  BinaryMarketSelection out;std::string error;
  CXET_CHECK(f.reader.select(request,out,error)==Status::Ok);
  CXET_CHECK(out.sourcesAtBegin[0].availableChannelMask==3u);
  CXET_CHECK(out.sourceLifecycleRevisionsAtBegin[0]==0u && out.sourceUnhealthyChannelMasksAtBegin[0]==1u);
}
void nativeRecoveryAdvancesOnlyActualGeneration() {
  Fixture f(4*1024*1024);auto generation=f.sources[0];generation.initialSourceGeneration=12u;
  auto recovered=lifecycle(f,1,200,BinaryMarketSourceLifecycleKind::DirectoryChanged,generation,12,3,3);
  auto& p=*binaryMarketPayload<BinaryMarketSourceLifecyclePayload>(&recovered);
  p.requestSequence=0u;p.affectedChannel=1u;p.cause=BinaryMarketSourceTransitionCause::NativeRecovered;
  CXET_CHECK(f.writer.append(recovered)==Status::Ok);
  auto foreign=recovered;foreign.header.eventSequence=foreign.header.shardSequence=2u;
  foreign.header.receiveMonotonicNs=foreign.header.receiveRealtimeNs=250;
  auto& changed=*binaryMarketPayload<BinaryMarketSourceLifecyclePayload>(&foreign);
  changed.source.tickSizeRaw=1;changed.source.directoryFlags|=BinaryMarketDirectoryTickSize;
  CXET_CHECK(validBinaryMarketRecord(foreign));
  CXET_CHECK(f.writer.append(foreign)==Status::InvalidArgument);
  auto row=f.record(2,300);row.header.sourceGeneration=12u;
  CXET_CHECK(f.writer.append(row)==Status::Ok);f.seal();
  auto request=f.request();request.beginReceiveNs=250;
  BinaryMarketSelection out;std::string error;
  CXET_CHECK(f.reader.select(request,out,error)==Status::Ok);
  CXET_CHECK(out.sourcesAtBegin[0].initialSourceGeneration==12u && out.sourceLifecycleRevisionsAtBegin[0]==0u);
}
void sourceIdCannotChangeNativeListing() {
  Fixture f(4*1024*1024);auto changed=f.sources[0];
  changed.nativeSymbol={};text(changed.nativeSymbol,"BAD");changed.nativeSymbolBytes=3;
  CXET_CHECK(f.writer.registerSources({&changed,1})==Status::InvalidArgument);
  CXET_CHECK(f.writer.append(lifecycle(f,1,300,BinaryMarketSourceLifecycleKind::DirectoryChanged,
      changed,11,3,3))==Status::InvalidArgument);
}
void lifecycleAddsStableCatalogSource() {
  Fixture f(4*1024*1024);auto added=f.sources[0];added.sourceId=2;added.canonicalSymbolId=8;added.initialSourceGeneration=21;
  added.canonicalSymbol={};added.nativeSymbol={};text(added.canonicalSymbol,"BBB");text(added.nativeSymbol,"BBB");
  CXET_CHECK(f.writer.append(lifecycle(f,1,300,BinaryMarketSourceLifecycleKind::Added,added,0,0,3))==Status::Ok);
  auto row=f.record(2,350);row.header.sourceId=2;row.header.sourceGeneration=21;
  CXET_CHECK(f.writer.append(row)==Status::Ok);f.seal();
  BinaryMarketManifest manifest;std::vector<BinaryMarketSource> catalog;std::string error;
  CXET_CHECK(f.reader.catalog(f.root,manifest,catalog,error)==Status::Ok && catalog.size()==2);
  CXET_CHECK(catalog[0].initiallyPresent==1 && catalog[1].initiallyPresent==0 && catalog[1].sourceId==2);
  auto request=f.request();request.allSources=true;request.exchange.clear();request.market.clear();request.canonicalSymbol.clear();
  BinaryMarketCorpusCursor cursor;BinaryMarketSelection metadata;
  CXET_CHECK(cursor.open(request,metadata,error)==Status::Ok);
  BinaryMarketRecord record;bool available=false;
  CXET_CHECK(cursor.next(record,available,error)==Status::Ok && available && record.header.channel==BinaryMarketChannel::SourceLifecycle);
  CXET_CHECK(cursor.next(record,available,error)==Status::Ok && available && record.header.sourceId==2);
}
void lifecyclePartialRemovalKeepsOtherChannels() {
  Fixture f(4*1024*1024);auto partial=f.sources[0];partial.initialSourceGeneration=11;
  partial.availableChannelMask=partial.traderReplayChannelMask=1;partial.compatibility[1]=BinaryMarketCompatibility::Unavailable;
  CXET_CHECK(f.writer.append(lifecycle(f,1,300,BinaryMarketSourceLifecycleKind::Removed,partial,11,3,1))==Status::Ok);
  auto row=f.record(2,400);row.header.sourceGeneration=11;
  CXET_CHECK(f.writer.append(row)==Status::Ok);f.seal();
  BinaryMarketSelection selection;std::string error;
  CXET_CHECK(f.reader.select(f.request(),selection,error)==Status::Ok && selection.records.size()==2);
  CXET_CHECK(selection.records[1].header.channel==BinaryMarketChannel::BookTicker);
}
void lifecycleRegistersDormantCatalogPrefix() {
  Fixture f(4*1024*1024);std::array<BinaryMarketSource,2> added{f.sources[0],f.sources[0]};
  for(std::size_t n=0;n<added.size();++n) {
    added[n].sourceId=2+n;added[n].canonicalSymbolId=8+n;added[n].initialSourceGeneration=20+n;
    added[n].canonicalSymbol={};added[n].nativeSymbol={};
    const std::string symbol=n==0?"BBB":"CCC";text(added[n].canonicalSymbol,symbol);text(added[n].nativeSymbol,symbol);
  }
  CXET_CHECK(f.writer.registerSources(added)==Status::Ok);
  CXET_CHECK(f.writer.append(lifecycle(f,1,300,BinaryMarketSourceLifecycleKind::Added,added[1],0,0,3))==Status::Ok);
  auto row=f.record(2,350);row.header.sourceId=3;row.header.sourceGeneration=21;
  CXET_CHECK(f.writer.append(row)==Status::Ok);f.seal();
  BinaryMarketManifest manifest;std::vector<BinaryMarketSource> catalog;std::string error;
  CXET_CHECK(f.reader.catalog(f.root,manifest,catalog,error)==Status::Ok && catalog.size()==3);
  CXET_CHECK(catalog[1].initiallyPresent==0 && catalog[1].initialSourceGeneration==0);
  CXET_CHECK(catalog[2].initiallyPresent==0 && catalog[2].initialSourceGeneration==0);
  auto request=f.request();request.allSources=true;request.exchange.clear();request.market.clear();request.canonicalSymbol.clear();
  BinaryMarketCorpusCursor cursor;BinaryMarketSelection metadata;
  CXET_CHECK(cursor.open(request,metadata,error)==Status::Ok);
  BinaryMarketRecord record;bool available=false;
  CXET_CHECK(cursor.next(record,available,error)==Status::Ok && available && record.header.sourceId==3);
}
void lifecycleLossRejectsEverySelection() {
  Fixture f(4*1024*1024,2,false,true);CXET_CHECK(f.writer.append(f.record(1,200))==Status::Ok);
  BinaryMarketGap gap{};gap.sourceId=2;gap.sourceGeneration=12;gap.shardIndex=1;gap.channel=BinaryMarketChannel::SourceLifecycle;
  gap.gapEpoch=gap.droppedRecords=1;gap.firstDroppedEventSequence=gap.lastDroppedEventSequence=2;
  gap.minimumDroppedReceiveNs=gap.maximumDroppedReceiveNs=gap.observedReceiveNs=300;
  CXET_CHECK(f.writer.appendGap(gap)==Status::Ok);f.seal();
  BinaryMarketSelection selection;std::string error;
  CXET_CHECK(f.reader.select(f.request(),selection,error)==Status::CorruptData && selection.gaps.size()==1);
}
void lifecycleReaddPreservesSharedSourceGeneration() {
  Fixture f(4*1024*1024);auto partial=f.sources[0];
  partial.availableChannelMask=partial.traderReplayChannelMask=2;partial.compatibility[0]=BinaryMarketCompatibility::Unavailable;
  auto removed=lifecycle(f,1,300,BinaryMarketSourceLifecycleKind::Removed,partial,11,3,2);
  binaryMarketPayload<BinaryMarketSourceLifecyclePayload>(&removed)->affectedChannel=1;
  CXET_CHECK(f.writer.append(removed)==Status::Ok);
  auto readded=lifecycle(f,2,400,BinaryMarketSourceLifecycleKind::Readded,f.sources[0],11,2,3);
  readded.header.sessionEpoch=18;binaryMarketPayload<BinaryMarketSourceLifecyclePayload>(&readded)->affectedChannel=1;
  CXET_CHECK(f.writer.append(readded)==Status::Ok);
  auto bbo=f.record(3,500);bbo.header.sessionEpoch=18;CXET_CHECK(f.writer.append(bbo)==Status::Ok);
  auto trade=f.record(4,600);trade.payload={};trade.header.channel=BinaryMarketChannel::Trade;
  trade.header.payloadBytes=sizeof(BinaryMarketTradePayload);trade.header.sourceFlags=0;
  *binaryMarketPayload<BinaryMarketTradePayload>(&trade)={100,1,1,{}};
  CXET_CHECK(f.writer.append(trade)==Status::Ok);f.seal();
  auto request=f.request();request.channelMask=request.requiredChannelMask=3;
  BinaryMarketSelection selected;std::string error;
  CXET_CHECK(f.reader.select(request,selected,error)==Status::Ok && selected.records.size()==4);
  CXET_CHECK(selected.records[2].header.sourceGeneration==11 && selected.records[2].header.sessionEpoch==18);
  CXET_CHECK(selected.records[3].header.sourceGeneration==11 && selected.records[3].header.sessionEpoch==17);
}
void lifecycleSelectionStartsWithEffectiveMetadata() {
  Fixture f(4*1024*1024);CXET_CHECK(f.writer.append(f.record(1,200))==Status::Ok);
  auto removed=f.sources[0];removed.availableChannelMask=removed.traderReplayChannelMask=0;removed.compatibility.fill(BinaryMarketCompatibility::Unavailable);
  CXET_CHECK(f.writer.append(lifecycle(f,2,300,BinaryMarketSourceLifecycleKind::Removed,removed,11,3,0))==Status::Ok);
  auto readded=f.sources[0];readded.initialSourceGeneration=13;readded.priceScale=7;
  CXET_CHECK(f.writer.append(lifecycle(f,3,400,BinaryMarketSourceLifecycleKind::Readded,readded,11,0,3))==Status::Ok);
  auto row=f.record(4,500);row.header.sourceGeneration=13;CXET_CHECK(f.writer.append(row)==Status::Ok);f.seal();
  auto request=f.request();request.beginReceiveNs=450;
  BinaryMarketCorpusCursor cursor;BinaryMarketSelection metadata;std::string error;
  const auto status=cursor.open(request,metadata,error);
  CXET_CHECK(status==Status::Ok && metadata.sourcesAtBegin.size()==1);
  CXET_CHECK(metadata.sourcesAtBegin[0].initiallyPresent==1 && metadata.sourcesAtBegin[0].initialSourceGeneration==13);
  CXET_CHECK(metadata.sourcesAtBegin[0].priceScale==7 && metadata.sourceLifecycleRevisionsAtBegin[0]==3);
  BinaryMarketRecord actual;bool available=false;
  CXET_CHECK(cursor.next(actual,available,error)==Status::Ok && available && actual.header.receiveRealtimeNs==500);
}
void cursorFailsBeforeEmission() {
  Fixture f; CXET_CHECK(f.writer.append(f.record(1))==Status::Ok); f.seal();
  for(const auto& entry : std::filesystem::directory_iterator(f.root/"segments")) {
    if(entry.path().extension()==".cxm") {
      std::filesystem::resize_file(entry.path(),entry.file_size()-1u); break;
    }
  }
  BinaryMarketCorpusCursor cursor; BinaryMarketSelection metadata; std::string error;
  CXET_CHECK(cursor.open(f.request(),metadata,error)==Status::CorruptData);
  BinaryMarketRecord record; bool available=true;
  CXET_CHECK(cursor.next(record,available,error)==Status::InvalidArgument && !available);
}
void identity() {
  Fixture f; CXET_CHECK(f.writer.append(f.record(1))==Status::Ok); f.seal();
  auto r=f.request(); r.canonicalSymbol="BBB"; BinaryMarketSelection out; std::string error;
  CXET_CHECK(f.reader.select(r,out,error)==Status::OutOfRange && out.records.empty());
}
void generation() {
  Fixture f; auto r=f.record(1); r.header.sourceGeneration=12;
  CXET_CHECK(f.writer.append(r)==Status::InvalidArgument && f.writer.snapshot().recordCount==0);
  CXET_CHECK(f.writer.append(f.record(1))==Status::Ok); f.seal();
}
void interval() {
  Fixture f; for (auto n : {200,300,400}) CXET_CHECK(f.writer.append(f.record(n,n))==Status::Ok);
  f.seal(); auto r=f.request(); r.beginReceiveNs=200; r.endReceiveNs=400;
  BinaryMarketSelection out; std::string error;
  CXET_CHECK(f.reader.select(r,out,error)==Status::Ok && out.records.size()==2);
  CXET_CHECK(out.records[0].header.receiveRealtimeNs==200 && out.records[1].header.receiveRealtimeNs==300);
}
void requiredChannel() {
  Fixture f; CXET_CHECK(f.writer.append(f.record(1))==Status::Ok); f.seal(); auto r=f.request();
  r.channelMask|=binaryMarketChannelBit(BinaryMarketChannel::Trade); r.requiredChannelMask=r.channelMask;
  BinaryMarketSelection out; std::string error;
  CXET_CHECK(f.reader.select(r,out,error)==Status::OutOfRange && out.presentChannelMask==1);
}
void gap() {
  Fixture f; CXET_CHECK(f.writer.append(f.record(1))==Status::Ok);
  BinaryMarketGap g{}; g.sourceId=1; g.sourceGeneration=11; g.gapEpoch=1; g.droppedRecords=1;
  g.firstDroppedEventSequence=g.lastDroppedEventSequence=2;
  g.minimumDroppedReceiveNs=g.maximumDroppedReceiveNs=g.observedReceiveNs=300;
  CXET_CHECK(f.writer.appendGap(g)==Status::Ok); f.seal(); BinaryMarketSelection out; std::string error;
  CXET_CHECK(f.reader.select(f.request(),out,error)==Status::CorruptData && out.gaps.size()==1);
  CXET_CHECK(out.gaps[0].droppedRecords==1);
}
void corruption() {
  {
    Fixture f; CXET_CHECK(f.writer.append(f.record(1))==Status::Ok); f.seal();
    std::fstream file(f.root/"manifest.bin",std::ios::binary|std::ios::in|std::ios::out);
    CXET_CHECK(file.good()); file.seekp(0); file.put('\0'); file.close();
    BinaryMarketManifest manifest; std::vector<BinaryMarketSource> sources; std::string error;
    CXET_CHECK(f.reader.catalog(f.root,manifest,sources,error)==Status::CorruptData);
  }
  Fixture f; CXET_CHECK(f.writer.append(f.record(1))==Status::Ok); f.seal();
  std::filesystem::path segment;
  for (const auto& file : std::filesystem::directory_iterator(f.root/"segments"))
    if (file.path().extension()==".cxm") segment=file.path();
  CXET_CHECK(!segment.empty());
  const auto offset=static_cast<std::streamoff>(sizeof(BinaryMarketSegmentHeader) + sizeof(BinaryMarketBlockHeader));
  std::fstream file(segment,std::ios::binary|std::ios::in|std::ios::out);
  file.seekg(offset); const auto original=file.get(); CXET_CHECK(file.good());
  // Corrupt the independent codec block while keeping the file size unchanged.
  file.seekp(offset); file.put(static_cast<char>(original^1)); file.close();
  BinaryMarketSelection out; std::string error;
  CXET_CHECK(f.reader.select(f.request(),out,error)==Status::CorruptData);
  file.open(segment,std::ios::binary|std::ios::in|std::ios::out);
  file.seekp(offset); file.put(static_cast<char>(original)); file.close();
  std::filesystem::resize_file(segment,std::filesystem::file_size(segment)-1u);
  CXET_CHECK(f.reader.select(f.request(),out,error)==Status::CorruptData);
}
void finalDrain() {
  Fixture f; std::uint64_t sequence=1; Status status=Status::Ok;
  for (; sequence<100000 && status==Status::Ok; ++sequence) status=f.writer.append(f.record(sequence,200+sequence));
  CXET_CHECK(status==Status::OutOfRange && sequence<100000 && f.writer.snapshot().quotaReached);
  const auto before=f.writer.snapshot().recordCount;
  CXET_CHECK(f.writer.beginFinalDrain()==Status::Ok);
  CXET_CHECK(f.writer.append(f.record(sequence,200+sequence))==Status::Ok);
  CXET_CHECK(f.writer.snapshot().recordCount==before+1 && f.writer.snapshot().projectedBytes<=16*1024);
  f.seal();
}
void uuidTailRoundtrip() {
  Fixture f; auto record = f.record(1u);
  record.payload = {};
  record.header.channel = BinaryMarketChannel::Trade;
  record.header.sourceFlags = 0u;
  record.header.payloadBytes = sizeof(BinaryMarketTradePayload);
  record.header.nativeIdentityShape = 4u;
  *binaryMarketPayload<BinaryMarketTradePayload>(&record) = {100, 1, 1u, {}};
  CXET_CHECK(f.writer.append(record) == Status::Ok);
  record.header.nativeIdentityFirst = UINT64_MAX;
  record.header.eventSequence = record.header.frameSequence = record.header.shardSequence = 2u;
  CXET_CHECK(f.writer.append(record) == Status::Ok); f.seal();
  auto request = f.request();
  request.channelMask = request.requiredChannelMask = binaryMarketChannelBit(BinaryMarketChannel::Trade);
  BinaryMarketSelection output; std::string error;
  CXET_CHECK(f.reader.select(request, output, error) == Status::Ok && output.records.size() == 2u);
  CXET_CHECK(output.records[0].header.nativeIdentityFirst == 0u && output.records[0].header.nativeIdentityShape == 4u);
  CXET_CHECK(output.records[1].header.nativeIdentityFirst == UINT64_MAX && output.records[1].header.nativeIdentitySecond == 0u);
}
void uuidTailInvalidBoundary() {
  Fixture f; auto record = f.record(1u);
  record.header.nativeIdentityShape = 4u;
  CXET_CHECK(!validBinaryMarketRecord(record));
  record.header.channel = BinaryMarketChannel::Trade;
  CXET_CHECK(validBinaryMarketRecord(record));
  record.header.nativeIdentitySecond = 1u;
  CXET_CHECK(!validBinaryMarketRecord(record));
  record.header.nativeIdentitySecond = 0u; record.header.nativeIdentityShape = 3u;
  CXET_CHECK(!validBinaryMarketRecord(record));
  record.header.nativeIdentityShape = 4u; record.header.schemaVersion = 2u;
  CXET_CHECK(!validBinaryMarketRecord(record));
}
void nativeQuantityAuthorityRoundtrip() {
  BinaryMarketQuantityConversion conversion{};
  conversion.kinds={BinaryMarketQuantityKind::NativeBase,BinaryMarketQuantityKind::NativeContract,BinaryMarketQuantityKind::NativeLot};
  conversion.canonicalBaseMultiplier=1u;conversion.nativeBaseMultiplier=1000u;
  conversion.nativeContractBaseQtyRaw=10'000;conversion.nativeLotBaseQtyRaw=20'000;
  Fixture f(4*1024*1024,1,true,false,&conversion);
  CXET_CHECK(f.writer.append(f.record(1u))==Status::Ok);f.seal();
  BinaryMarketSelection out;std::string error;
  CXET_CHECK(f.reader.select(f.request(),out,error)==Status::Ok && out.records.size()==1u);
  CXET_CHECK(out.sourcesAtBegin[0].quantityConversion==conversion);
  const auto* raw=binaryMarketPayload<BinaryMarketBookTickerPayload>(&out.records[0]);
  CXET_CHECK(raw->bidQtyRaw==2 && raw->askQtyRaw==3);
  auto state=initialBinaryMarketSourceState(out.sourcesAtBegin[0]);
  auto marker=lifecycle(f,2u,201,BinaryMarketSourceLifecycleKind::DirectoryChanged,
      out.sourcesAtBegin[0],11u,0xffu,0xffu);
  auto& p=*binaryMarketPayload<BinaryMarketSourceLifecyclePayload>(&marker);
  p.requestSequence=0u;p.affectedChannel=1u;p.cause=BinaryMarketSourceTransitionCause::NativeStale;
  CXET_CHECK(validBinaryMarketRecord(marker));
  p.source.quantityConversion.nativeContractBaseQtyRaw+=1;
  CXET_CHECK(!advanceBinaryMarketSourceState(marker,state));
}
void malformedQuantityAuthorityIsNotRecordable() {
  Fixture f;auto source=f.sources[0];auto& q=source.quantityConversion;
  CXET_CHECK(validBinaryMarketSource(source,0u));
  q.kinds[0]=static_cast<BinaryMarketQuantityKind>(255u);
  CXET_CHECK(!validBinaryMarketSource(source,0u));
  q={};q.kinds[0]=BinaryMarketQuantityKind::NativeContract;
  q.canonicalBaseMultiplier=q.nativeBaseMultiplier=1u;
  CXET_CHECK(!validBinaryMarketSource(source,0u));
  q.nativeContractBaseQtyRaw=1;
  CXET_CHECK(validBinaryMarketSource(source,0u));
  q.kinds[0]=BinaryMarketQuantityKind::NativeLot;
  q.nativeContractBaseQtyRaw=0;
  CXET_CHECK(!validBinaryMarketSource(source,0u));
  q.nativeLotBaseQtyRaw=1;
  CXET_CHECK(validBinaryMarketSource(source,0u));
  q.kinds[2]=BinaryMarketQuantityKind::NativeBase;
  CXET_CHECK(!validBinaryMarketSource(source,0u));
  q.kinds[2]=BinaryMarketQuantityKind::Unknown;
  q.reserved[0]=std::byte{1};
  CXET_CHECK(!validBinaryMarketSource(source,0u));
  q={};q.nativeBaseMultiplier=1u;
  CXET_CHECK(!validBinaryMarketSource(source,0u));
}
}
int main(int argc,char** argv) {
  const cxet::testing::Case cases[]{
    cxet::testing::Case{"recorder.native_quantity_authority_roundtrip_preserves_raw_and_health_identity",nativeQuantityAuthorityRoundtrip},
    cxet::testing::Case{"recorder.malformed_quantity_authority_is_not_recordable",malformedQuantityAuthorityIsNotRecordable},
    cxet::testing::Case{"recorder.native_health_cannot_apply_future_sibling_selection",nativeHealthSampleCannotApplyFutureSiblingSelection},
    cxet::testing::Case{"recorder.native_recovery_advances_actual_generation",nativeRecoveryAdvancesOnlyActualGeneration},
    cxet::testing::Case{"recorder.native_health_preserves_membership_and_hydrates_interval",nativeHealthPreservesMembershipAndHydratesInterval},
    cxet::testing::Case{"recorder.source_id_rejects_foreign_native_listing",sourceIdCannotChangeNativeListing},
    cxet::testing::Case{"recorder.captured_health_fences_preserved_capture_loss_refused",capturedHealthIsAnAvailabilityFence},
    cxet::testing::Case{"recorder.health_coverage_and_interval_start_are_exact",healthCoverageAndIntervalStart},
    cxet::testing::Case{"recorder.depth_state_gap_is_availability_not_price",depthStateGapIsNotAReplayPrice},
    cxet::testing::Case{"recorder.lifecycle_interval_start_uses_effective_membership_metadata",lifecycleSelectionStartsWithEffectiveMetadata},
    cxet::testing::Case{"recorder.lifecycle_native_readd_keeps_shared_generation_and_sibling_epoch",lifecycleReaddPreservesSharedSourceGeneration},
    cxet::testing::Case{"recorder.lifecycle_catalog_registration_keeps_dormant_future_ids",lifecycleRegistersDormantCatalogPrefix},
    cxet::testing::Case{"recorder.lifecycle_loss_refuses_unrelated_source_selection",lifecycleLossRejectsEverySelection},
    cxet::testing::Case{"recorder.lifecycle_remove_readd_retains_real_boundaries",lifecycleRemoveAndReadd},
    cxet::testing::Case{"recorder.lifecycle_new_listing_appends_stable_source_catalog",lifecycleAddsStableCatalogSource},
    cxet::testing::Case{"recorder.lifecycle_partial_remove_preserves_remaining_feed",lifecyclePartialRemovalKeepsOtherChannels},
    cxet::testing::Case{"recorder.whole_corpus_source_subset_ignores_foreign_gap",selectedCorpusSourcesIgnoreForeignGaps},
    cxet::testing::Case{"recorder.whole_corpus_cursor_merges_sources_without_record_vectors",wholeCorpusCursor},
    cxet::testing::Case{"recorder.compressed_all_channels_preserve_every_record_byte",allChannelsExactBytes},
    cxet::testing::Case{"recorder.compressed_cursor_bounded_multiblock_arrival_merge",compressedCursor},
    cxet::testing::Case{"recorder.cursor_refuses_corruption_before_emission",cursorFailsBeforeEmission},
    cxet::testing::Case{"recorder.uuid_tail_roundtrip_retains_zero_and_max",uuidTailRoundtrip},
    cxet::testing::Case{"recorder.uuid_tail_rejects_wrong_shape_channel_and_schema",uuidTailInvalidBoundary},
    cxet::testing::Case{"recorder.binary_roundtrip_retains_identity_and_values",roundtrip},
    cxet::testing::Case{"recorder.selection_requires_exact_source_identity",identity},
    cxet::testing::Case{"recorder.writer_rejects_foreign_generation_without_append",generation},
    cxet::testing::Case{"recorder.selection_receive_interval_is_half_open",interval},
    cxet::testing::Case{"recorder.selection_refuses_missing_required_channel",requiredChannel},
    cxet::testing::Case{"recorder.selection_preserves_gap_evidence_and_refuses_replay",gap},
    cxet::testing::Case{"recorder.reader_refuses_manifest_crc_and_truncated_segment",corruption},
    cxet::testing::Case{"recorder.final_drain_uses_reserved_capacity_below_hard_ceiling",finalDrain},
  }; return cxet::testing::runCases(argc,argv,cases);
}
