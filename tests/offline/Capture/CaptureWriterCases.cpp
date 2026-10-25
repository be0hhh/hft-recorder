#include "OfflineCase.hpp"
#include "Capture/Parser/BufferedCorpusWriter.hpp"
#include "hftrec/CorpusContract/BinaryMarketCorpusReader.hpp"
#include <array>
#include <filesystem>
#include <fstream>
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
  config.retainedCompressedBytes=1; // Force lag/archive path without blocking capture.
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
  CorpusProducerProgress progress{};progress.producerEpoch=1;progress.shardCount=1;
  progress.safeMonotonicNs=300;progress.shardSequences[0]=200;progress.directoryRevision=1;
  CXET_CHECK(queue.appendProgress(progress)==Status::Ok);
  CXET_CHECK(queue.finish()==Status::Ok);
  const auto activeCut=queue.latestCut();
  CXET_CHECK(activeCut && !activeCut->finalized && activeCut->progress.safeMonotonicNs==300);
  CXET_CHECK(!activeCut->blocks.empty());
  CorpusActiveCursor active;std::string activeError;
  CXET_CHECK(active.open(root,0,activeError,queue.publication())==Status::Ok);
  BinaryMarketRecord activeRecord{};CorpusCursorResult result{};
  for(std::uint64_t n=1;n<=200;++n) {
    if(n==100) {
      CXET_CHECK(active.next(activeRecord,result,activeError)==Status::Ok);
      CXET_CHECK(result==CorpusCursorResult::WouldBlock); // Exclusive Parser cut.
      progress.safeMonotonicNs=401;
      CXET_CHECK(writer.updateProgress(progress)==Status::Ok);
      CXET_CHECK(writer.publishCut()==Status::Ok);
      CXET_CHECK(active.refresh(activeError)==Status::Ok);
    }
    CXET_CHECK(active.next(activeRecord,result,activeError)==Status::Ok);
    CXET_CHECK(result==CorpusCursorResult::Record);
    CXET_CHECK(activeRecord.header.shardSequence==n);
    CXET_CHECK(activeRecord.header.receiveMonotonicNs==200+n);
    CXET_CHECK(activeRecord.header.receiveRealtimeNs==static_cast<std::int64_t>(200+n));
  }
  BinaryMarketRecord older=activeRecord;
  older.header.eventSequence=older.header.frameSequence=older.header.shardSequence=201;
  older.header.receiveMonotonicNs=50;
  CXET_CHECK(writer.append(older)==Status::CorruptData); // Executable anchor is frozen.
  CXET_CHECK(active.usingArchive());
  CXET_CHECK(active.next(activeRecord,result,activeError)==Status::Ok);
  CXET_CHECK(result==CorpusCursorResult::WouldBlock);
  std::shared_ptr<const CorpusCut> diskCut;
  CXET_CHECK(readCorpusCut(root,diskCut,activeError)==Status::Ok);
  CXET_CHECK(diskCut->generation>activeCut->generation);
  CorpusSummary summary{};
  CXET_CHECK(readCorpusSummary(root,summary,activeError)==Status::Ok);
  CXET_CHECK(summary.cutGeneration==diskCut->generation && summary.manifest.recordCount==200 && !summary.finalized);
  const auto identity=corpusIdentity(summary.manifest);
  CXET_CHECK(identity=="cxm:1");
  auto adjustedAnchor=summary.manifest;adjustedAnchor.startedReceiveNs-=1;
  adjustedAnchor.replayAnchorReceiveNs-=1;adjustedAnchor.replayAnchorMonotonicNs-=1;
  CXET_CHECK(corpusIdentity(adjustedAnchor)==identity);
  adjustedAnchor.producerEpoch+=1;
  CXET_CHECK(corpusIdentity(adjustedAnchor)!=identity);
  std::vector<BinaryMarketSource> sourceCatalog;
  CXET_CHECK(readCorpusSourceCatalog(root,sourceCatalog,activeError)==Status::Ok);
  CXET_CHECK(sourceCatalog.size()==1 && sourceCatalog[0].canonicalSymbolId==1);
  const auto cutBytes=std::filesystem::file_size(root/"cut.bin");
  {std::ofstream appended(root/"cut.bin",std::ios::binary|std::ios::app);appended.put('x');}
  CXET_CHECK(readCorpusSummary(root,summary,activeError)==Status::CorruptData);
  std::filesystem::resize_file(root/"cut.bin",cutBytes);
  CXET_CHECK(readCorpusSummary(root,summary,activeError)==Status::Ok);
  char originalTail{};
  {std::fstream altered(root/"cut.bin",std::ios::binary|std::ios::in|std::ios::out);
    altered.seekg(-1,std::ios::end);altered.get(originalTail);altered.seekp(-1,std::ios::end);
    altered.put(static_cast<char>(static_cast<unsigned char>(originalTail)^1u));}
  // Summaries and source catalogs validate their own bounded sections, without
  // scanning historical block ranges or a potentially large reference payload.
  CXET_CHECK(readCorpusSummary(root,summary,activeError)==Status::Ok);
  CXET_CHECK(readCorpusSourceCatalog(root,sourceCatalog,activeError)==Status::Ok);
  std::shared_ptr<const CorpusCut> damaged;
  CXET_CHECK(readCorpusCut(root,damaged,activeError)==Status::CorruptData);
  {std::fstream restored(root/"cut.bin",std::ios::binary|std::ios::in|std::ios::out);
    restored.seekp(-1,std::ios::end);restored.put(originalTail);}
  std::shared_ptr<const CorpusCut> refused;
  CXET_CHECK(readCorpusCut(root,refused,activeError,1)==Status::OutOfRange);
  CorpusActiveCursor tooSmall;
  CXET_CHECK(tooSmall.open(root,0,activeError,queue.publication(),{},1)==Status::OutOfRange);
  std::uint64_t acknowledged=0;
  CorpusCompressedBlockSource remote{};remote.context=&acknowledged;
  remote.approvedGeneration=[](void* context,std::uint64_t& generation,std::string&) noexcept {
    generation=*static_cast<std::uint64_t*>(context);return Status::Ok;
  };
  CorpusActiveCursor gated;
  CXET_CHECK(gated.open(root,0,activeError,{},remote)==Status::Unknown);
  acknowledged=diskCut->generation;
  CXET_CHECK(gated.open(root,0,activeError,{},remote)==Status::Ok);
  std::vector<std::uint8_t> compressed;
  CXET_CHECK(readCorpusCompressedBlock(root,*diskCut,0,compressed,activeError)==Status::Ok);
  CXET_CHECK(compressed.size()==sizeof(BinaryMarketBlockHeader)+diskCut->blocks[0].header.encodedBytes);
  CXET_CHECK(writer.snapshot().recordCount==200);
  CXET_CHECK(writer.finalize(BinaryMarketStopReason::Requested,10000)==Status::Ok);
  CXET_CHECK(active.refresh(activeError)==Status::Ok);
  CXET_CHECK(active.next(activeRecord,result,activeError)==Status::Ok);
  CXET_CHECK(result==CorpusCursorResult::End);
  CXET_CHECK(readCorpusSummary(root,summary,activeError)==Status::Ok && summary.finalized);
  CXET_CHECK(corpusIdentity(summary.manifest)==identity);
  // Canonical manifest witnesses the final cut's prior fsync, so an offline
  // archive does not depend on its original Recorder daemon staying reachable.
  remote.approvedGeneration=[](void*,std::uint64_t&,std::string&) noexcept {return Status::IoError;};
  CorpusActiveCursor offline;
  CXET_CHECK(offline.open(root,0,activeError,{},remote)==Status::Ok);
  CXET_CHECK(offline.usingArchive());
  // Published snapshots remain immutable after final archive metadata replaces.
  CXET_CHECK(!activeCut->finalized && activeCut->progress.safeMonotonicNs==300);
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
