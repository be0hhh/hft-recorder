#include "OfflineCase.hpp"
#include "Capture/Parser/BufferedCorpusWriter.hpp"
#include "hftrec/CorpusContract/BinaryMarketCorpusReader.hpp"
#include <array>
#include <filesystem>
#include <string>
#include <unistd.h>

namespace {
using namespace hftrec;
using namespace hftrec::corpus;
void queuedCapturePreservesRecords() {
  const auto root=std::filesystem::current_path()/
      ("buffered-corpus-"+std::to_string(::getpid()));
  CXET_CHECK(!std::filesystem::exists(root));
  BinaryMarketSource source{};
  source.sourceId=1;source.canonicalSymbolId=1;source.initialSourceGeneration=1;
  source.venueId=1;source.marketRaw=2;source.marketKindRaw=1;
  source.economicBaseAssetId=1;source.quoteAssetId=2;
  source.priceScale=source.quantityScale=8;
  source.venue[0]='x';source.venueBytes=1;
  source.market[0]='s';source.market[1]='p';source.market[2]='o';source.market[3]='t';source.marketBytes=4;
  source.canonicalSymbol[0]='A';source.canonicalSymbolBytes=1;
  source.nativeSymbol[0]='A';source.nativeSymbolBytes=1;
  source.directoryFlags=BinaryMarketDirectoryBookTicker;
  source.configuredChannelMask=source.availableChannelMask=source.traderReplayChannelMask=1;
  source.compatibility[0]=BinaryMarketCompatibility::ExactTraderReplay;
  BinaryMarketCorpusWriter writer;
  BinaryMarketWriterConfig config{};config.root=root;config.sources={&source,1};
  config.producerEpoch=1;config.maximumBytes=1u<<20;config.segmentTargetBytes=1u<<16;
  config.targetDurationNs=10000;config.startedReceiveNs=100;config.startedMonotonicNs=100;
  config.ringCapacity=1;config.shardCount=1;config.pendingRecordCapacity=256;
  CXET_CHECK(writer.start(config)==Status::Ok);
  capture::BufferedCorpusWriter queue(writer);
  CXET_CHECK(queue.start(256)==Status::Ok);
  for(std::uint64_t i=1;i<=200;++i) {
    BinaryMarketRecord record{};auto& h=record.header;
    h.sourceId=1;h.sourceGeneration=1;h.sessionEpoch=1;
    h.eventSequence=h.frameSequence=h.shardSequence=i;
    h.receiveRealtimeNs=200+i;h.receiveMonotonicNs=200+i;
    h.exchangeTimestampNs=150+i;h.flags=BinaryMarketRecordTraderReplayCompatible;
    h.payloadBytes=sizeof(BinaryMarketBookTickerPayload);
    h.sourceFlags=BinaryMarketSourceBookTickerBidPresent|BinaryMarketSourceBookTickerAskPresent;
    *binaryMarketPayload<BinaryMarketBookTickerPayload>(&record)={100,2,101,3};
    CXET_CHECK(queue.append(record)==Status::Ok);
  }
  CXET_CHECK(queue.finish()==Status::Ok);
  CXET_CHECK(writer.snapshot().recordCount==200);
  CXET_CHECK(writer.finalize(BinaryMarketStopReason::Requested,10000)==Status::Ok);
  BinaryMarketSelectionRequest request{};request.root=root;request.exchange="x";
  request.market="spot";request.canonicalSymbol="A";request.beginReceiveNs=100;
  request.endReceiveNs=10000;request.channelMask=request.requiredChannelMask=1;
  BinaryMarketSelection output;std::string error;
  CXET_CHECK(BinaryMarketCorpusReader{}.select(request,output,error)==Status::Ok);
  CXET_CHECK(output.records.size()==200);
  for(std::size_t i=0;i<output.records.size();++i)
    CXET_CHECK(output.records[i].header.shardSequence==i+1);
  std::filesystem::remove_all(root);
}
void invalidQueueCapacityRefuses() {
  BinaryMarketCorpusWriter writer;
  capture::BufferedCorpusWriter queue(writer);
  CXET_CHECK(queue.start(0)==Status::InvalidArgument);
  CXET_CHECK(queue.start(65537)==Status::InvalidArgument);
}
}
int main(int argc,char** argv) {
  const cxet::testing::Case cases[]{
    cxet::testing::Case{"corpus.buffered_capture_preserves_records",queuedCapturePreservesRecords},
    cxet::testing::Case{"corpus.buffered_capture_capacity_refuses",invalidQueueCapacityRefuses}};
  return cxet::testing::runCases(argc,argv,cases);
}
