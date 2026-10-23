void nativeQuantityAuthorityRoundtrip() {
  BinaryMarketQuantityConversion conversion{};
  conversion.kinds={BinaryMarketQuantityKind::NativeBase,BinaryMarketQuantityKind::NativeContract,BinaryMarketQuantityKind::NativeLot};
  conversion.canonicalBaseMultiplier=1u;conversion.nativeBaseMultiplier=1000u;
  conversion.nativeContractBaseQtyRaw=10'000;conversion.nativeLotBaseQtyRaw=20'000;
  Fixture f(4*1024*1024,1,true,false,&conversion);
  CXET_CHECK(f.writer.append(f.record(1u))==Status::Ok);f.seal();
  BinaryMarketSelection out;std::string error;
  CXET_CHECK(f.reader.select(f.request(),out,error)==Status::Ok && out.records.size()==1u);
  CXET_CHECK(out.sourcesAtBegin[0].quantityConversion==conversion);
  const auto* raw=binaryMarketPayload<BinaryMarketBookTickerPayload>(&out.records[0]);
  CXET_CHECK(raw->bidQtyRaw==2 && raw->askQtyRaw==3);
  auto state=initialBinaryMarketSourceState(out.sourcesAtBegin[0]);
  auto marker=lifecycle(f,2u,201,BinaryMarketSourceLifecycleKind::DirectoryChanged,
      out.sourcesAtBegin[0],11u,0xffu,0xffu);
  auto& p=*binaryMarketPayload<BinaryMarketSourceLifecyclePayload>(&marker);
  p.requestSequence=0u;p.affectedChannel=1u;p.cause=BinaryMarketSourceTransitionCause::NativeStale;
  CXET_CHECK(validBinaryMarketRecord(marker));
  p.source.quantityConversion.nativeContractBaseQtyRaw+=1;
  CXET_CHECK(!advanceBinaryMarketSourceState(marker,state));
}
void malformedQuantityAuthorityIsNotRecordable() {
  Fixture f;auto source=f.sources[0];auto& q=source.quantityConversion;
  CXET_CHECK(validBinaryMarketSource(source,0u));
  q.kinds[0]=static_cast<BinaryMarketQuantityKind>(255u);
  CXET_CHECK(!validBinaryMarketSource(source,0u));
  q={};q.kinds[0]=BinaryMarketQuantityKind::NativeContract;
  q.canonicalBaseMultiplier=q.nativeBaseMultiplier=1u;
  CXET_CHECK(!validBinaryMarketSource(source,0u));
  q.nativeContractBaseQtyRaw=1;
  CXET_CHECK(validBinaryMarketSource(source,0u));
  q.kinds[0]=BinaryMarketQuantityKind::NativeLot;
  q.nativeContractBaseQtyRaw=0;
  CXET_CHECK(!validBinaryMarketSource(source,0u));
  q.nativeLotBaseQtyRaw=1;
  CXET_CHECK(validBinaryMarketSource(source,0u));
  q.kinds[2]=BinaryMarketQuantityKind::NativeBase;
  CXET_CHECK(!validBinaryMarketSource(source,0u));
  q.kinds[2]=BinaryMarketQuantityKind::Unknown;
  q.reserved[0]=std::byte{1};
  CXET_CHECK(!validBinaryMarketSource(source,0u));
  q={};q.nativeBaseMultiplier=1u;
  CXET_CHECK(!validBinaryMarketSource(source,0u));
}