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
    r.payload={};r.header.sourceFlags=0u;
    r.header.nativeIdentityShape=1u;
    r.header.nativeIdentityFirst=0x1122334455667788ull+n;
    switch(r.header.channel) {
    case BinaryMarketChannel::BookTicker:
      r.header.payloadBytes=sizeof(BinaryMarketBookTickerPayload);
      r.header.sourceFlags=BinaryMarketSourceBookTickerBidPresent|BinaryMarketSourceBookTickerAskPresent;
      *binaryMarketPayload<BinaryMarketBookTickerPayload>(&r)={100,2,101,3};
      break;
    case BinaryMarketChannel::Trade:
      r.header.payloadBytes=sizeof(BinaryMarketTradePayload);
      *binaryMarketPayload<BinaryMarketTradePayload>(&r)={100,2,1u,{}};
      break;
    case BinaryMarketChannel::Depth: {
      r.header.payloadBytes=sizeof(BinaryMarketDepthChunkPayload);
      auto* p=binaryMarketPayload<BinaryMarketDepthChunkPayload>(&r);
      p->totalLevelCount=p->levelCount=kBinaryMarketDepthLevelsPerRecord;
      p->partCount=1;p->frameKind=BinaryMarketDepthFrameKind::Snapshot;
      p->transactionPartCount=1;
      p->state=BinaryMarketDepthState::Healthy;
      p->sequence={100,132,99};p->sourceReceiveRealtimeNs=p->sourceFreshnessMonotonicNs=198;
      for(std::size_t i=0;i<p->levels.size();++i) {
        p->levels[i].priceRaw=100+i;p->levels[i].quantityRaw=10+i;
        p->levels[i].side=i%2;p->levels[i].action=1;
      }
      r.header.flags|=BinaryMarketRecordDepthSnapshot|BinaryMarketRecordDepthTransactionEnd;
      break;
    }
    case BinaryMarketChannel::Liquidation:
      r.header.payloadBytes=sizeof(BinaryMarketLiquidationPayload);
      *binaryMarketPayload<BinaryMarketLiquidationPayload>(&r)={100,2,99,1,1,2,3,4,1u,{}};
      break;
    case BinaryMarketChannel::MarkPrice:
    case BinaryMarketChannel::IndexPrice:
      r.header.payloadBytes=sizeof(BinaryMarketScalarPricePayload);
      *binaryMarketPayload<BinaryMarketScalarPricePayload>(&r)={100+static_cast<std::int64_t>(n)};
      break;
    case BinaryMarketChannel::Funding:
      r.header.payloadBytes=sizeof(BinaryMarketFundingPayload);
      *binaryMarketPayload<BinaryMarketFundingPayload>(&r)={-5,190,290};
      break;
    case BinaryMarketChannel::PriceLimit:
      r.header.payloadBytes=sizeof(BinaryMarketPriceLimitPayload);
      *binaryMarketPayload<BinaryMarketPriceLimitPayload>(&r)={110,90,1u,{}};
      break;
    case BinaryMarketChannel::SourceLifecycle:
      CXET_CHECK(false);
      break;
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
