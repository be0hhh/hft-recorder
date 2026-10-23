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
