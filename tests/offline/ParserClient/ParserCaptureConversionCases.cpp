#include "OfflineCase.hpp"
#include "Capture/Parser/Private/CorpusConversion.hpp"
#include "hftrec/CorpusContract/BinaryMarketDepthTransaction.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <cstdint>
#include <limits>

namespace {
namespace ipc=hft_parser::ipc;
namespace corpus=hftrec::corpus;
ipc::MarketCaptureMembershipPayload membershipPayload(const ipc::MarketCaptureRecord& record) {
  CXET_CHECK(record.header.channel==ipc::MarketCaptureChannel::Membership);
  CXET_CHECK(record.header.payloadBytes==sizeof(ipc::MarketCaptureMembershipPayload));
  ipc::MarketCaptureMembershipPayload value{};
  std::memcpy(&value,record.payload.data(),sizeof(value));
  return value;
}
void writeMembershipPayload(ipc::MarketCaptureRecord& record,const ipc::MarketCaptureMembershipPayload& value) {
  CXET_CHECK(record.header.channel==ipc::MarketCaptureChannel::Membership);
  CXET_CHECK(record.header.payloadBytes==sizeof(value));
  std::memcpy(record.payload.data(),&value,sizeof(value));
}
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
  ipc::MarketCaptureMembershipPayload p{};
  p.source=source();p.source.availableChannelMask=p.source.traderReplayChannelMask=0;
  p.source.compatibility={};p.sourceGeneration=p.previousSourceGeneration=11;p.sessionEpoch=17;
  p.directoryRevision=9;p.requestSequence=4;p.channelMaskBefore=1;p.channelMaskAfter=0;
  p.canonicalSymbolId=7;p.venueId=1;p.marketRaw=2;
  p.affectedChannel=ipc::MarketCaptureChannel::BookTicker;
  p.action=static_cast<std::uint8_t>(ipc::MarketCaptureSubscriptionAction::Remove);
  p.status=static_cast<std::uint8_t>(ipc::MarketCaptureSubscriptionStatus::Applied);
  writeMembershipPayload(r,p);
  return r;
}
void membershipHasExactArrivalAndMetadata() {
  const auto wire=marker();
  CXET_CHECK(offsetof(ipc::MarketCaptureRecord,payload)==104u);
  CXET_CHECK(reinterpret_cast<std::uintptr_t>(wire.payload.data())%alignof(ipc::MarketCaptureMembershipPayload)!=0u);
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
  auto badPayload=membershipPayload(bad);badPayload.sourceGeneration=99;
  writeMembershipPayload(bad,badPayload);
  CXET_CHECK(!hftrec::capture::detail::convertRecord(bad,0,directory,out));
  bad=marker();bad.header.payloadBytes=0;
  CXET_CHECK(!hftrec::capture::detail::convertRecord(bad,0,directory,out));
}
void failedReaddRetainsFailureEvidence() {
  const std::array directory{source()};auto input=marker();
  auto p=membershipPayload(input);
  p.action=static_cast<std::uint8_t>(ipc::MarketCaptureSubscriptionAction::Add);
  p.status=static_cast<std::uint8_t>(ipc::MarketCaptureSubscriptionStatus::LifecycleFailed);
  p.channelMaskBefore=p.channelMaskAfter=0u;
  writeMembershipPayload(input,p);
  corpus::BinaryMarketRecord out{};
  CXET_CHECK(hftrec::capture::detail::convertRecord(input,0,directory,out));
  const auto& result=*corpus::binaryMarketPayload<corpus::BinaryMarketSourceLifecyclePayload>(&out);
  CXET_CHECK(result.kind==corpus::BinaryMarketSourceLifecycleKind::DirectoryChanged);
  CXET_CHECK(result.reason==p.status && result.affectedChannel==1u);
  CXET_CHECK(result.channelMaskBefore==0 && result.channelMaskAfter==0);
}
void nativeClosedPreservesCapabilityAndExactCause() {
  const std::array directory{source()};auto input=marker();
  auto p=membershipPayload(input);
  p.channelMaskBefore=p.channelMaskAfter=1u;p.source=directory[0];p.requestSequence=0u;
  p.cause=ipc::MarketCaptureSourceTransitionCause::NativeClosed;
  writeMembershipPayload(input,p);
  corpus::BinaryMarketRecord out{};
  CXET_CHECK(hftrec::capture::detail::convertRecord(input,0,directory,out));
  const auto& result=*corpus::binaryMarketPayload<corpus::BinaryMarketSourceLifecyclePayload>(&out);
  CXET_CHECK(result.kind==corpus::BinaryMarketSourceLifecycleKind::DirectoryChanged);
  CXET_CHECK(result.cause==corpus::BinaryMarketSourceTransitionCause::NativeClosed);
  CXET_CHECK(result.channelMaskBefore==1u && result.channelMaskAfter==1u && result.requestSequence==0u);
  CXET_CHECK(out.header.receiveMonotonicNs==2000u && out.header.receiveRealtimeNs==1000);
  p.affectedChannel=ipc::MarketCaptureChannel::Trade;
  writeMembershipPayload(input,p);
  CXET_CHECK(!hftrec::capture::detail::convertRecord(input,0,directory,out));
  auto depth=source();depth.market.flags=ipc::MarketDirectoryDepth;
  depth.configuredChannelMask=depth.availableChannelMask=depth.traderReplayChannelMask=4u;
  depth.compatibility={};depth.compatibility[2]=ipc::MarketCaptureCompatibility::ExactTraderReplay;
  const std::array depthDirectory{depth};auto depthMarker=marker();
  auto health=membershipPayload(depthMarker);
  health.source=depth;health.channelMaskBefore=health.channelMaskAfter=4u;health.requestSequence=0u;
  health.affectedChannel=ipc::MarketCaptureChannel::Depth;
  for(const auto cause:{ipc::MarketCaptureSourceTransitionCause::NativeDegraded,ipc::MarketCaptureSourceTransitionCause::NativeRecovered}) {
    health.cause=cause;
    writeMembershipPayload(depthMarker,health);
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
  auto row=marker();auto payload=membershipPayload(row);
  payload.source=input;payload.channelMaskBefore=payload.channelMaskAfter=1u;
  payload.cause=ipc::MarketCaptureSourceTransitionCause::NativeRecovered;payload.requestSequence=0u;
  writeMembershipPayload(row,payload);
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
ipc::MarketCaptureSourceDescriptor metadataSource() {
  auto input=source();
  auto& conversion=input.instrument.quantityConversion;
  conversion.kinds[0]=ipc::MarketCaptureQuantityKind::NativeBase;
  conversion.canonicalBaseMultiplier=conversion.nativeBaseMultiplier=1u;
  return input;
}
void preparedMetadataOwnsExactSourceAndProvenance() {
  auto input=metadataSource();
  auto prepared=hftrec::capture::detail::prepareInstrumentMetadata(input,0u,17u,23u);
  CXET_CHECK(prepared.qualified);
  CXET_CHECK(prepared.exchange=="fixture" && prepared.market=="spot");
  CXET_CHECK(prepared.canonicalSymbol=="AAA" && prepared.nativeSymbol=="AAA");
  CXET_CHECK(prepared.metadata.exchange=="fixture" && prepared.metadata.market=="spot" && prepared.metadata.symbol=="AAA");
  CXET_CHECK(prepared.producerEpoch==17u && prepared.directoryGeneration==23u);
  CXET_CHECK(prepared.sourceId==1u && prepared.sourceGeneration==11u);
  input.market.sourceGeneration=std::uint64_t{1u}<<40u;
  const auto later=hftrec::capture::detail::prepareInstrumentMetadata(input,0u,17u,24u);
  CXET_CHECK(later.qualified && later.sourceGeneration==(std::uint64_t{1u}<<40u) && later.directoryGeneration==24u);
  CXET_CHECK(prepared.sourceGeneration==11u && prepared.directoryGeneration==23u);
  input.market.venueCode.fill('Z');input.market.canonicalSymbol.fill('Z');input.market.nativeSymbol.fill('Z');
  CXET_CHECK(prepared.exchange=="fixture" && prepared.metadata.symbol=="AAA" && prepared.nativeSymbol=="AAA");
}
void preparedMetadataUsesOnlyProvenCanonicalFacts() {
  auto input=metadataSource();
  input.market.tickSizeRaw=5;
  input.market.flags|=ipc::MarketDirectoryTickSize;
  input.market.priceBasisQtyRaw=100'000'000;
  input.market.expiryRealtimeNs=1'800'000'000'000'000'000u;
  input.instrument.stepSizeRaw=6;input.instrument.canonicalStepSizeRaw=1'200'000;
  input.instrument.contractBaseQtyRaw=20'000'000;
  input.instrument.flags=ipc::MarketCaptureInstrumentStepSize |
      ipc::MarketCaptureInstrumentCanonicalStepSize | ipc::MarketCaptureInstrumentContractBaseQty;
  const auto prepared=hftrec::capture::detail::prepareInstrumentMetadata(input,0u,17u,23u);
  CXET_CHECK(prepared.qualified);
  CXET_CHECK(prepared.metadata.tickSizeE8==5 && prepared.metadata.lotSizeE8==1'200'000);
  CXET_CHECK(prepared.metadata.contractBaseQtyE8==20'000'000 && prepared.metadata.priceBasisQtyE8==100'000'000);
  CXET_CHECK(prepared.metadata.expiryUtcNs==1'800'000'000'000'000'000LL);
  input=metadataSource();input.market.tickSizeRaw=5;
  input.instrument.stepSizeRaw=6;input.instrument.flags=ipc::MarketCaptureInstrumentStepSize;
  const auto nativeStepOnly=hftrec::capture::detail::prepareInstrumentMetadata(input,0u,17u,23u);
  CXET_CHECK(nativeStepOnly.qualified && !nativeStepOnly.metadata.lotSizeE8.has_value());
  CXET_CHECK(!nativeStepOnly.metadata.tickSizeE8.has_value() && !nativeStepOnly.metadata.contractBaseQtyE8.has_value());
  CXET_CHECK(!nativeStepOnly.metadata.expiryUtcNs.has_value() && !nativeStepOnly.metadata.priceBasisQtyE8.has_value());
}
void preparedMetadataRefusesInvalidSourceAndProvenance() {
  auto input=metadataSource();
  CXET_CHECK(!hftrec::capture::detail::prepareInstrumentMetadata(input,1u,17u,23u).qualified);
  CXET_CHECK(!hftrec::capture::detail::prepareInstrumentMetadata(input,0u,0u,23u).qualified);
  CXET_CHECK(!hftrec::capture::detail::prepareInstrumentMetadata(input,0u,17u,0u).qualified);
  input.market.sourceGeneration=0u;
  CXET_CHECK(!hftrec::capture::detail::prepareInstrumentMetadata(input,0u,17u,23u).qualified);
  input=metadataSource();input.instrument.canonicalStepSizeRaw=1;
  CXET_CHECK(!hftrec::capture::detail::prepareInstrumentMetadata(input,0u,17u,23u).qualified);
  input=metadataSource();input.market.expiryRealtimeNs=std::uint64_t{std::numeric_limits<std::int64_t>::max()}+1u;
  CXET_CHECK(!hftrec::capture::detail::prepareInstrumentMetadata(input,0u,17u,23u).qualified);
  input=source();
  CXET_CHECK(!hftrec::capture::detail::prepareInstrumentMetadata(input,0u,17u,23u).qualified);
}
void preparedMetadataReversesPriceBasisDenominationExactlyOnce() {
  auto input=metadataSource();
  auto& conversion=input.instrument.quantityConversion;
  conversion.nativeBaseMultiplier=1000u;input.market.priceBasisQtyRaw=50'000;
  const auto native=hftrec::capture::detail::prepareInstrumentMetadata(input,0u,17u,23u);
  CXET_CHECK(native.qualified && native.metadata.priceBasisQtyE8==50'000'000);
  conversion.nativeBaseMultiplier=1u;conversion.canonicalBaseMultiplier=1000u;
  input.market.priceBasisQtyRaw=50'000'000;
  const auto inverse=hftrec::capture::detail::prepareInstrumentMetadata(input,0u,17u,23u);
  CXET_CHECK(inverse.qualified && inverse.metadata.priceBasisQtyE8==50'000);
  input.market.priceBasisQtyRaw=50'000'001;
  CXET_CHECK(!hftrec::capture::detail::prepareInstrumentMetadata(input,0u,17u,23u).qualified);
  conversion.nativeBaseMultiplier=1000u;conversion.canonicalBaseMultiplier=1u;
  input.market.priceBasisQtyRaw=std::numeric_limits<std::int64_t>::max();
  CXET_CHECK(!hftrec::capture::detail::prepareInstrumentMetadata(input,0u,17u,23u).qualified);
  conversion.nativeBaseMultiplier=3u;input.market.priceBasisQtyRaw=100'000'000;
  CXET_CHECK(!hftrec::capture::detail::prepareInstrumentMetadata(input,0u,17u,23u).qualified);
}
void preparedMetadataConvertsScalesWithoutRounding() {
  auto input=metadataSource();
  input.market.priceScale=input.market.quantityScale=6u;
  input.market.tickSizeRaw=123;input.market.flags|=ipc::MarketDirectoryTickSize;
  input.market.priceBasisQtyRaw=50'000;
  input.instrument.quantityConversion.nativeBaseMultiplier=1000u;
  const auto prepared=hftrec::capture::detail::prepareInstrumentMetadata(input,0u,17u,23u);
  CXET_CHECK(prepared.qualified && prepared.metadata.tickSizeE8==12'300);
  CXET_CHECK(prepared.metadata.priceBasisQtyE8==5'000'000'000LL);
  input=metadataSource();input.market.priceScale=9u;
  input.market.tickSizeRaw=11;input.market.flags|=ipc::MarketDirectoryTickSize;
  CXET_CHECK(!hftrec::capture::detail::prepareInstrumentMetadata(input,0u,17u,23u).qualified);
  input=metadataSource();input.market.quantityScale=9u;input.market.priceBasisQtyRaw=11;
  CXET_CHECK(!hftrec::capture::detail::prepareInstrumentMetadata(input,0u,17u,23u).qualified);
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
  tail.header.sourceFlags=hft_parser::market::SourceExchangeTimestampMissing;
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
    cxet::testing::Case{"capture.prepared_metadata_owns_exact_source_and_provenance",preparedMetadataOwnsExactSourceAndProvenance},
    cxet::testing::Case{"capture.prepared_metadata_uses_only_proven_canonical_facts",preparedMetadataUsesOnlyProvenCanonicalFacts},
    cxet::testing::Case{"capture.prepared_metadata_refuses_invalid_source_and_provenance",preparedMetadataRefusesInvalidSourceAndProvenance},
    cxet::testing::Case{"capture.prepared_metadata_reverses_price_basis_denomination_once",preparedMetadataReversesPriceBasisDenominationExactlyOnce},
    cxet::testing::Case{"capture.prepared_metadata_converts_scales_without_rounding",preparedMetadataConvertsScalesWithoutRounding},
    cxet::testing::Case{"capture.composed_snapshot_preserves_authoritative_state_and_absent_clocks",composedSnapshotPreservesAuthoritativeStateAndAbsentClocks}};
  return cxet::testing::runCases(argc,argv,cases);
}
