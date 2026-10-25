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
  row.header.sourceFlags=0;row.header.flags=BinaryMarketRecordDepthState|BinaryMarketRecordDepthTransactionEnd;
  row.header.payloadBytes=sizeof(BinaryMarketDepthChunkPayload);
  auto& p=*binaryMarketPayload<BinaryMarketDepthChunkPayload>(&row);
  p.frameKind=BinaryMarketDepthFrameKind::State;p.state=BinaryMarketDepthState::Gap;p.partCount=1;
  p.transactionPartCount=1u;p.failure=static_cast<std::uint8_t>(cxet::market::DepthFailure::ExchangeSequenceGap);
  CXET_CHECK(f.writer.append(row)==Status::Ok);f.seal();auto request=f.request();request.channelMask=5;
  BinaryMarketSelection out;std::string error;
  CXET_CHECK(f.reader.select(request,out,error)==Status::Ok && out.records.size()==2);
  CXET_CHECK(out.presentChannelMask==1 && out.sourcePresentChannelMasks[0]==1);
  CXET_CHECK(out.sourceObservedChannelMasks[0]==5);
  CXET_CHECK((out.records[1].header.flags&BinaryMarketRecordTraderReplayCompatible)==0);
  CXET_CHECK(binaryMarketPayload<BinaryMarketDepthChunkPayload>(&out.records[1])->state==BinaryMarketDepthState::Gap);
  request.requiredChannelMask=5;
  CXET_CHECK(f.reader.select(request,out,error)==Status::OutOfRange);
  // A complete native snapshot rebase retains its RebaseInFlight state.
  auto native=f.record(1,300);native.payload={};native.header.channel=BinaryMarketChannel::Depth;
  native.header.sourceFlags=0u;native.header.flags=BinaryMarketRecordTraderReplayCompatible|
      BinaryMarketRecordDepthSnapshot|BinaryMarketRecordDepthRebase|BinaryMarketRecordDepthTransactionEnd;
  native.header.payloadBytes=sizeof(BinaryMarketDepthChunkPayload);
  auto& snapshot=*binaryMarketPayload<BinaryMarketDepthChunkPayload>(&native);
  snapshot.sequence={17u,17u,0u};snapshot.partCount=snapshot.transactionPartCount=1u;
  snapshot.totalLevelCount=snapshot.levelCount=1u;snapshot.levels[0]={100u,2u,1u,1u,0u,0u};
  snapshot.frameKind=BinaryMarketDepthFrameKind::Snapshot;snapshot.state=BinaryMarketDepthState::RebaseInFlight;
  snapshot.sourceReceiveRealtimeNs=snapshot.sourceFreshnessMonotonicNs=299u;
  auto rebased=native;
  CXET_CHECK(binaryMarketDepthRecordIsNativeRebaseSnapshot(rebased));
  BinaryMarketDepthTransactionState transaction{};
  CXET_CHECK(advanceBinaryMarketDepthTransaction(rebased,transaction) && !transaction.active());
  CXET_CHECK(binaryMarketPayload<BinaryMarketDepthChunkPayload>(&rebased)->state==BinaryMarketDepthState::RebaseInFlight);
  auto incomplete=rebased;incomplete.header.flags&=~BinaryMarketRecordDepthTransactionEnd;
  transaction={};CXET_CHECK(!advanceBinaryMarketDepthTransaction(incomplete,transaction));
  auto unproved=rebased;unproved.header.flags|=BinaryMarketRecordDepthHasCoverage;
  CXET_CHECK(!binaryMarketDepthRecordIsNativeRebaseSnapshot(unproved));
  auto empty=native;empty.payload={};
  auto& emptyPayload=*binaryMarketPayload<BinaryMarketDepthChunkPayload>(&empty);
  emptyPayload=snapshot;emptyPayload.totalLevelCount=emptyPayload.levelCount=0u;emptyPayload.levels={};
  auto emptyRebased=empty;
  CXET_CHECK(binaryMarketDepthRecordIsNativeRebaseSnapshot(emptyRebased));
  transaction={};CXET_CHECK(advanceBinaryMarketDepthTransaction(emptyRebased,transaction) && !transaction.active());
  auto emptyMissingEnd=emptyRebased;emptyMissingEnd.header.flags&=~BinaryMarketRecordDepthTransactionEnd;
  transaction={};CXET_CHECK(!advanceBinaryMarketDepthTransaction(emptyMissingEnd,transaction));
  auto natural=initialBinaryMarketSourceState(f.sources[0]);
  CXET_CHECK(advanceBinaryMarketSourceState(row,natural) && natural.unhealthyMask==4u && natural.administrativeUnhealthyMask==0u);
  CXET_CHECK(advanceBinaryMarketSourceState(rebased,natural,binaryMarketDepthRecordIsNativeRebaseSnapshot(rebased)) && natural.unhealthyMask==0u);
  auto healthy=rebased;healthy.header.flags=BinaryMarketRecordTraderReplayCompatible|BinaryMarketRecordDepthDelta|BinaryMarketRecordDepthTransactionEnd;
  auto& healthyPayload=*binaryMarketPayload<BinaryMarketDepthChunkPayload>(&healthy);
  healthyPayload.state=BinaryMarketDepthState::Healthy;healthyPayload.frameKind=BinaryMarketDepthFrameKind::Delta;healthyPayload.sequence={18u,18u,17u};
  natural=initialBinaryMarketSourceState(f.sources[0]);
  CXET_CHECK(advanceBinaryMarketSourceState(row,natural) && natural.unhealthyMask==4u);
  CXET_CHECK(advanceBinaryMarketSourceState(healthy,natural) && natural.unhealthyMask==0u);
  for(const auto& ready:std::array{rebased,healthy,emptyRebased}) {
    Fixture current(4*1024*1024,1,true);
    auto naturalGap=row;naturalGap.header.eventSequence=naturalGap.header.frameSequence=naturalGap.header.shardSequence=1u;
    naturalGap.header.receiveRealtimeNs=naturalGap.header.receiveMonotonicNs=200;
    auto fresh=ready;fresh.header.eventSequence=fresh.header.frameSequence=fresh.header.shardSequence=2u;
    fresh.header.receiveRealtimeNs=fresh.header.receiveMonotonicNs=400;
    CXET_CHECK(current.writer.append(naturalGap)==Status::Ok);
    auto missingEnd=fresh;missingEnd.header.flags&=~BinaryMarketRecordDepthTransactionEnd;
    CXET_CHECK(current.writer.append(missingEnd)==Status::InvalidArgument);
    CXET_CHECK(current.writer.append(fresh)==Status::Ok);current.seal();
    auto depthRequest=current.request();depthRequest.channelMask=depthRequest.requiredChannelMask=4u;depthRequest.beginReceiveNs=250;
    CXET_CHECK(current.reader.select(depthRequest,out,error)==Status::Ok && out.sourcePresentChannelMasks[0]==4u);
    CXET_CHECK(out.sourceUnhealthyChannelMasksAtBegin[0]==4u && out.sourceAdministrativeUnhealthyChannelMasksAtBegin[0]==0u);
    CXET_CHECK(binaryMarketPayload<BinaryMarketDepthChunkPayload>(&out.records[0])->state==binaryMarketPayload<BinaryMarketDepthChunkPayload>(&ready)->state);
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
  Fixture depth(4*1024*1024,1,true);
  const auto depthMask=depth.sources[0].availableChannelMask;
  auto degraded=lifecycle(depth,1,200,BinaryMarketSourceLifecycleKind::DirectoryChanged,depth.sources[0],11,depthMask,depthMask);
  auto& depthHealth=*binaryMarketPayload<BinaryMarketSourceLifecyclePayload>(&degraded);
  depthHealth.requestSequence=0u;depthHealth.affectedChannel=3u;depthHealth.cause=BinaryMarketSourceTransitionCause::NativeDegraded;
  auto overlay=initialBinaryMarketSourceState(depth.sources[0]);
  CXET_CHECK(advanceBinaryMarketSourceState(degraded,overlay) && overlay.unhealthyMask==4u);
  auto fresh=depth.record(3,300);fresh.payload={};fresh.header.channel=BinaryMarketChannel::Depth;
  fresh.header.sourceFlags=0u;fresh.header.flags=BinaryMarketRecordTraderReplayCompatible|BinaryMarketRecordDepthSnapshot|
      BinaryMarketRecordDepthSourcePosition|BinaryMarketRecordDepthTransactionEnd;
  fresh.header.payloadBytes=sizeof(BinaryMarketDepthChunkPayload);
  auto& freshPayload=*binaryMarketPayload<BinaryMarketDepthChunkPayload>(&fresh);
  freshPayload.sequence={3u,3u,0u};freshPayload.partCount=freshPayload.transactionPartCount=1u;
  freshPayload.frameKind=BinaryMarketDepthFrameKind::Snapshot;freshPayload.state=BinaryMarketDepthState::Healthy;
  freshPayload.sourceReceiveRealtimeNs=freshPayload.sourceFreshnessMonotonicNs=100u;
  CXET_CHECK(validBinaryMarketRecord(fresh));
  CXET_CHECK(advanceBinaryMarketSourceState(fresh,overlay) && overlay.unhealthyMask==4u);
  CXET_CHECK(depth.writer.append(degraded)==Status::Ok);
  auto depthRecovered=degraded;depthRecovered.header.eventSequence=depthRecovered.header.shardSequence=2u;
  depthRecovered.header.receiveMonotonicNs=depthRecovered.header.receiveRealtimeNs=500;
  binaryMarketPayload<BinaryMarketSourceLifecyclePayload>(&depthRecovered)->cause=BinaryMarketSourceTransitionCause::NativeRecovered;
  CXET_CHECK(advanceBinaryMarketSourceState(depthRecovered,overlay) && overlay.unhealthyMask==4u &&
      overlay.administrativeUnhealthyMask==0u && overlay.recoveryPendingMask==4u);
  CXET_CHECK(advanceBinaryMarketSourceState(fresh,overlay) && overlay.unhealthyMask==0u && overlay.recoveryPendingMask==0u);
  CXET_CHECK(depth.writer.append(depthRecovered)==Status::Ok);depth.seal();
  auto depthRequest=depth.request();depthRequest.channelMask=4u;depthRequest.requiredChannelMask=0u;
  CXET_CHECK(depth.reader.select(depthRequest,out,error)==Status::Ok && out.records.size()==2u);
  for(const auto& administrative:out.records) {
    CXET_CHECK(administrative.header.channel==BinaryMarketChannel::SourceLifecycle);
    CXET_CHECK(administrative.header.nativeIdentityShape==0u && administrative.header.nativeIdentityFirst==0u && administrative.header.nativeIdentitySecond==0u);
  }
  CXET_CHECK(out.sourcePresentChannelMasks[0]==0u && out.presentChannelMask==0u);
  depthRequest.beginReceiveNs=300;depthRequest.endReceiveNs=450;
  CXET_CHECK(depth.reader.select(depthRequest,out,error)==Status::Ok && out.sourceUnhealthyChannelMasksAtBegin[0]==4u && out.records.empty());
  CXET_CHECK(out.sourceAdministrativeUnhealthyChannelMasksAtBegin[0]==4u && out.sourceRecoveryPendingChannelMasksAtBegin[0]==0u);
  depthRequest.beginReceiveNs=550;depthRequest.endReceiveNs=900;
  CXET_CHECK(depth.reader.select(depthRequest,out,error)==Status::Ok && out.sourceUnhealthyChannelMasksAtBegin[0]==4u && out.records.empty());
  CXET_CHECK(out.sourceAdministrativeUnhealthyChannelMasksAtBegin[0]==0u && out.sourceRecoveryPendingChannelMasksAtBegin[0]==4u);
  Fixture admin(4*1024*1024,1,true);
  auto blocked=fresh;blocked.header.eventSequence=blocked.header.frameSequence=blocked.header.shardSequence=2u;
  auto armed=depthRecovered;armed.header.eventSequence=armed.header.shardSequence=3u;
  auto admitted=fresh;admitted.header.eventSequence=admitted.header.frameSequence=admitted.header.shardSequence=4u;
  admitted.header.receiveRealtimeNs=admitted.header.receiveMonotonicNs=600;
  binaryMarketPayload<BinaryMarketDepthChunkPayload>(&blocked)->sequence={2u,2u,0u};
  binaryMarketPayload<BinaryMarketDepthChunkPayload>(&admitted)->sequence={4u,4u,0u};
  CXET_CHECK(admin.writer.append(degraded)==Status::Ok && admin.writer.append(blocked)==Status::Ok &&
      admin.writer.append(armed)==Status::Ok && admin.writer.append(admitted)==Status::Ok);admin.seal();
  auto adminRequest=admin.request();adminRequest.channelMask=adminRequest.requiredChannelMask=4u;
  adminRequest.beginReceiveNs=250;adminRequest.endReceiveNs=450;
  CXET_CHECK(admin.reader.select(adminRequest,out,error)==Status::OutOfRange && out.sourcePresentChannelMasks[0]==0u);
  CXET_CHECK(out.sourceAdministrativeUnhealthyChannelMasksAtBegin[0]==4u);
  adminRequest.beginReceiveNs=550;adminRequest.endReceiveNs=900;
  CXET_CHECK(admin.reader.select(adminRequest,out,error)==Status::Ok && out.sourcePresentChannelMasks[0]==4u);
  CXET_CHECK(out.sourceAdministrativeUnhealthyChannelMasksAtBegin[0]==0u && out.sourceRecoveryPendingChannelMasksAtBegin[0]==4u);
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
