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
  const auto offset=static_cast<std::streamoff>(sizeof(BinaryMarketSegmentHeader) + sizeof(BinaryMarketBlockHeader));
  std::fstream file(segment,std::ios::binary|std::ios::in|std::ios::out);
  file.seekg(offset); const auto original=file.get(); CXET_CHECK(file.good());
  // Corrupt the independent codec block while keeping the file size unchanged.
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
  for (; sequence<100000 && status==Status::Ok; ++sequence) status=f.writer.append(f.record(sequence,200+sequence));
  CXET_CHECK(status==Status::OutOfRange && sequence<100000 && f.writer.snapshot().quotaReached);
  const auto before=f.writer.snapshot().recordCount;
  CXET_CHECK(f.writer.beginFinalDrain()==Status::Ok);
  CXET_CHECK(f.writer.append(f.record(sequence,200+sequence))==Status::Ok);
  CXET_CHECK(f.writer.snapshot().recordCount==before+1 && f.writer.snapshot().projectedBytes<=16*1024);
  f.seal();
}
void uuidTailRoundtrip() {
  Fixture f; auto record = f.record(1u);
  record.payload = {};
  record.header.channel = BinaryMarketChannel::Trade;
  record.header.sourceFlags = 0u;
  record.header.payloadBytes = sizeof(BinaryMarketTradePayload);
  record.header.nativeIdentityShape = 4u;
  *binaryMarketPayload<BinaryMarketTradePayload>(&record) = {100, 1, 1u, {}};
  CXET_CHECK(f.writer.append(record) == Status::Ok);
  record.header.nativeIdentityFirst = UINT64_MAX;
  record.header.eventSequence = record.header.frameSequence = record.header.shardSequence = 2u;
  CXET_CHECK(f.writer.append(record) == Status::Ok); f.seal();
  auto request = f.request();
  request.channelMask = request.requiredChannelMask = binaryMarketChannelBit(BinaryMarketChannel::Trade);
  BinaryMarketSelection output; std::string error;
  CXET_CHECK(f.reader.select(request, output, error) == Status::Ok && output.records.size() == 2u);
  CXET_CHECK(output.records[0].header.nativeIdentityFirst == 0u && output.records[0].header.nativeIdentityShape == 4u);
  CXET_CHECK(output.records[1].header.nativeIdentityFirst == UINT64_MAX && output.records[1].header.nativeIdentitySecond == 0u);
}
void uuidTailInvalidBoundary() {
  Fixture f; auto record = f.record(1u);
  record.header.nativeIdentityShape = 4u;
  CXET_CHECK(!validBinaryMarketRecord(record));
  record.header.channel = BinaryMarketChannel::Trade;
  CXET_CHECK(validBinaryMarketRecord(record));
  record.header.nativeIdentitySecond = 1u;
  CXET_CHECK(!validBinaryMarketRecord(record));
  record.header.nativeIdentitySecond = 0u; record.header.nativeIdentityShape = 3u;
  CXET_CHECK(!validBinaryMarketRecord(record));
  record.header.nativeIdentityShape = 4u; record.header.payloadBytes = 0u;
  CXET_CHECK(!validBinaryMarketRecord(record));
}
