// One source is intentionally retained on both shards. Lifecycle state must
// follow merged arrival order, never segment-number order or eager lookahead.
void multishardLifecycleMerge() {
  for (const bool reverseSegments : {false,true}) {
    for (const bool refillBeforeTransition : {false,true}) {
      Fixture f(4*1024*1024,2);
      const std::uint16_t dataShard=reverseSegments?1u:0u;
      const std::uint16_t lifeShard=1u-dataShard;
      if(reverseSegments) {
        auto primer=f.record(1,150);primer.header.shardIndex=lifeShard;
        CXET_CHECK(f.writer.append(primer)==Status::Ok);
      }
      auto first=f.record(1,200);first.header.shardIndex=dataShard;
      CXET_CHECK(f.writer.append(first)==Status::Ok);
      std::uint64_t sequence=2u;
      if(refillBeforeTransition) {
        auto before=f.record(sequence++,250);before.header.shardIndex=dataShard;
        CXET_CHECK(f.writer.append(before)==Status::Ok);
      }
      auto generation=f.sources[0];generation.initialSourceGeneration=12u;
      auto transition=lifecycle(f,reverseSegments?2u:1u,300,BinaryMarketSourceLifecycleKind::DirectoryChanged,generation,12,3,3);
      transition.header.shardIndex=lifeShard;
      auto& life=*binaryMarketPayload<BinaryMarketSourceLifecyclePayload>(&transition);
      life.requestSequence=0u;life.affectedChannel=1u;life.cause=BinaryMarketSourceTransitionCause::NativeRecovered;
      CXET_CHECK(f.writer.append(transition)==Status::Ok);
      auto filtered=f.record(reverseSegments?3u:2u,325);filtered.header.shardIndex=lifeShard;
      filtered.header.sourceGeneration=12u;filtered.header.channel=BinaryMarketChannel::Trade;
      filtered.header.payloadBytes=sizeof(BinaryMarketTradePayload);filtered.payload={};
      filtered.header.sourceFlags=BinaryMarketSourceStale;filtered.header.flags=0u;
      *binaryMarketPayload<BinaryMarketTradePayload>(&filtered)={100,1,1,{}};
      CXET_CHECK(f.writer.append(filtered)==Status::Ok);
      // Cross enough blocks/rotations to exercise per-shard decoder refill.
      for(std::size_t i=0u;i<1025u;++i) {
        auto after=f.record(sequence++,400+i);after.header.shardIndex=dataShard;after.header.sourceGeneration=12u;
        CXET_CHECK(f.writer.append(after)==Status::Ok);
      }
      f.seal();
      for(const auto begin:{100,275,350}) {
        auto request=f.request();request.beginReceiveNs=begin;
        BinaryMarketCorpusCursor cursor;BinaryMarketSelection metadata;std::string error;
        CXET_CHECK(cursor.open(request,metadata,error)==Status::Ok);
        CXET_CHECK(metadata.sourcesAtBegin[0].initialSourceGeneration==(begin>300?12u:11u));
        CXET_CHECK(metadata.sourceUnhealthyChannelMasksAtBegin[0]==(begin>325?2u:0u));
        std::uint64_t count=0u,previous=0u;bool available=false;BinaryMarketRecord row;
        do {
          CXET_CHECK(cursor.next(row,available,error)==Status::Ok);
          if(available) {CXET_CHECK(row.header.receiveMonotonicNs>=previous);previous=row.header.receiveMonotonicNs;++count;}
        } while(available);
        CXET_CHECK(count==1025u+(begin<300?1u:0u)+(begin<200?1u:0u)+(begin<250 && refillBeforeTransition?1u:0u)+(begin<150 && reverseSegments?1u:0u));
      }
    }
  }
}
void multishardStaleGenerationFailsBeforeEmission() {
  Fixture f(4*1024*1024,2);
  auto old=f.record(1,400);CXET_CHECK(f.writer.append(old)==Status::Ok);
  auto generation=f.sources[0];generation.initialSourceGeneration=12u;
  auto transition=lifecycle(f,1,300,BinaryMarketSourceLifecycleKind::DirectoryChanged,generation,12,3,3);
  transition.header.shardIndex=1u;
  auto& life=*binaryMarketPayload<BinaryMarketSourceLifecyclePayload>(&transition);
  life.requestSequence=0u;life.affectedChannel=1u;life.cause=BinaryMarketSourceTransitionCause::NativeRecovered;
  // Writer append order is valid; actual merged order makes the t400 old row stale.
  CXET_CHECK(f.writer.append(transition)==Status::Ok);f.seal();
  BinaryMarketCorpusCursor cursor;BinaryMarketSelection metadata;std::string error;
  CXET_CHECK(cursor.open(f.request(),metadata,error)==Status::CorruptData);
  BinaryMarketRecord row;bool available=true;
  CXET_CHECK(cursor.next(row,available,error)==Status::InvalidArgument && !available);
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
