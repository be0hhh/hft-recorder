#include "OfflineCase.hpp"
#include "hftrec/CorpusContract/BinaryMarketCorpusReader.hpp"
#include "hftrec/CorpusContract/BinaryMarketCorpusWriter.hpp"
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
  std::array<BinaryMarketSource,1> sources{};
  BinaryMarketCorpusWriter writer;
  BinaryMarketCorpusReader reader;
  Fixture() {
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
    BinaryMarketWriterConfig cfg{}; cfg.root=root; cfg.sources=sources; cfg.producerEpoch=13;
    cfg.maximumBytes=16*1024; cfg.segmentTargetBytes=4096; cfg.targetDurationNs=100000;
    cfg.startedReceiveNs=100; cfg.startedMonotonicNs=100; cfg.ringCapacity=2; cfg.shardCount=1;
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
    BinaryMarketSelectionRequest r{}; r.root=root; r.exchange="fixture"; r.market="spot";
    r.canonicalSymbol="AAA"; r.beginReceiveNs=100; r.endReceiveNs=10000;
    r.channelMask=r.requiredChannelMask=binaryMarketChannelBit(BinaryMarketChannel::BookTicker); return r;
  }
};
void roundtrip() {
  Fixture f; CXET_CHECK(f.writer.append(f.record(1))==Status::Ok); f.seal();
  BinaryMarketSelection out; std::string error;
  CXET_CHECK(f.reader.select(f.request(),out,error)==Status::Ok && out.records.size()==1);
  CXET_CHECK(out.sourceGeneration==11 && out.records[0].header.sessionEpoch==17);
  const auto* p=binaryMarketPayload<BinaryMarketBookTickerPayload>(&out.records[0]);
  CXET_CHECK(p->bidPriceRaw==100 && p->askQtyRaw==3 && out.manifest.complete==1);
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
  const auto offset=static_cast<std::streamoff>(sizeof(BinaryMarketSegmentHeader)
      + offsetof(BinaryMarketRecord,payload)+offsetof(BinaryMarketBookTickerPayload,bidQtyRaw));
  std::fstream file(segment,std::ios::binary|std::ios::in|std::ios::out);
  file.seekg(offset); const auto original=file.get(); CXET_CHECK(file.good());
  // Quantity 2 -> 3 is still a valid row; its sealed CRC must reject it.
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
  for (; sequence<100 && status==Status::Ok; ++sequence) status=f.writer.append(f.record(sequence,200+sequence));
  CXET_CHECK(status==Status::OutOfRange && sequence<100 && f.writer.snapshot().quotaReached);
  const auto before=f.writer.snapshot().recordCount;
  CXET_CHECK(f.writer.beginFinalDrain()==Status::Ok);
  CXET_CHECK(f.writer.append(f.record(sequence,200+sequence))==Status::Ok);
  CXET_CHECK(f.writer.snapshot().recordCount==before+1 && f.writer.snapshot().projectedBytes<=16*1024);
  f.seal();
}
}
int main(int argc,char** argv) {
  const cxet::testing::Case cases[]{
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
