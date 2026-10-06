#include "OfflineCase.hpp"
#include "Capture/Parser/Private/CorpusConversion.hpp"
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
  bad=marker();bad.header.schemaVersion=5;
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
}
int main(int argc,char** argv) {
  const cxet::testing::Case cases[]{
    cxet::testing::Case{"capture.membership_exact_arrival_metadata",membershipHasExactArrivalAndMetadata},
    cxet::testing::Case{"capture.membership_tag_and_historical_metadata",membershipCannotBecomeLiquidationOrUseFutureMetadata},
    cxet::testing::Case{"capture.failed_readd_retains_failure_evidence",failedReaddRetainsFailureEvidence},
    cxet::testing::Case{"capture.native_closed_preserves_capability_and_exact_cause",nativeClosedPreservesCapabilityAndExactCause},
    cxet::testing::Case{"capture.execution_quantity_grid_survives_source_conversion",exactExecutionQuantitySurvivesSourceConversion},
    cxet::testing::Case{"capture.market_quantity_authority_survives_conversion_and_lifecycle",marketQuantityAuthoritySurvivesConversionAndLifecycle},
    cxet::testing::Case{"capture.malformed_market_quantity_authority_refuses",captureMalformedQuantityAuthorityRefuses}};
  return cxet::testing::runCases(argc,argv,cases);
}
