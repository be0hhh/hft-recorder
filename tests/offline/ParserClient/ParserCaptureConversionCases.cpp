#include "OfflineCase.hpp"
#include "Capture/Parser/Private/CorpusConversion.hpp"
#include "hftrec/CorpusContract/BinaryMarketDepthTransaction.hpp"
#include <algorithm>
#include <array>

namespace {
namespace ipc=hft_parser::ipc;
namespace corpus=hftrec::corpus;
ipc::MarketCaptureSourceDescriptor source() {
  ipc::MarketCaptureSourceDescriptor s{};
  s.market.sourceId=1;s.market.canonicalSymbolId=7;s.market.sourceGeneration=11;
  s.market.venueId=1;s.market.marketRaw=2;s.market.priceScale=s.market.quantityScale=8;
  s.market.economicBaseAssetId=1;s.market.quoteAssetId=2;
  s.market.flags=ipc::MarketDirectoryBookTicker;
  std::copy_n("fixture",7,s.market.venueCode.begin());s.market.venueCodeBytes=7;
  std::copy_n("AAA",3,s.market.canonicalSymbol.begin());s.market.canonicalSymbolBytes=3;
  std::copy_n("AAA",3,s.market.nativeSymbol.begin());s.market.nativeSymbolBytes=3;
  std::copy_n("spot",4,s.marketCode.begin());s.marketCodeBytes=4;
  s.configuredChannelMask=s.availableChannelMask=s.traderReplayChannelMask=1;
  s.compatibility[0]=ipc::MarketCaptureCompatibility::ExactTraderReplay;
  return s;
}
ipc::MarketCaptureRecord marker() {
  ipc::MarketCaptureRecord r{};
  r.header.sourceId=1;r.header.sourceGeneration=11;r.header.sessionEpoch=17;
  r.header.eventSequence=3;r.header.shardSequence=5;
  r.header.receiveRealtimeNs=1000;r.header.receiveMonotonicNs=2000;
  r.header.channel=ipc::MarketCaptureChannel::Membership;
  r.header.flags=ipc::MarketCaptureRecordTraderReplayCompatible|ipc::MarketCaptureRecordExchangeTimestampMissing;
  r.header.payloadBytes=sizeof(ipc::MarketCaptureMembershipPayload);
  auto& p=*ipc::marketCapturePayload<ipc::MarketCaptureMembershipPayload>(&r);
  p.source=source();p.source.availableChannelMask=p.source.traderReplayChannelMask=0;
  p.source.compatibility={};p.sourceGeneration=p.previousSourceGeneration=11;p.sessionEpoch=17;
  p.directoryRevision=9;p.requestSequence=4;p.channelMaskBefore=1;p.channelMaskAfter=0;
  p.canonicalSymbolId=7;p.venueId=1;p.marketRaw=2;
  p.affectedChannel=ipc::MarketCaptureChannel::BookTicker;
  p.action=static_cast<std::uint8_t>(ipc::MarketCaptureSubscriptionAction::Remove);
  p.status=static_cast<std::uint8_t>(ipc::MarketCaptureSubscriptionStatus::Applied);
  return r;
}
void membershipHasExactArrivalAndMetadata() {
  const std::array directory{source()};
  CXET_CHECK(hftrec::capture::detail::validSourceDescriptor(directory[0],0));
  CXET_CHECK(corpus::validBinaryMarketSource(hftrec::capture::detail::convertSource(directory[0]),0));
  CXET_CHECK(ipc::validMarketCaptureRecord(marker()));
  corpus::BinaryMarketRecord out{};
  const bool converted=hftrec::capture::detail::convertRecord(marker(),0,directory,out);
  CXET_CHECK(out.header.channel==corpus::BinaryMarketChannel::SourceLifecycle);
  const auto& evidence=*corpus::binaryMarketPayload<corpus::BinaryMarketSourceLifecyclePayload>(&out);
  CXET_CHECK(evidence.directoryRevision==9);
  CXET_CHECK(corpus::validBinaryMarketSource(evidence.source,0));
  CXET_CHECK(corpus::validBinaryMarketRecord(out));
  CXET_CHECK(converted);
  CXET_CHECK(out.header.channel==corpus::BinaryMarketChannel::SourceLifecycle);
  CXET_CHECK(out.header.frameSequence==0 && out.header.exchangeTimestampNs==0);
  CXET_CHECK(out.header.receiveRealtimeNs==1000 && out.header.receiveMonotonicNs==2000);
  CXET_CHECK(out.header.shardSequence==5 && out.header.sourceGeneration==11);
  const auto& p=*corpus::binaryMarketPayload<corpus::BinaryMarketSourceLifecyclePayload>(&out);
  CXET_CHECK(p.kind==corpus::BinaryMarketSourceLifecycleKind::Removed);
  CXET_CHECK(p.previousSourceGeneration==11 && p.source.initialSourceGeneration==11);
  CXET_CHECK(p.channelMaskBefore==1 && p.channelMaskAfter==0 && p.source.availableChannelMask==0);
  CXET_CHECK(p.source.canonicalSymbol==directory[0].market.canonicalSymbol);
}
void membershipCannotBecomeLiquidationOrUseFutureMetadata() {
  auto current=source();current.market.sourceGeneration=99;
  const std::array directory{current};
  corpus::BinaryMarketRecord out{};
  CXET_CHECK(hftrec::capture::detail::convertRecord(marker(),0,directory,out));
  const auto& p=*corpus::binaryMarketPayload<corpus::BinaryMarketSourceLifecyclePayload>(&out);
  CXET_CHECK(p.source.initialSourceGeneration==11);
  auto bad=marker();
  ipc::marketCapturePayload<ipc::MarketCaptureMembershipPayload>(&bad)->sourceGeneration=99;
  CXET_CHECK(!hftrec::capture::detail::convertRecord(bad,0,directory,out));
  bad=marker();bad.header.payloadBytes=0;
  CXET_CHECK(!hftrec::capture::detail::convertRecord(bad,0,directory,out));
}
void failedReaddRetainsFailureEvidence() {
  const std::array directory{source()};auto input=marker();
  auto& p=*ipc::marketCapturePayload<ipc::MarketCaptureMembershipPayload>(&input);
  p.action=static_cast<std::uint8_t>(ipc::MarketCaptureSubscriptionAction::Add);
  p.status=static_cast<std::uint8_t>(ipc::MarketCaptureSubscriptionStatus::LifecycleFailed);
  p.channelMaskBefore=p.channelMaskAfter=0u;
  corpus::BinaryMarketRecord out{};
  CXET_CHECK(hftrec::capture::detail::convertRecord(input,0,directory,out));
  const auto& result=*corpus::binaryMarketPayload<corpus::BinaryMarketSourceLifecyclePayload>(&out);
  CXET_CHECK(result.kind==corpus::BinaryMarketSourceLifecycleKind::DirectoryChanged);
  CXET_CHECK(result.reason==p.status && result.affectedChannel==1u);
  CXET_CHECK(result.channelMaskBefore==0 && result.channelMaskAfter==0);
}
void nativeClosedPreservesCapabilityAndExactCause() {
  const std::array directory{source()};auto input=marker();
  auto& p=*ipc::marketCapturePayload<ipc::MarketCaptureMembershipPayload>(&input);
  p.channelMaskBefore=p.channelMaskAfter=1u;p.source=directory[0];p.requestSequence=0u;
  p.cause=ipc::MarketCaptureSourceTransitionCause::NativeClosed;
  corpus::BinaryMarketRecord out{};
  CXET_CHECK(hftrec::capture::detail::convertRecord(input,0,directory,out));
  const auto& result=*corpus::binaryMarketPayload<corpus::BinaryMarketSourceLifecyclePayload>(&out);
  CXET_CHECK(result.kind==corpus::BinaryMarketSourceLifecycleKind::DirectoryChanged);
  CXET_CHECK(result.cause==corpus::BinaryMarketSourceTransitionCause::NativeClosed);
  CXET_CHECK(result.channelMaskBefore==1u && result.channelMaskAfter==1u && result.requestSequence==0u);
  CXET_CHECK(out.header.receiveMonotonicNs==2000u && out.header.receiveRealtimeNs==1000);
  p.affectedChannel=ipc::MarketCaptureChannel::Trade;
  CXET_CHECK(!hftrec::capture::detail::convertRecord(input,0,directory,out));
  auto depth=source();depth.market.flags=ipc::MarketDirectoryDepth;
  depth.configuredChannelMask=depth.availableChannelMask=depth.traderReplayChannelMask=4u;
  depth.compatibility={};depth.compatibility[2]=ipc::MarketCaptureCompatibility::ExactTraderReplay;
  const std::array depthDirectory{depth};auto depthMarker=marker();
  auto& health=*ipc::marketCapturePayload<ipc::MarketCaptureMembershipPayload>(&depthMarker);
  health.source=depth;health.channelMaskBefore=health.channelMaskAfter=4u;health.requestSequence=0u;
  health.affectedChannel=ipc::MarketCaptureChannel::Depth;
  for(const auto cause:{ipc::MarketCaptureSourceTransitionCause::NativeDegraded,ipc::MarketCaptureSourceTransitionCause::NativeRecovered}) {
    health.cause=cause;
    CXET_CHECK(hftrec::capture::detail::convertRecord(depthMarker,0u,depthDirectory,out));
    CXET_CHECK(out.header.channel==corpus::BinaryMarketChannel::SourceLifecycle);
    CXET_CHECK(out.header.frameSequence==0u && out.header.nativeIdentityShape==0u &&
               out.header.nativeIdentityFirst==0u && out.header.nativeIdentitySecond==0u);
    const auto& converted=*corpus::binaryMarketPayload<corpus::BinaryMarketSourceLifecyclePayload>(&out);
    CXET_CHECK(converted.affectedChannel==3u && static_cast<std::uint8_t>(converted.cause)==static_cast<std::uint8_t>(cause));
    CXET_CHECK(converted.source.availableChannelMask==4u && converted.channelMaskBefore==converted.channelMaskAfter);
  }
}
void exactExecutionQuantitySurvivesSourceConversion() {
  auto input=source();
  input.instrument.stepSizeRaw=6;
  input.instrument.contractBaseQtyRaw=20'000'000;
  input.instrument.canonicalStepSizeRaw=1'200'000;
  input.instrument.executionQuantityStepRaw=2'400'000;
  input.instrument.flags=ipc::MarketCaptureInstrumentStepSize |
      ipc::MarketCaptureInstrumentContractBaseQty |
      ipc::MarketCaptureInstrumentCanonicalStepSize |
      ipc::MarketCaptureInstrumentExecutionQuantityStep;
  CXET_CHECK(hftrec::capture::detail::validSourceDescriptor(input,0));
  const auto converted=hftrec::capture::detail::convertSource(input);
  CXET_CHECK(converted.stepSizeRaw==6);
  CXET_CHECK(converted.canonicalStepSizeRaw==1'200'000);
  CXET_CHECK(converted.executionQuantityStepRaw==2'400'000);
  CXET_CHECK(corpus::validBinaryMarketSource(converted,0));
  input.instrument.flags&=~ipc::MarketCaptureInstrumentExecutionQuantityStep;
  CXET_CHECK(!hftrec::capture::detail::validSourceDescriptor(input,0));
}
void marketQuantityAuthoritySurvivesConversionAndLifecycle() {
  auto input=source();auto& q=input.instrument.quantityConversion;
  q.kinds[0]=ipc::MarketCaptureQuantityKind::NativeContract;
  q.canonicalBaseMultiplier=1u;q.nativeBaseMultiplier=1000u;
  q.nativeContractBaseQtyRaw=10'000;
  CXET_CHECK(hftrec::capture::detail::validSourceDescriptor(input,0));
  const auto converted=hftrec::capture::detail::convertSource(input);
  const auto& out=converted.quantityConversion;
  CXET_CHECK(out.kinds[0]==corpus::BinaryMarketQuantityKind::NativeContract);
  CXET_CHECK(out.canonicalBaseMultiplier==1u && out.nativeBaseMultiplier==1000u);
  CXET_CHECK(out.nativeContractBaseQtyRaw==10'000 && out.nativeLotBaseQtyRaw==0);
  auto row=marker();auto& payload=*ipc::marketCapturePayload<ipc::MarketCaptureMembershipPayload>(&row);
  payload.source=input;payload.channelMaskBefore=payload.channelMaskAfter=1u;
  payload.cause=ipc::MarketCaptureSourceTransitionCause::NativeRecovered;payload.requestSequence=0u;
  corpus::BinaryMarketRecord record{};const std::array directory{input};
  CXET_CHECK(hftrec::capture::detail::convertRecord(row,0u,directory,record));
  const auto& lifecycle=*corpus::binaryMarketPayload<corpus::BinaryMarketSourceLifecyclePayload>(&record);
  CXET_CHECK(lifecycle.source.quantityConversion==out);
}
void captureMalformedQuantityAuthorityRefuses() {
  auto input=source();auto& q=input.instrument.quantityConversion;
  q.kinds[0]=static_cast<ipc::MarketCaptureQuantityKind>(255u);
  CXET_CHECK(!hftrec::capture::detail::validSourceDescriptor(input,0u));
  q={};q.kinds[0]=ipc::MarketCaptureQuantityKind::NativeContract;
  q.canonicalBaseMultiplier=q.nativeBaseMultiplier=1u;
  CXET_CHECK(!hftrec::capture::detail::validSourceDescriptor(input,0u));
  q.nativeContractBaseQtyRaw=1;
  CXET_CHECK(hftrec::capture::detail::validSourceDescriptor(input,0u));
  q.kinds[1]=ipc::MarketCaptureQuantityKind::NativeBase;
  CXET_CHECK(!hftrec::capture::detail::validSourceDescriptor(input,0u));
  q.kinds[1]=ipc::MarketCaptureQuantityKind::Unknown;
  q.reserved[0]=std::byte{1};
  CXET_CHECK(!hftrec::capture::detail::validSourceDescriptor(input,0u));
}
void composedSnapshotPreservesAuthoritativeStateAndAbsentClocks() {
  auto descriptor=source();
  descriptor.configuredChannelMask=descriptor.availableChannelMask=descriptor.traderReplayChannelMask=4u;
  descriptor.market.flags=ipc::MarketDirectoryDepth;
  descriptor.compatibility={};descriptor.compatibility[2]=ipc::MarketCaptureCompatibility::ExactTraderReplay;
  const std::array directory{descriptor};
  ipc::MarketCaptureRecord row{};
  row.header.sourceId=1u;row.header.sourceGeneration=11u;row.header.sessionEpoch=17u;
  row.header.eventSequence=3u;row.header.frameSequence=40u;row.header.shardSequence=5u;
  row.header.receiveRealtimeNs=1000;row.header.receiveMonotonicNs=2000u;
  row.header.channel=ipc::MarketCaptureChannel::Depth;
  row.header.payloadBytes=sizeof(ipc::MarketCaptureDepthChunkPayload);
  const auto flags=ipc::MarketCaptureRecordTraderReplayCompatible|ipc::MarketCaptureRecordExchangeTimestampMissing|
      ipc::MarketCaptureRecordDepthSnapshot|ipc::MarketCaptureRecordDepthSourcePosition|ipc::MarketCaptureRecordDepthHasCoverage;
  auto& p=*ipc::marketCapturePayload<ipc::MarketCaptureDepthChunkPayload>(&row);
  p.sequence={7u,7u,0u};p.partCount=1u;p.transactionPartCount=1u;
  p.frameKind=ipc::MarketCaptureDepthFrameKind::Snapshot;
  corpus::BinaryMarketRecord converted{};
  for(const auto state:{cxet::market::DepthSourceState::Healthy,cxet::market::DepthSourceState::Partial,
                        cxet::market::DepthSourceState::Gap,cxet::market::DepthSourceState::Empty}) {
    p.state=state;row.header.flags=flags|(state==cxet::market::DepthSourceState::Partial?ipc::MarketCaptureRecordDepthPartial:0u);
    CXET_CHECK(hftrec::capture::detail::convertRecord(row,0u,directory,converted));
    CXET_CHECK(corpus::binaryMarketDepthRecordIsComposedCut(converted));
    const auto& out=*corpus::binaryMarketPayload<corpus::BinaryMarketDepthChunkPayload>(&converted);
    CXET_CHECK(out.state==static_cast<corpus::BinaryMarketDepthState>(state));
    CXET_CHECK(out.failure==0u && out.totalLevelCount==0u && out.sourceReceiveRealtimeNs==0u && out.sourceFreshnessMonotonicNs==0u);
    CXET_CHECK((converted.header.flags&corpus::BinaryMarketRecordDepthRebase)==0u);
    const bool unavailable=state==cxet::market::DepthSourceState::Gap || state==cxet::market::DepthSourceState::Empty;
    CXET_CHECK(corpus::binaryMarketRecordIsSourceHealth(converted)==unavailable);
    converted.header.sourceFlags|=corpus::BinaryMarketSourceDegraded;
    CXET_CHECK(corpus::binaryMarketRecordIsSourceHealth(converted)==unavailable);
  }
  p.state=cxet::market::DepthSourceState::Gap;row.header.flags=flags;
  CXET_CHECK(hftrec::capture::detail::convertRecord(row,0u,directory,converted));
  corpus::BinaryMarketDepthTransactionState transaction{};
  CXET_CHECK(corpus::advanceBinaryMarketDepthTransaction(converted,transaction));
  CXET_CHECK(transaction.active());
  auto tail=row;tail.payload={};tail.header.shardSequence=6u;
  tail.header.flags=(flags&~ipc::MarketCaptureRecordDepthSnapshot)|ipc::MarketCaptureRecordDepthCoverage|ipc::MarketCaptureRecordDepthTransactionEnd;
  tail.header.payloadBytes=sizeof(ipc::MarketCaptureDepthCoveragePayload);
  auto& coverage=*ipc::marketCapturePayload<ipc::MarketCaptureDepthCoveragePayload>(&tail);
  coverage.totalCoverageCount=1u;coverage.coverageCount=coverage.partCount=coverage.transactionPartCount=1u;
  coverage.coverage[0].sourceId=1u;
  CXET_CHECK(hftrec::capture::detail::convertRecord(tail,0u,directory,converted));
  CXET_CHECK(corpus::advanceBinaryMarketDepthTransaction(converted,transaction));
  CXET_CHECK(!transaction.active());
  row.header.flags=flags&~ipc::MarketCaptureRecordDepthHasCoverage;
  CXET_CHECK(!hftrec::capture::detail::convertRecord(row,0u,directory,converted));
  row.header.flags=flags&~ipc::MarketCaptureRecordDepthSourcePosition;
  CXET_CHECK(!hftrec::capture::detail::convertRecord(row,0u,directory,converted));
  row.header.flags=flags;p.state=cxet::market::DepthSourceState::Empty;
  p.totalLevelCount=p.levelCount=1u;p.levels[0]={1u,1u,cxet::market::DepthSide::Bid,cxet::market::DepthAction::Upsert,0u,0u};
  CXET_CHECK(!hftrec::capture::detail::convertRecord(row,0u,directory,converted));
}
}
int main(int argc,char** argv) {
  const cxet::testing::Case cases[]{
    cxet::testing::Case{"capture.membership_exact_arrival_metadata",membershipHasExactArrivalAndMetadata},
    cxet::testing::Case{"capture.membership_tag_and_historical_metadata",membershipCannotBecomeLiquidationOrUseFutureMetadata},
    cxet::testing::Case{"capture.failed_readd_retains_failure_evidence",failedReaddRetainsFailureEvidence},
    cxet::testing::Case{"capture.native_closed_preserves_capability_and_exact_cause",nativeClosedPreservesCapabilityAndExactCause},
    cxet::testing::Case{"capture.execution_quantity_grid_survives_source_conversion",exactExecutionQuantitySurvivesSourceConversion},
    cxet::testing::Case{"capture.market_quantity_authority_survives_conversion_and_lifecycle",marketQuantityAuthoritySurvivesConversionAndLifecycle},
    cxet::testing::Case{"capture.malformed_market_quantity_authority_refuses",captureMalformedQuantityAuthorityRefuses},
    cxet::testing::Case{"capture.composed_snapshot_preserves_authoritative_state_and_absent_clocks",composedSnapshotPreservesAuthoritativeStateAndAbsentClocks}};
  return cxet::testing::runCases(argc,argv,cases);
}
